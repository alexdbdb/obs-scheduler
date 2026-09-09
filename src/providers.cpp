#include "providers.hpp"
#include "calendar.hpp"
#include "google-client.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>
#include <QtConcurrent>
namespace bs {
QJsonObject Providers::googleClient() const {
  // Preserve credentials associated with accounts connected by older builds.
  auto c = store.config("google");
  if (!c["client_id"].toString().isEmpty() &&
      (QString(BS_GOOGLE_CLIENT_ID).isEmpty() || !secrets.read("google-refresh").isEmpty())) {
    c["client_secret"] = QString::fromUtf8(secrets.read("google-client-secret"));
    return c;
  }
  return {{"client_id", BS_GOOGLE_CLIENT_ID}, {"client_secret", BS_GOOGLE_CLIENT_SECRET}};
}
QJsonObject Providers::googleStatus() const {
  const bool configured = !QString(BS_GOOGLE_CLIENT_ID).isEmpty() ||
                          !store.config("google")["client_id"].toString().isEmpty();
  QJsonObject result{{"configured", configured}, {"connected", false}, {"connecting", connecting}};
  if (configured) {
    try { result["connected"] = !secrets.read("google-refresh").isEmpty(); }
    catch (const std::exception &) { result["storage_error"] = true; }
  }
  return result;
}
Providers::Providers(Store &s, Secrets &v, QObject *p)
    : QObject(p), store(s), secrets(v) {
  parsing.setMaxThreadCount(1);
  connect(&oauth, &QTcpServer::newConnection, this, [this] {
    while (oauth.hasPendingConnections()) {
      auto sock = oauth.nextPendingConnection();
      callbacks.insert(sock, {});
      connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
      connect(sock, &QObject::destroyed, this,
              [this, sock] { callbacks.remove(sock); });
      QTimer::singleShot(10000, sock, [sock] { sock->disconnectFromHost(); });
      connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
        try {
        auto &b = callbacks[sock];
        b += sock->readAll();
        if (b.size() > 8192) {
          sock->disconnectFromHost();
          return;
        }
        if (!b.contains("\r\n\r\n"))
          return;
        auto line = b.left(b.indexOf("\r\n")).split(' ');
        if (line.size() != 3 || line[0] != "GET") {
          sock->disconnectFromHost();
          return;
        }
        QUrl url("http://localhost" + QString::fromUtf8(line[1]));
        QUrlQuery q(url);
        if (url.path() != "/oauth/callback" || state.isEmpty() ||
            q.queryItemValue("state").toUtf8() != state) {
          sock->write("HTTP/1.1 400 Bad Request\r\nContent-Length: "
                      "0\r\nConnection: close\r\n\r\n");
          sock->disconnectFromHost();
          return;
        }
        sock->write(
            "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "
            "29\r\nConnection: close\r\n\r\nReturn to OBS Studio to finish");
        sock->disconnectFromHost();
        oauth.close();
        state.clear();
        if (q.queryItemValue("code").isEmpty()) {
          connecting = false;
          verifier.clear();
          emit changed();
          emit problem("Google authorization was denied");
          return;
        }
        auto c = googleClient();
        QUrlQuery form;
        form.addQueryItem("client_id", c["client_id"].toString());
        form.addQueryItem("client_secret", c["client_secret"].toString());
        form.addQueryItem("code", q.queryItemValue("code"));
        form.addQueryItem("code_verifier", QString::fromUtf8(verifier));
        form.addQueryItem("redirect_uri", redirect);
        form.addQueryItem("grant_type", "authorization_code");
        token(form.query(QUrl::FullyEncoded).toUtf8(), [this](QString err) {
          connecting = false;
          emit changed();
          if (!err.isEmpty())
            emit problem(err);
          else {
            store.log("info", "Google account connected");
            listGoogle();
          }
        });
        verifier.clear();
        } catch (const std::exception &) {
          sock->disconnectFromHost();
          oauth.close();
          state.clear();
          verifier.clear();
          connecting = false;
          emit changed();
          emit problem("Google connection failed. Check secure credential storage and try again.");
        }
      });
    }
  });
}
Providers::~Providers() {
  oauth.close();
  parsing.waitForDone();
}
void Providers::request(const QUrl &url, const QByteArray &bearer,
                        std::function<void(QByteArray, QString)> done) {
  const auto generation = googleGeneration;
  const bool googleRequest = !bearer.isEmpty();
  if (url.scheme() != "https" || !url.userInfo().isEmpty()) {
    done({}, "Calendar URL must use HTTPS without userinfo");
    return;
  }
  QNetworkRequest req(url);
  req.setTransferTimeout(30000);
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::SameOriginRedirectPolicy);
  if (!bearer.isEmpty())
    req.setRawHeader("Authorization", "Bearer " + bearer);
  auto *r = network.get(req);
  connect(r, &QNetworkReply::readyRead, r, [r] {
    if (r->bytesAvailable() > 8 * 1024 * 1024)
      r->abort();
  });
  connect(r, &QNetworkReply::finished, this, [this, r, done, generation, googleRequest] {
    if (googleRequest && generation != googleGeneration) { r->deleteLater(); return; }
    auto data = r->readAll();
    QString err;
    if (r->error() != QNetworkReply::NoError)
      err =
          "Calendar HTTP request failed (status " +
          QString::number(
              r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()) +
          ")";
    if (data.size() > 8 * 1024 * 1024)
      err = "Calendar response exceeds limit";
    r->deleteLater();
    done(data, err);
  });
}
void Providers::token(const QByteArray &body,
                      std::function<void(QString)> done) {
  const auto generation = googleGeneration;
  QNetworkRequest req(QUrl("https://oauth2.googleapis.com/token"));
  req.setHeader(QNetworkRequest::ContentTypeHeader,
                "application/x-www-form-urlencoded");
  req.setTransferTimeout(30000);
  auto *r = network.post(req, body);
  connect(r, &QNetworkReply::finished, this, [this, r, done, generation] {
    if (generation != googleGeneration) { r->deleteLater(); return; }
    auto o = QJsonDocument::fromJson(r->readAll()).object();
    bool ok = r->error() == QNetworkReply::NoError &&
              !o["access_token"].toString().isEmpty();
    r->deleteLater();
    if (!ok) {
      if (o["error"].toString() == "invalid_grant") {
        try { secrets.remove("google-refresh"); } catch (const std::exception &) {}
        access.clear();
        accessExpires = 0;
        emit changed();
      }
      done("Google token request failed; reconnect the account or check OAuth "
           "client configuration");
      return;
    }
    try {
      const auto scopes = o["scope"].toString().split(' ');
      if (o.contains("scope") && !scopes.contains("https://www.googleapis.com/auth/calendar.readonly") &&
          (!scopes.contains("https://www.googleapis.com/auth/calendar.events.readonly") ||
           !scopes.contains("https://www.googleapis.com/auth/calendar.calendarlist.readonly"))) {
        done("Allow both calendar permissions to connect Google Calendar.");
        return;
      }
      auto refresh = o["refresh_token"].toString();
      if (!refresh.isEmpty())
        secrets.write("google-refresh", refresh.toUtf8());
      access = o["access_token"].toString().toUtf8();
      accessExpires = now() + o["expires_in"].toInt(3600) * 1000LL;
      done({});
    } catch (const std::exception &e) {
      done(QString::fromUtf8(e.what()));
    }
  });
}
void Providers::authorized(std::function<void(QString)> done) {
  if (!access.isEmpty() && now() < accessExpires - 60000) {
    done({});
    return;
  }
  try {
    auto refresh = secrets.read("google-refresh");
    if (refresh.isEmpty()) {
      done("Connect a Google account first");
      return;
    }
    auto c = googleClient();
    QUrlQuery form;
    form.addQueryItem("client_id", c["client_id"].toString());
    form.addQueryItem("client_secret", c["client_secret"].toString());
    form.addQueryItem("refresh_token", QString::fromUtf8(refresh));
    form.addQueryItem("grant_type", "refresh_token");
    token(form.query(QUrl::FullyEncoded).toUtf8(), done);
  } catch (const std::exception &e) {
    done(QString::fromUtf8(e.what()));
  }
}
void Providers::connectGoogle() {
  auto c = googleClient();
  if (c["client_id"].toString().isEmpty())
    throw Error("Google connection is not configured in this installer. Contact the plugin distributor.");
  if (connecting) return;
  if (!QString(BS_GOOGLE_CLIENT_ID).isEmpty() && secrets.read("google-refresh").isEmpty()) {
    store.config("google", {});
    secrets.remove("google-client-secret");
  }
  ++googleGeneration;
  secrets.write("storage-check", "ok");
  secrets.remove("storage-check");
  oauth.close();
  if (!oauth.listen(QHostAddress::LocalHost, 0))
    throw Error("Cannot open OAuth loopback listener");
  redirect = "http://127.0.0.1:" + QString::number(oauth.serverPort()) +
             "/oauth/callback";
  verifier = Secrets::random();
  state = Secrets::random();
  connecting = true;
  const auto attempt = state;
  QUrl url("https://accounts.google.com/o/oauth2/v2/auth");
  QUrlQuery q;
  q.addQueryItem("client_id", c["client_id"].toString());
  q.addQueryItem("redirect_uri", redirect);
  q.addQueryItem("response_type", "code");
  q.addQueryItem("scope", "https://www.googleapis.com/auth/calendar.events.readonly "
                         "https://www.googleapis.com/auth/calendar.calendarlist.readonly");
  q.addQueryItem("access_type", "offline");
  q.addQueryItem("prompt", "select_account consent");
  q.addQueryItem("state", QString::fromUtf8(state));
  q.addQueryItem("code_challenge_method", "S256");
  q.addQueryItem(
      "code_challenge",
      QString::fromUtf8(
          QCryptographicHash::hash(verifier, QCryptographicHash::Sha256)
              .toBase64(QByteArray::Base64UrlEncoding |
                        QByteArray::OmitTrailingEquals)));
  url.setQuery(q);
  emit openUrl(url.toString());
  emit changed();
  QTimer::singleShot(300000, this, [this, attempt] {
    if (state != attempt) return;
    oauth.close();
    state.clear();
    verifier.clear();
    connecting = false;
    emit changed();
    emit problem("Google connection timed out. Click Connect with Google to try again.");
  });
}
void Providers::disconnectGoogle() {
  ++googleGeneration;
  connecting = false;
  verifier.clear();
  auto refresh = secrets.read("google-refresh");
  secrets.remove("google-refresh");
  access.clear();
  accessExpires = 0;
  oauth.close();
  state.clear();
  if (!refresh.isEmpty()) {
    QNetworkRequest req(QUrl("https://oauth2.googleapis.com/revoke"));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  "application/x-www-form-urlencoded");
    req.setTransferTimeout(15000);
    auto *r = network.post(
        req, "token=" + QUrl::toPercentEncoding(QString::fromUtf8(refresh)));
    connect(r, &QNetworkReply::finished, r, &QObject::deleteLater);
  }
  auto c = store.config("calendars");
  QJsonArray list;
  for (auto v : c["items"].toArray()) {
    auto o = v.toObject();
    if (o["kind"].toString() == "google") {
      o["enabled"] = false;
      busy.remove(o["id"].toString());
      last.remove(o["id"].toString());
    }
    list.append(o);
  }
  c["items"] = list;
  store.config("calendars", c);
  store.log("info", "Google account disconnected; calendars disabled");
  emit changed();
}
void Providers::listGoogle() {
  authorized([this](QString err) {
    if (!err.isEmpty()) {
      emit problem(err);
      return;
    }
    auto list = std::make_shared<QJsonArray>();
    auto fetch = std::make_shared<std::function<void(QString)>>();
    std::weak_ptr<std::function<void(QString)>> weak = fetch;
    *fetch = [this, list, weak](QString page) {
      QUrl u("https://www.googleapis.com/calendar/v3/users/me/calendarList");
      QUrlQuery q;
      q.addQueryItem("maxResults", "250");
      if (!page.isEmpty())
        q.addQueryItem("pageToken", page);
      u.setQuery(q);
      auto keep = weak.lock();
      request(u, access, [this, list, keep](QByteArray data, QString err) {
        if (!err.isEmpty()) {
          emit problem(err);
          return;
        }
        auto o = QJsonDocument::fromJson(data).object();
        for (auto v : o["items"].toArray())
          list->append(v);
        auto next = o["nextPageToken"].toString();
        if (!next.isEmpty() && list->size() < 5000)
          (*keep)(next);
        else
          emit googleCalendars(*list);
      });
    };
    (*fetch)({});
  });
}
void Providers::complete(const QJsonObject &cal, const QList<Event> &events,
                         const QString &err) {
  busy.remove(cal["id"].toString());
  QString error = err;
  try {
    auto current = store.config("calendars")["items"].toArray();
    bool matches = false;
    for (auto v : current)
      if (v.toObject() == cal)
        matches = true;
    if (!matches)
      return;
    if (error.isEmpty())
      store.replaceCalendar(cal["id"].toString(), events);
  } catch (const std::exception &e) {
    error = QString::fromUtf8(e.what());
  }
  store.config("sync/" + cal["id"].toString(), {{"ok", error.isEmpty()},
                                                {"time", iso(now())},
                                                {"error", error},
                                                {"count", events.size()}});
  store.log(error.isEmpty() ? "info" : "error",
            "Calendar " + cal["id"].toString() +
                (error.isEmpty() ? " synchronized"
                                 : " synchronization failed: " + error));
  emit changed();
}
void Providers::parse(QJsonObject cal, QByteArray data) {
  using Result = QPair<QList<Event>, QString>;
  auto *w = new QFutureWatcher<Result>(this);
  connect(w, &QFutureWatcher<Result>::finished, this, [this, w, cal] {
    auto r = w->result();
    w->deleteLater();
    complete(cal, r.first, r.second);
  });
  w->setFuture(QtConcurrent::run(&parsing, [cal, data]() -> Result {
    try {
      return {CalendarParser::ics(data, cal, now() - 7LL * 86400000,
                                  now() + 400LL * 86400000),
              {}};
    } catch (const std::exception &e) {
      return {{}, QString::fromUtf8(e.what())};
    }
  }));
}
void Providers::sync(bool force) {
  for (auto v : store.config("calendars")["items"].toArray()) {
    auto c = v.toObject();
    auto id = c["id"].toString();
    if (!c["enabled"].toBool(true) || busy.contains(id) ||
        (!force && now() - last.value(id) <
                       qMax(1, c["refresh_minutes"].toInt(15)) * 60000LL))
      continue;
    busy.insert(id);
    last[id] = now();
    auto kind = c["kind"].toString();
    if (kind == "google")
      authorized([this, c](QString err) {
        if (!err.isEmpty())
          complete(c, {}, err);
        else
          googlePage(c, {}, {});
      });
    else if (kind == "file") {
      using Result = QPair<QByteArray, QString>;
      auto *w = new QFutureWatcher<Result>(this);
      connect(w, &QFutureWatcher<Result>::finished, this, [this, w, c] {
        auto r = w->result();
        w->deleteLater();
        if (r.second.isEmpty())
          parse(c, r.first);
        else
          complete(c, {}, r.second);
      });
      w->setFuture(QtConcurrent::run(&parsing, [c]() -> Result {
        QFile f(c["location"].toString());
        if (!f.open(QIODevice::ReadOnly))
          return {{}, "Cannot open ICS file"};
        if (f.size() > 8 * 1024 * 1024)
          return {{}, "ICS file exceeds limit"};
        return {f.readAll(), {}};
      }));
    } else if (kind == "ics")
      request(QUrl(c["location"].toString()), {},
              [this, c](QByteArray data, QString err) {
                if (err.isEmpty())
                  parse(c, data);
                else
                  complete(c, {}, err);
              });
    else
      complete(c, {}, "Unknown provider kind");
  }
}
void Providers::googlePage(QJsonObject cal, QString page, QList<Event> events,
                           int pages) {
  if (pages > 100) {
    complete(cal, {}, "Google pagination limit exceeded");
    return;
  }
  auto id =
      QString::fromLatin1(QUrl::toPercentEncoding(cal["location"].toString()));
  QUrl url("https://www.googleapis.com/calendar/v3/calendars/" + id +
           "/events");
  QUrlQuery q;
  q.addQueryItem("singleEvents", "true");
  q.addQueryItem("showDeleted", "false");
  q.addQueryItem("maxResults", "2500");
  q.addQueryItem("timeMin", iso(now() - 7LL * 86400000));
  q.addQueryItem("timeMax", iso(now() + 400LL * 86400000));
  if (!page.isEmpty())
    q.addQueryItem("pageToken", page);
  url.setQuery(q);
  request(
      url, access,
      [this, cal, events, pages](QByteArray data, QString err) mutable {
        if (!err.isEmpty()) {
          complete(cal, {}, err);
          return;
        }
        try {
          QJsonParseError pe;
          auto doc = QJsonDocument::fromJson(data, &pe);
          if (pe.error != QJsonParseError::NoError ||
              !doc.object().contains("items"))
            throw Error("Invalid Google events response");
          auto o = doc.object();
          for (auto item : o["items"].toArray()) {
            auto g = item.toObject();
            if (g["status"].toString() == "cancelled")
              continue;
            Event e;
            e.calendar = cal["id"].toString();
            e.externalId = g["id"].toString();
            if (e.externalId.isEmpty())
              throw Error("Google event ID missing");
            e.id = QString::fromLatin1(
                QCryptographicHash::hash(
                    (e.calendar + "/" + e.externalId).toUtf8(),
                    QCryptographicHash::Sha256)
                    .toHex());
            e.title = g["summary"].toString("(Untitled)");
            e.description = g["description"].toString();
            e.source = "GoogleCalendar";
            e.templateId = cal["template"].toString();
            e.timezone = g["start"].toObject()["timeZone"].toString(
                cal["timezone"].toString("UTC"));
            auto dt = [&](QString field) {
              auto t = g[field].toObject();
              if (t.contains("dateTime"))
                return instant(t["dateTime"].toString()).toMSecsSinceEpoch();
              return QDateTime(
                         QDate::fromString(t["date"].toString(), Qt::ISODate),
                         QTime(0, 0), QTimeZone(e.timezone.toUtf8()))
                  .toMSecsSinceEpoch();
            };
            e.start = dt("start") + cal["start_offset"].toInt() * 1000LL;
            e.end = dt("end") + cal["end_offset"].toInt() * 1000LL;
            e.metadata = {{"all_day", g["start"].toObject().contains("date")}};
            e.updated = now();
            e.validate();
            events.append(e);
          }
          auto next = o["nextPageToken"].toString();
          if (!next.isEmpty())
            googlePage(cal, next, events, pages + 1);
          else
            complete(cal, events, {});
        } catch (const std::exception &e) {
          complete(cal, {}, QString::fromUtf8(e.what()));
        }
      });
}
} // namespace bs
