#include "runtime.hpp"
#include "calendar.hpp"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonDocument>
namespace bs {
void Runtime::seed() {
  if (!store->templates().isEmpty())
    return;
  auto make = [&](QString name, QList<Action> actions) {
    for (auto &a : actions)
      a.id = uid();
    store->put(Template{name, name, actions});
  };
  make("Recording Only", {{{}, "record.start", "start", 0, {}},
                          {{}, "record.stop", "end", 0, {}}});
  make("Concert", {{{}, "scene.current", "start", -600, {{"scene", "GENERAL"}}},
                   {{}, "record.start", "start", -300, {}},
                   {{}, "record.stop", "end", 900, {}}});
  make("Podcast", {{{}, "record.start", "start", 0, {}},
                   {{}, "record.stop", "end", 0, {}}});
  make("Livestream", {{{}, "stream.start", "start", 0, {}},
                      {{}, "stream.stop", "end", 0, {}}});
  make("Church Service", {{{}, "stream.start", "start", -300, {}},
                          {{}, "record.start", "start", 0, {}},
                          {{}, "stream.stop", "end", 0, {}},
                          {{}, "record.stop", "end", 600, {}}});
}
void Runtime::start() {
  try {
    store = std::make_unique<Store>(path);
    secrets = std::make_unique<Secrets>(QFileInfo(path).absolutePath());
    auto settings = store->config("settings");
    if (!settings.contains("enabled")) settings["enabled"] = true;
    if (!settings.contains("timezone")) settings["timezone"] = "UTC";
    if (!settings.contains("missed")) settings["missed"] = "ignore";
    if (!settings.contains("tolerance_seconds")) settings["tolerance_seconds"] = 60;
    if (!settings.contains("record_stop")) settings["record_stop"] = "exact";
    if (!settings.contains("stream_stop")) settings["stream_stop"] = "exact";
    if (!settings.contains("grace_minutes")) settings["grace_minutes"] = 15;
    if (!settings.contains("record_existing")) settings["record_existing"] = "leave";
    if (!settings.contains("stream_existing")) settings["stream_existing"] = "leave";
    store->config("settings", settings);
    seed();
    scheduler = std::make_unique<Scheduler>(*store);
    scheduler->execute = [this](const Due &d) {
      return action ? action(d, store->config("settings"))
                    : Outcome{"failed", "OBS unavailable"};
    };
    scheduler->ask = [this](const Due &d, const QString &kind) {
      emit ask(d.key(), d.event.title, kind, iso(d.time));
    };
    providers = new Providers(*store, *secrets, this);
    api = new Api(this);
    connect(providers, &Providers::problem, this, &Runtime::problem);
    connect(providers, &Providers::openUrl, this, &Runtime::openUrl);
    connect(providers, &Providers::googleCalendars, this,
            &Runtime::googleCalendars);
    api->call = [this](QString op, QJsonObject data) {
      return request(op, data);
    };
    try {
      api->configure(store->config("settings"));
    } catch (const std::exception &e) {
      emit problem(QString::fromUtf8(e.what()));
    }
    expandRecurrences();
    for (auto v : store->query(
             "SELECT key,state FROM deferred WHERE state LIKE 'ask%'")) {
      auto row = v.toObject();
      for (auto &d : scheduler->pending())
        if (d.key() == row["key"].toString())
          emit ask(d.key(), d.event.title,
                   row["state"].toString() == "ask-stop" ? "stop" : "missed",
                   iso(d.time));
    }
    timer = new QTimer(this);
    timer->setTimerType(Qt::PreciseTimer);
    timer->setInterval(250);
    connect(timer, &QTimer::timeout, this, [this] {
      try {
        if (now() - recurrenceRefresh > 3600000)
          expandRecurrences();
        scheduler->tick(now());
        providers->sync(false);
      } catch (const std::exception &e) {
        timer->stop();
        emit problem("Scheduler paused after internal error: " +
                     QString::fromUtf8(e.what()));
      }
    });
    timer->start();
    store->log("info", "Broadcast Scheduler 0.1.0 started");
    snapshot();
  } catch (const std::exception &e) {
    emit problem(QString::fromUtf8(e.what()));
  }
}
void Runtime::shutdown() {
  if (timer)
    timer->stop();
  delete api;
  api = nullptr;
  delete providers;
  providers = nullptr;
  scheduler.reset();
  secrets.reset();
  store.reset();
}
void Runtime::expandRecurrences() {
  for (auto v : store->config("recurrences")["items"].toArray()) {
    auto o = v.toObject();
    auto seed = Event::parse(o["event"].toObject());
    store->replaceCalendar(
        seed.id, CalendarParser::recurrence(seed, o["rrule"].toString(),
                                            now() - 7LL * 86400000,
                                            now() + 400LL * 86400000));
  }
  recurrenceRefresh = now();
}
void Runtime::snapshot() {
  if (!store || !scheduler)
    return;
  QJsonArray events, templates;
  for (auto &e : store->events())
    events.append(e.json());
  for (auto &t : store->templates())
    templates.append(t.json());
  auto settings = store->config("settings");
  settings.remove("api_token_hash");
  QJsonObject data{
      {"events", events},
      {"templates", templates},
      {"settings", settings},
      {"calendars", store->config("calendars")},
      {"recurrences", store->config("recurrences")},
      {"google", store->config("google")},
      {"history", store->history()},
      {"logs", store->query("SELECT * FROM logs ORDER BY id DESC LIMIT 500")},
      {"database", path},
      {"api_running", api && api->running()},
      {"obs", obsStatus ? obsStatus() : QJsonObject{}}};
  auto pending = scheduler->pending();
  if (!pending.isEmpty()) {
    auto d = pending.first();
    data["next"] = QJsonObject{{"title", d.event.title},
                               {"action", d.action.type},
                               {"time", iso(d.time)},
                               {"start", iso(d.event.start)},
                               {"end", iso(d.event.end)}};
  }
  QJsonArray sync;
  for (auto v : store->config("calendars")["items"].toArray()) {
    auto o = v.toObject();
    sync.append(
        QJsonObject{{"name", o["name"]},
                    {"enabled", o["enabled"]},
                    {"state", store->config("sync/" + o["id"].toString())}});
  }
  data["sync"] = sync;
  emit state(data);
}
void Runtime::command(QString op, QJsonObject data) {
  try {
    if (!store)
      throw Error("Database is not available");
    request(op, data);
    snapshot();
  } catch (const std::exception &e) {
    emit problem(QString::fromUtf8(e.what()));
  }
}
QJsonObject Runtime::request(const QString &op, QJsonObject data) {
  if (op == "diagnostic") {
    store->log(data["level"].toString(), data["message"].toString());
    return {};
  }
  if (op == "action.complete") {
    store->finish(data["key"].toString(), data["result"].toString(),
                  data["message"].toString());
    return {};
  }
  auto failure = [](int status, const QString &message) {
    return QJsonObject{{"_status", status}, {"error", message}};
  };
  if (op == "GET /api/v1/status")
    return {{"version", "0.1.0"},
            {"scheduler_enabled",
             store->config("settings")["enabled"].toBool(true)},
            {"api_running", api->running()},
            {"obs", obsStatus ? obsStatus() : QJsonObject{}}};
  if (op == "GET /api/v1/events") {
    QJsonArray a;
    for (auto &e : store->events())
      a.append(e.json());
    return {{"events", a}};
  }
  if (op == "GET /api/v1/templates") {
    QJsonArray a;
    for (auto &t : store->templates())
      a.append(t.json());
    return {{"templates", a}};
  }
  QString prefix;
  for (auto method : {"GET", "PUT", "DELETE"}) {
    QString p = QString(method) + " /api/v1/events/";
    if (op.startsWith(p)) {
      prefix = method;
      data["id"] = op.mid(p.size());
    }
  }
  if (!prefix.isEmpty()) {
    Event e;
    try {
      e = store->event(data["id"].toString());
    } catch (const Error &) {
      return failure(404, "Event not found");
    }
    if (prefix == "GET")
      return e.json();
    if (e.source != "Manual" && e.source != "API")
      return failure(
          409,
          "External and recurring events are read-only; edit their source");
    if (prefix == "DELETE") {
      store->erase(e.id);
      store->log("info", "Event deleted: " + e.id);
      return {{"deleted", e.id}};
    }
  }
  if (op == "event.save" || op == "POST /api/v1/events" || prefix == "PUT") {
    data["source"] = op == "event.save" ? "Manual" : "API";
    data["source_calendar"] = "";
    data["external_id"] = "";
    if (op == "POST /api/v1/events")
      data["id"] = uid();
    auto e = Event::parse(data);
    bool found = false;
    for (auto &t : store->templates())
      if (t.id == e.templateId || t.name == e.templateId) {
        e.templateId = t.id;
        found = true;
        break;
      }
    if (!found)
      throw Error("Unknown template");
    if (op == "event.save") {
      auto old = store->query("SELECT id FROM events WHERE id=?", {e.id});
      if (!old.isEmpty()) {
        auto previous = store->event(e.id);
        if (previous.source != "Manual" && previous.source != "API")
          throw Error("External events are read-only");
      }
    }
    store->put(e);
    store->log("info", "Event saved: " + e.id);
    auto out = e.json();
    out["_status"] = op == "POST /api/v1/events" ? 201 : 200;
    return out;
  }
  if (op == "event.delete") {
    auto e = store->event(data["id"].toString());
    if (e.source != "Manual" && e.source != "API")
      throw Error("External events are read-only");
    store->erase(e.id);
    store->log("info", "Event deleted: " + e.id);
    return {};
  }
  if (op == "template.save") {
    auto t = Template::parse(data);
    store->put(t);
    store->log("info", "Template saved: " + t.id);
    return t.json();
  }
  if (op == "settings.save") {
    auto old = store->config("settings");
    for (auto i = data.begin(); i != data.end(); ++i)
      old[i.key()] = i.value();
    if (!QTimeZone(old["timezone"].toString("UTC").toUtf8()).isValid())
      throw Error("Invalid timezone");
    api->configure(old);
    store->config("settings", old);
    return {};
  }
  if (op == "token.generate") {
    auto token = Secrets::random();
    auto s = store->config("settings");
    s["api_token_hash"] = QString::fromLatin1(
        QCryptographicHash::hash(token, QCryptographicHash::Sha256).toHex());
    store->config("settings", s);
    api->configure(s);
    emit tokenGenerated(QString::fromUtf8(token));
    return {};
  }
  if (op == "calendars.save") {
    QSet<QString> ids;
    for (auto v : data["items"].toArray()) {
      auto c = v.toObject();
      auto id = c["id"].toString();
      if (id.isEmpty() || ids.contains(id))
        throw Error("Calendar IDs must be unique");
      ids.insert(id);
      if (!QStringList{"ics", "file", "google"}.contains(c["kind"].toString()))
        throw Error("Invalid calendar provider");
      if (!QTimeZone(c["timezone"].toString("UTC").toUtf8()).isValid())
        throw Error("Unknown calendar timezone");
      if (c["kind"].toString() == "ics" &&
          QUrl(c["location"].toString()).scheme() != "https")
        throw Error("ICS URL must use HTTPS");
    }
    auto old = store->config("calendars");
    store->config("calendars", data);
    for (auto v : old["items"].toArray())
      if (!ids.contains(v.toObject()["id"].toString()))
        store->replaceCalendar(v.toObject()["id"].toString(), {});
    providers->sync(true);
    return {};
  }
  if (op == "recurrences.save") {
    QSet<QString> ids;
    QList<QPair<QString, QList<Event>>> expanded;
    for (auto v : data["items"].toArray()) {
      auto o = v.toObject();
      auto e = Event::parse(o["event"].toObject());
      if (ids.contains(e.id))
        throw Error("Duplicate recurrence ID");
      ids.insert(e.id);
      expanded.append(
          {e.id, CalendarParser::recurrence(e, o["rrule"].toString(),
                                            now() - 7LL * 86400000,
                                            now() + 400LL * 86400000)});
    }
    auto old = store->config("recurrences");
    store->config("recurrences", data);
    for (auto v : old["items"].toArray()) {
      auto id = v.toObject()["event"].toObject()["id"].toString();
      if (!ids.contains(id))
        store->replaceCalendar(id, {});
    }
    for (auto &p : expanded)
      store->replaceCalendar(p.first, p.second);
    return {};
  }
  if (op == "google.save") {
    auto secret = data.take("client_secret").toString();
    if (!secret.isEmpty())
      secrets->write("google-client-secret", secret.toUtf8());
    store->config("google", data);
    return {};
  }
  if (op == "google.connect") {
    providers->connectGoogle();
    return {};
  }
  if (op == "google.disconnect") {
    providers->disconnectGoogle();
    return {};
  }
  if (op == "google.list") {
    providers->listGoogle();
    return {};
  }
  if (op == "sync" || op == "POST /api/v1/sync") {
    providers->sync(true);
    expandRecurrences();
    return {{"_status", 202}, {"result", "Sync requested"}};
  }
  if (op == "answer") {
    scheduler->answer(data["key"].toString(), data["minutes"].toInt());
    return {};
  }
  if (op == "action" || op.startsWith("POST /api/v1/actions/")) {
    QString type = data["type"].toString();
    if (op != "action")
      type = op.mid(QString("POST /api/v1/actions/").size()).replace('/', '.');
    if (!QStringList{"record.start", "record.stop", "stream.start",
                     "stream.stop"}
             .contains(type))
      return failure(404, "Unknown action");
    Due d;
    d.event.id = "operator";
    d.event.title = "Operator";
    d.action = Action::parse({{"type", type}});
    d.time = now();
    auto result = scheduler->execute(d);
    store->log(result.result, "Operator " + type + " " + result.message);
    return {{"result", result.result}, {"message", result.message}};
  }
  return failure(405, "Method or operation not supported");
}
} // namespace bs
