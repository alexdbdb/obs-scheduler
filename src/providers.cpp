#include "providers.hpp"
#include "calendar.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>
#include <QtConcurrent>
namespace bs {
namespace {
constexpr qint64 providerWindow = 7LL * 24 * 60 * 60 * 1000;
}
QByteArray formUrlEncoded(const QList<QPair<QString, QString>> &fields) {
  QByteArray body;
  for (const auto &[key, value] : fields) {
    if (!body.isEmpty())
      body += '&';
    body += QUrl::toPercentEncoding(key);
    body += '=';
    body += QUrl::toPercentEncoding(value);
  }
  return body;
}

QString googleApiError(const QByteArray &body, int httpStatus) {
  const auto root = QJsonDocument::fromJson(body).object();
  const auto googleError = root["error"].toObject();
  QString reason;
  const auto errors = googleError["errors"].toArray();
  if (!errors.isEmpty())
    reason = errors.first().toObject()["reason"].toString();
  if (reason.isEmpty()) {
    for (const auto detail : googleError["details"].toArray()) {
      reason = detail.toObject()["reason"].toString();
      if (!reason.isEmpty())
        break;
    }
  }
  auto message = googleError["message"].toString().simplified();
  reason.remove(QRegularExpression("[^A-Za-z0-9_.-]"));
  message.remove(QRegularExpression("[\\x00-\\x1F\\x7F]"));
  if (message.size() > 300)
    message = message.left(300) + "…";
  QStringList details{"HTTP " + QString::number(httpStatus)};
  if (!reason.isEmpty())
    details << reason;
  if (!message.isEmpty())
    details << message;
  return "Calendar HTTP request failed (" + details.join("; ") + ")";
}

QJsonObject Providers::googleClient() const {
  return store.config("google");
}
QJsonObject Providers::googleStatus() const {
  const auto clientId = googleClient()["client_id"].toString();
  bool secretConfigured = false;
  bool storageError = false;
  try { secretConfigured = !secrets.read("google-client-secret").isEmpty(); }
  catch (const std::exception &) { storageError = true; }
  QJsonObject result{{"configured", !clientId.isEmpty() && secretConfigured},
                     {"client_id", clientId},
                     {"client_secret_configured", secretConfigured},
                     {"connected", false},
                     {"connecting", connecting}};
  if (storageError)
    result["storage_error"] = true;
  if (!clientId.isEmpty()) {
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
        const auto callbackState =
            q.queryItemValue("state", QUrl::FullyDecoded).toUtf8();
        const auto code = q.queryItemValue("code", QUrl::FullyDecoded);
        if (url.path() != "/oauth/callback" || state.isEmpty() ||
            callbackState != state) {
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
        if (code.isEmpty()) {
          connecting = false;
          verifier.clear();
          emit changed();
          emit problem("Google authorization was denied");
          return;
        }
        auto c = googleClient();
        const auto form = formUrlEncoded({
            {"client_id", c["client_id"].toString()},
            {"client_secret", QString::fromUtf8(secrets.read("google-client-secret"))},
            {"code", code},
            {"code_verifier", QString::fromUtf8(verifier)},
            {"redirect_uri", redirect},
            {"grant_type", "authorization_code"},
        });
        token(form, [this](QString err) {
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
  auto *reply = network.get(req);
  connect(reply, &QNetworkReply::readyRead, reply, [reply] {
    if (reply->bytesAvailable() > 8 * 1024 * 1024)
      reply->abort();
  });
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, done, generation, googleRequest] {
            if (googleRequest && generation != googleGeneration) {
              reply->deleteLater();
              return;
            }
            auto data = reply->readAll();
            QString error;
            if (reply->error() != QNetworkReply::NoError)
              error = googleRequest
                          ? googleApiError(
                                data,
                                reply->attribute(
                                         QNetworkRequest::HttpStatusCodeAttribute)
                                    .toInt())
                          : "Calendar HTTP request failed (status " +
                                QString::number(reply->attribute(
                                                         QNetworkRequest::HttpStatusCodeAttribute)
                                                    .toInt()) +
                                ")";
            if (data.size() > 8 * 1024 * 1024)
              error = "Calendar response exceeds limit";
            reply->deleteLater();
            done(data, error);
          });
}
void Providers::token(const QByteArray &body,
                      std::function<void(QString)> done) {
  const auto generation = googleGeneration;
  QNetworkRequest req(QUrl("https://oauth2.googleapis.com/token"));
  req.setHeader(QNetworkRequest::ContentTypeHeader,
                "application/x-www-form-urlencoded");
  req.setTransferTimeout(30000);
  auto *reply = network.post(req, body);
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, done, generation] {
    if (generation != googleGeneration) {
      reply->deleteLater();
      return;
    }
    const auto data = reply->readAll();
    const auto networkError = reply->error();
    const auto status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
    auto o = QJsonDocument::fromJson(data).object();
    bool ok = networkError == QNetworkReply::NoError &&
              !o["access_token"].toString().isEmpty();
    if (!ok) {
      if (o["error"].toString() == "invalid_grant") {
        try { secrets.remove("google-refresh"); } catch (const std::exception &) {}
        access.clear();
        accessExpires = 0;
        emit changed();
      }
      auto oauthError = o["error"].toString();
      if (!QRegularExpression("^[A-Za-z0-9_.-]{1,64}$")
               .match(oauthError)
               .hasMatch())
        oauthError.clear();
      auto oauthDescription = o["error_description"].toString().simplified();
      oauthDescription.remove(QRegularExpression("[\\x00-\\x1F\\x7F]"));
      if (oauthDescription.size() > 240)
        oauthDescription = oauthDescription.left(240) + "…";
      QStringList details;
      if (!oauthError.isEmpty())
        details << oauthError;
      if (!oauthDescription.isEmpty())
        details << oauthDescription;
      if (status > 0)
        details << "HTTP " + QString::number(status);
      if (details.isEmpty())
        details << "network error " + QString::number(int(networkError));
      const auto detail = details.join(", ");
      store.log("error", "Google OAuth token request failed (" + detail + ")");
      done("Google token request failed (" + detail +
           "); reconnect the account or check OAuth client configuration");
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
    token(formUrlEncoded({
              {"client_id", c["client_id"].toString()},
              {"client_secret", QString::fromUtf8(secrets.read("google-client-secret"))},
              {"refresh_token", QString::fromUtf8(refresh)},
              {"grant_type", "refresh_token"},
          }),
          done);
  } catch (const std::exception &e) {
    done(QString::fromUtf8(e.what()));
  }
}
void Providers::configureGoogle(QString clientId, QString clientSecret) {
  clientId = clientId.trimmed();
  clientSecret = clientSecret.trimmed();
  if (!clientId.isEmpty() &&
      !QRegularExpression("^[A-Za-z0-9_.-]+\\.apps\\.googleusercontent\\.com$")
           .match(clientId)
           .hasMatch())
    throw Error("Enter a valid Google Desktop OAuth client ID");
  if (!clientSecret.isEmpty() &&
      (!QRegularExpression("^[A-Za-z0-9_.-]{1,512}$")
            .match(clientSecret)
            .hasMatch()))
    throw Error("Enter a valid Google OAuth client secret");
  const auto oldClientId = googleClient()["client_id"].toString();
  const auto oldClientSecret = secrets.read("google-client-secret");
  if (!clientId.isEmpty() && clientId != oldClientId && clientSecret.isEmpty())
    throw Error("Enter the client secret supplied with this Desktop OAuth client");
  if (clientId == oldClientId &&
      (clientSecret.isEmpty() || clientSecret.toUtf8() == oldClientSecret))
    return;
  disconnectGoogle();
  if (clientId.isEmpty())
    secrets.remove("google-client-secret");
  else if (!clientSecret.isEmpty())
    secrets.write("google-client-secret", clientSecret.toUtf8());
  store.config("google",
               clientId.isEmpty() ? QJsonObject{}
                                  : QJsonObject{{"client_id", clientId}});
  store.log("info", clientId.isEmpty()
                        ? "Google OAuth client ID removed"
                        : "Google OAuth client ID updated");
  emit changed();
}
void Providers::connectGoogle() {
  auto c = googleClient();
  if (c["client_id"].toString().isEmpty())
    throw Error("Enter your Google Desktop OAuth client ID in Settings first");
  if (secrets.read("google-client-secret").isEmpty())
    throw Error("Enter the client secret supplied with your Google Desktop OAuth client");
  if (connecting) return;
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
    auto *reply = network.post(
        req, "token=" + QUrl::toPercentEncoding(QString::fromUtf8(refresh)));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
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

QJsonObject Providers::odooStatus() const {
  auto result = store.config("odoo");
  bool keyConfigured = false;
  try { keyConfigured = !secrets.read("odoo-api-key").isEmpty(); }
  catch (const std::exception &) { result["storage_error"] = true; }
  result["api_key_configured"] = keyConfigured;
  result["configured"] = !result["url"].toString().isEmpty() &&
                         !result["database"].toString().isEmpty() &&
                         keyConfigured &&
                         (result["protocol"].toString("jsonrpc") == "json2" ||
                          !result["login"].toString().isEmpty());
  return result;
}

void Providers::configureOdoo(QJsonObject configuration, QString apiKey) {
  auto urlText = configuration["url"].toString().trimmed();
  while (urlText.endsWith('/'))
    urlText.chop(1);
  const QUrl url(urlText);
  if (!urlText.isEmpty() &&
      (url.scheme() != "https" || url.host().isEmpty() ||
       !url.userInfo().isEmpty() || !url.query().isEmpty() ||
       !url.fragment().isEmpty()))
    throw Error("Odoo URL must be an HTTPS origin without credentials, query or fragment");
  const auto protocol = configuration["protocol"].toString("jsonrpc");
  if (!QStringList{"jsonrpc", "json2"}.contains(protocol))
    throw Error("Unsupported Odoo API protocol");
  configuration["url"] = urlText;
  configuration["database"] = configuration["database"].toString().trimmed();
  configuration["login"] = configuration["login"].toString().trimmed();
  configuration["protocol"] = protocol;
  configuration["enabled"] = configuration["enabled"].toBool(true);
  configuration["refresh_minutes"] =
      qBound(1, configuration["refresh_minutes"].toInt(15), 1440);
  apiKey = apiKey.trimmed();
  if (!apiKey.isEmpty() && apiKey.size() > 2048)
    throw Error("Odoo API key is too long");
  const auto old = store.config("odoo");
  const auto oldApiKey = secrets.read("odoo-api-key");
  const bool identityChanged = old["url"] != configuration["url"] ||
                               old["database"] != configuration["database"] ||
                               old["login"] != configuration["login"] ||
                               old["protocol"] != configuration["protocol"];
  const bool credentialsChanged = identityChanged ||
                                  (!apiKey.isEmpty() &&
                                   apiKey.toUtf8() != oldApiKey);
  if (!urlText.isEmpty() && configuration["database"].toString().isEmpty())
    throw Error("Enter the Odoo database name");
  if (!urlText.isEmpty() && protocol == "jsonrpc" &&
      configuration["login"].toString().isEmpty())
    throw Error("Enter the Odoo user login");
  if (!urlText.isEmpty() &&
      (identityChanged || oldApiKey.isEmpty()) &&
      apiKey.isEmpty())
    throw Error("Enter an Odoo user API key");
  ++odooGeneration;
  odooUid = 0;
  busy.remove("odoo-events");
  last.remove("odoo-events");
  if (urlText.isEmpty()) {
    secrets.remove("odoo-api-key");
    store.config("odoo", {});
  } else {
    if (!apiKey.isEmpty())
      secrets.write("odoo-api-key", apiKey.toUtf8());
    store.config("odoo", configuration);
  }
  auto calendars = store.config("calendars");
  QJsonArray items;
  bool found = false;
  for (const auto value : calendars["items"].toArray()) {
    auto item = value.toObject();
    if (item["id"].toString() == "odoo-events") {
      found = true;
      if (urlText.isEmpty())
        continue;
      item["enabled"] = !urlText.isEmpty() && configuration["enabled"].toBool(true);
      item["name"] = "Odoo Events";
      item["kind"] = "odoo";
      item["location"] = urlText;
      item["refresh_minutes"] = configuration["refresh_minutes"].toInt(15);
    }
    items.append(item);
  }
  if (!found && !urlText.isEmpty())
    items.append(QJsonObject{{"id", "odoo-events"},
                             {"enabled", configuration["enabled"].toBool(true)},
                             {"name", "Odoo Events"}, {"kind", "odoo"},
                             {"location", urlText},
                             {"refresh_minutes", configuration["refresh_minutes"].toInt(15)},
                             {"timezone", "UTC"}});
  calendars["items"] = items;
  store.config("calendars", calendars);
  if (credentialsChanged) {
    store.replaceCalendar("odoo-events", {});
    store.clearIgnored("odoo-events");
  }
  store.log("info", urlText.isEmpty() ? "Odoo connection removed"
                                       : "Odoo connection updated");
  emit changed();
}

void Providers::disconnectOdoo() {
  configureOdoo({}, {});
}

void Providers::odooPost(
    const QUrl &url, const QJsonObject &body,
    const QList<QPair<QByteArray, QByteArray>> &headers,
    std::function<void(QJsonValue, QString)> done) {
  const auto generation = odooGeneration;
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  request.setTransferTimeout(30000);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                       QNetworkRequest::SameOriginRedirectPolicy);
  for (const auto &header : headers)
    request.setRawHeader(header.first, header.second);
  auto *reply = network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
  connect(reply, &QNetworkReply::readyRead, reply, [reply] {
    if (reply->bytesAvailable() > 8 * 1024 * 1024)
      reply->abort();
  });
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, done, generation] {
    if (generation != odooGeneration) {
      reply->deleteLater();
      return;
    }
    const auto data = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto networkError = reply->error();
    const auto networkMessage = reply->errorString();
    reply->deleteLater();
    auto sanitized = [this](QString message) {
      try {
        const auto key = QString::fromUtf8(secrets.read("odoo-api-key"));
        if (!key.isEmpty())
          message.replace(key, "[redacted]");
      } catch (const std::exception &) {
      }
      message = message.simplified();
      message.remove(QRegularExpression("[\\x00-\\x1F\\x7F]"));
      return message.left(300);
    };
    if (data.size() > 8 * 1024 * 1024) {
      done({}, "Odoo response exceeds limit");
      return;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(data, &parseError);
    if (networkError != QNetworkReply::NoError || parseError.error != QJsonParseError::NoError) {
      QString message;
      if (document.isObject()) {
        const auto error = document.object()["error"].toObject();
        message = error["message"].toString();
        if (message.isEmpty())
          message = error["data"].toObject()["message"].toString();
      }
      if (message.isEmpty())
        message = networkError == QNetworkReply::NoError
                      ? "invalid JSON response"
                      : networkMessage;
      message = sanitized(message);
      const auto statusText =
          status > 0 ? "HTTP " + QString::number(status)
                     : "network error " + QString::number(int(networkError));
      done({}, "Odoo request failed (" + statusText +
                   (message.isEmpty() ? QString() : "; " + message) + ")");
      return;
    }
    if (document.isArray()) {
      done(document.array(), {});
      return;
    }
    if (!document.isObject()) {
      done({}, "Odoo returned an invalid JSON response");
      return;
    }
    const auto object = document.object();
    if (object.contains("error")) {
      auto message = object["error"].toObject()["data"].toObject()["message"].toString();
      if (message.isEmpty())
        message = object["error"].toObject()["message"].toString();
      done({}, "Odoo request failed: " + sanitized(message));
      return;
    }
    done(object.contains("result") ? object["result"] : QJsonValue(object), {});
  });
}

void Providers::odooAuthenticate(std::function<void(QString)> done) {
  if (odooUid > 0) {
    done({});
    return;
  }
  const auto config = store.config("odoo");
  if (config["protocol"].toString("jsonrpc") == "json2") {
    done({});
    return;
  }
  QUrl endpoint(config["url"].toString());
  endpoint.setPath(endpoint.path() + "/jsonrpc");
  const auto key = QString::fromUtf8(secrets.read("odoo-api-key"));
  QJsonObject body{{"jsonrpc", "2.0"}, {"method", "call"}, {"id", 1},
                   {"params", QJsonObject{{"service", "common"},
                                           {"method", "authenticate"},
                                           {"args", QJsonArray{config["database"], config["login"], key, QJsonObject{}}}}}};
  odooPost(endpoint, body, {}, [this, done](QJsonValue result, QString error) {
    if (error.isEmpty()) {
      odooUid = result.toInt();
      if (odooUid <= 0)
        error = "Odoo rejected the database, login or API key";
    }
    done(error);
  });
}

void Providers::odooCall(const QString &model, const QString &method,
                         const QJsonArray &args, const QJsonObject &kwargs,
                         std::function<void(QJsonValue, QString)> done) {
  odooAuthenticate([this, model, method, args, kwargs, done](QString error) {
    if (!error.isEmpty()) {
      done({}, error);
      return;
    }
    try {
      const auto config = store.config("odoo");
      const auto key = secrets.read("odoo-api-key");
      QUrl endpoint(config["url"].toString());
      QList<QPair<QByteArray, QByteArray>> headers;
      QJsonObject body;
      if (config["protocol"].toString("jsonrpc") == "json2") {
        endpoint.setPath(endpoint.path() + "/json/2/" + model + "/" + method);
        headers.append({"Authorization", "bearer " + key});
        headers.append(
            {"X-Odoo-Database", config["database"].toString().toUtf8()});
        body = kwargs;
        if (!args.isEmpty()) {
          if (method == "search_read")
            body["domain"] = args.first();
          else
            body["ids"] = args.first();
        }
      } else {
        endpoint.setPath(endpoint.path() + "/jsonrpc");
        QJsonArray callArgs{config["database"], odooUid,
                            QString::fromUtf8(key), model, method, args,
                            kwargs};
        body = {{"jsonrpc", "2.0"}, {"method", "call"}, {"id", 1},
                {"params", QJsonObject{{"service", "object"},
                                        {"method", "execute_kw"},
                                        {"args", callArgs}}}};
      }
      odooPost(endpoint, body, headers, done);
    } catch (const std::exception &exception) {
      done({}, QString::fromUtf8(exception.what()));
    }
  });
}

void Providers::listOdoo() {
  auto options = std::make_shared<QJsonObject>();
  auto fields = QJsonArray{"id", "name"};
  QJsonArray allRecords;
  allRecords.append(QJsonArray{});
  odooCall("event.type", "search_read", allRecords,
           {{"fields", fields}, {"limit", 1000}, {"order", "name"}},
           [this, options, fields, allRecords](QJsonValue result, QString error) {
    if (!error.isEmpty()) {
      emit problem(error);
      return;
    }
    (*options)["types"] = result.toArray();
    odooCall("event.stage", "search_read", allRecords,
             {{"fields", fields}, {"limit", 1000}, {"order", "sequence,id"}},
             [this, options](QJsonValue stages, QString stageError) {
      if (!stageError.isEmpty()) {
        emit problem(stageError);
        return;
      }
      (*options)["stages"] = stages.toArray();
      emit odooOptions(*options);
    });
  });
}

void Providers::odooPage(QJsonObject calendar, int offset, QList<Event> events,
                         qint64 windowStart) {
  const auto config = store.config("odoo");
  if (!windowStart)
    windowStart = now();
  QJsonArray domain{
      QJsonArray{"active", "=", true},
      QJsonArray{"date_end", ">=", QDateTime::fromMSecsSinceEpoch(windowStart, Qt::UTC).toString("yyyy-MM-dd HH:mm:ss")},
      QJsonArray{"date_begin", "<=", QDateTime::fromMSecsSinceEpoch(windowStart + providerWindow, Qt::UTC).toString("yyyy-MM-dd HH:mm:ss")}};
  const bool filtersInitialized = config["filters_initialized"].toBool();
  for (const auto pair : {qMakePair(QString("event_type_id"), config["type_ids"].toArray()),
                          qMakePair(QString("stage_id"), config["stage_ids"].toArray()),
                          qMakePair(QString("kanban_state"), config["kanban_states"].toArray())}) {
    if (!pair.second.isEmpty())
      domain.append(QJsonArray{pair.first, "in", pair.second});
    else if (filtersInitialized)
      domain.append(QJsonArray{"id", "=", false});
  }
  const QJsonArray fields{"id", "name", "description", "date_begin", "date_end",
                          "date_tz", "event_type_id", "stage_id", "kanban_state", "write_date"};
  QJsonArray arguments;
  arguments.append(domain);
  odooCall("event.event", "search_read", arguments,
           {{"fields", fields}, {"limit", 500}, {"offset", offset},
            {"order", "date_begin,id"}},
           [this, calendar, offset, events, windowStart](QJsonValue result, QString error) mutable {
    if (!error.isEmpty()) {
      complete(calendar, {}, error);
      return;
    }
    const auto rows = result.toArray();
    try {
      for (const auto value : rows) {
        const auto row = value.toObject();
        Event event;
        event.calendar = calendar["id"].toString();
        event.externalId = "event.event/" + QString::number(row["id"].toInt());
        event.id = QString::fromLatin1(QCryptographicHash::hash(
            (event.calendar + "\n" + event.externalId).toUtf8(),
            QCryptographicHash::Sha256).toHex());
        event.title = row["name"].toString("(Untitled)");
        event.description = row["description"].toString();
        auto parseOdooDate = [](const QString &text) {
          const auto date = QDateTime::fromString(
              text + "Z", "yyyy-MM-dd HH:mm:ss'Z'");
          if (!date.isValid())
            throw Error("Odoo returned an invalid event date");
          return QDateTime(date.date(), date.time(), QTimeZone::UTC)
              .toMSecsSinceEpoch();
        };
        event.start = parseOdooDate(row["date_begin"].toString());
        event.end = parseOdooDate(row["date_end"].toString());
        event.timezone = row["date_tz"].toString("UTC");
        if (!QTimeZone(event.timezone.toUtf8()).isValid())
          event.timezone = "UTC";
        event.source = "OdooEvent";
        event.enabled = true;
        event.updated = now();
        event.metadata = {{"odoo_id", row["id"]},
                          {"event_type", row["event_type_id"]},
                          {"stage", row["stage_id"]},
                          {"kanban_state", row["kanban_state"]},
                          {"write_date", row["write_date"]}};
        event.validate();
        events.append(event);
      }
    } catch (const std::exception &exception) {
      complete(calendar, {}, QString::fromUtf8(exception.what()));
      return;
    }
    if (rows.size() == 500 && events.size() < 5000)
      odooPage(calendar, offset + 500, events, windowStart);
    else
      complete(calendar, events, {});
  });
}
void Providers::complete(const QJsonObject &cal, const QList<Event> &events,
                         const QString &err) {
  busy.remove(cal["id"].toString());
  QString error = err;
  try {
    auto current = store.config("calendars")["items"].toArray();
    bool matches = false;
    for (auto v : current) {
      const auto saved = v.toObject();
      if (saved["id"] == cal["id"] && saved["kind"] == cal["kind"] &&
          saved["location"] == cal["location"] &&
          saved["enabled"].toBool(true))
        matches = true;
    }
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
  const auto calendars = store.config("calendars")["items"].toArray();
  bool googleBusy = false;
  for (auto v : calendars) {
    const auto calendar = v.toObject();
    if (calendar["kind"].toString() == "google" &&
        busy.contains(calendar["id"].toString())) {
      googleBusy = true;
      break;
    }
  }
  for (auto v : calendars) {
    auto c = v.toObject();
    auto id = c["id"].toString();
    auto kind = c["kind"].toString();
    if (!c["enabled"].toBool(true) || busy.contains(id) ||
        (kind == "google" && googleBusy) ||
        (!force && now() - last.value(id) <
                       qMax(1, c["refresh_minutes"].toInt(15)) * 60000LL))
      continue;
    busy.insert(id);
    last[id] = now();
    if (kind == "google") {
      googleBusy = true;
      authorized([this, c](QString err) {
        if (!err.isEmpty())
          complete(c, {}, err);
        else
          googlePage(c, {}, {});
      });
    } else if (kind == "odoo") {
      if (!odooStatus()["configured"].toBool())
        complete(c, {}, "Configure the Odoo connection first");
      else
        odooPage(c, 0, {});
    } else if (kind == "file") {
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
                           int pages, qint64 windowStart) {
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
  // Large pages can stall in Qt's Windows network stack even for calendars
  // with few events. Pagination below preserves the overall result limit.
  q.addQueryItem("maxResults", "250");
  if (!windowStart)
    windowStart = now();
  q.addQueryItem("timeMin", iso(windowStart));
  q.addQueryItem("timeMax", iso(windowStart + providerWindow));
  if (!page.isEmpty())
    q.addQueryItem("pageToken", page);
  url.setQuery(q);
  request(
      url, access,
      [this, cal, events, pages, windowStart](QByteArray data, QString err) mutable {
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
            googlePage(cal, next, events, pages + 1, windowStart);
          else
            complete(cal, events, {});
        } catch (const std::exception &e) {
          complete(cal, {}, QString::fromUtf8(e.what()));
        }
      });
}
} // namespace bs
