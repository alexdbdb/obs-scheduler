#include "runtime.hpp"
#include "calendar.hpp"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonDocument>
namespace bs {
void Runtime::migrateRecordingSchedule() {
  if (store->config("recording_schedule")["version"].toInt() == 1)
    return;
  // Keep old templates/configuration as an archive. They can never dispatch actions.
  store->sql("BEGIN IMMEDIATE");
  try {
    store->run("INSERT OR IGNORE INTO executions "
      "SELECT event_id || '/' || action,event_id,title,action,scheduled,actual,result,message "
      "FROM executions WHERE action IN ('record.start','record.stop') ORDER BY actual DESC");
    if (!store->events().isEmpty()) {
      auto settings = store->config("settings");
      settings["enabled"] = false;
      store->config("settings", settings);
      store->log("info", "Existing schedule preserved and paused for review: every event now records from start to end");
    }
    store->config("recording_schedule", {{"version", 1}});
    store->sql("COMMIT");
  } catch (...) {
    store->sql("ROLLBACK");
    throw;
  }
}
void Runtime::start() {
  try {
    store = std::make_unique<Store>(path);
    secrets = std::make_unique<Secrets>(QFileInfo(path).absolutePath());
    auto settings = store->config("settings");
    if (!settings.contains("enabled")) settings["enabled"] = true;
    settings["timezone"] = deviceZone();
    settings["record_stop"] = "exact";
    settings["record_existing"] = "leave";
    settings["allow_unowned_stop"] = false;
    settings["advanced_actions"] = false;
    // Migrate previously network-exposed configurations to the local API.
    settings["api_host"] = "127.0.0.1";
    settings.remove("api_network_acknowledged");
    store->config("settings", settings);
    migrateRecordingSchedule();
    scheduler = std::make_unique<Scheduler>(*store);
    scheduler->execute = [this](const Due &d) {
      return action ? action(d, store->config("settings"))
                    : Outcome{"failed", "OBS unavailable"};
    };
    providers = new Providers(*store, *secrets, this);
    api = new Api(this);
    connect(providers, &Providers::problem, this, &Runtime::problem);
    connect(providers, &Providers::openUrl, this, &Runtime::openUrl);
    connect(providers, &Providers::googleCalendars, this,
            &Runtime::googleCalendars);
    connect(providers, &Providers::odooOptions, this,
            &Runtime::odooOptions);
    connect(providers, &Providers::changed, this, &Runtime::snapshot);
    api->call = [this](QString op, QJsonObject data) {
      return request(op, data);
    };
    try {
      api->configure(store->config("settings"));
    } catch (const std::exception &e) {
      emit problem(QString::fromUtf8(e.what()));
    }
    expandRecurrences();
    // Refresh remote calendars once per OBS session immediately, then let
    // each calendar's configured interval govern subsequent rolling updates.
    providers->sync(true);
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
        engineError = QString::fromUtf8(e.what());
        emit problem("Scheduler stopped after internal error: " + engineError);
      }
    });
    timer->start();
    store->log("info", "Broadcast Scheduler " PLUGIN_VERSION " started");
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
  QJsonArray events;
  for (auto &e : store->events())
    events.append(e.json());
  auto settings = store->config("settings");
  settings.remove("api_token_hash");
  settings["timezone"] = deviceZone();
  QJsonObject data{
      {"events", events},
      {"settings", settings},
      {"calendars", store->config("calendars")},
      {"recurrences", store->config("recurrences")},
      {"google", providers ? providers->googleStatus() : QJsonObject{}},
      {"odoo", providers ? providers->odooStatus() : QJsonObject{}},
      {"history", store->history()},
      {"excluded", store->query("SELECT calendar,external_id,created FROM ignored_events ORDER BY created DESC")},
      {"logs", store->query("SELECT * FROM logs ORDER BY id DESC LIMIT 500")},
      {"database", path},
      {"api_running", api && api->running()},
      {"engine_running", timer && timer->isActive()},
      {"engine_error", engineError},
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
    auto result = request(op, data);
    if (result.value("_status").toInt(200) >= 400)
      throw Error(result.value("error").toString());
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
    return {{"version", PLUGIN_VERSION},
            {"scheduler_enabled",
             store->config("settings")["enabled"].toBool(true)},
            {"api_running", api->running()},
            {"engine_running", timer && timer->isActive()},
            {"engine_error", engineError},
            {"device_timezone", deviceZone()},
            {"obs", obsStatus ? obsStatus() : QJsonObject{}}};
  if (op == "GET /api/v1/events") {
    QJsonArray a;
    for (auto &e : store->events())
      a.append(e.json());
    return {{"events", a}};
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
    const bool writable = e.source == "Manual" || e.source == "API";
    const bool imported = !e.calendar.isEmpty() && !e.externalId.isEmpty();
    if (prefix == "DELETE") {
      if (writable) {
        store->erase(e.id);
        store->log("info", "Event deleted: " + e.id);
        return {{"deleted", e.id}};
      }
      if (imported) {
        store->ignoreExternal(e);
        store->log("info", "External event excluded locally: " + e.id);
        return {{"deleted", e.id}, {"local_only", true}};
      }
      return failure(
          409,
          "Recurring events are read-only; edit their recurrence rule");
    }
    if (!writable)
      return failure(409, "External and recurring events are read-only; edit "
                          "their source");
  }
  if (op == "event.save" || op == "POST /api/v1/events" || prefix == "PUT") {
    data["source"] = op == "event.save" ? "Manual" : "API";
    data["source_calendar"] = "";
    data["external_id"] = "";
    if (op == "POST /api/v1/events")
      data["id"] = uid();
    data["timezone"] = deviceZone();
    data.remove("template");
    data["start"] = iso(deviceInstant(data.value("start").toString()).toMSecsSinceEpoch());
    data["end"] = iso(deviceInstant(data.value("end").toString()).toMSecsSinceEpoch());
    auto e = Event::parse(data);
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
    if (e.source == "Manual" || e.source == "API") {
      store->erase(e.id);
      store->log("info", "Event deleted: " + e.id);
    } else if (!e.calendar.isEmpty() && !e.externalId.isEmpty()) {
      store->ignoreExternal(e);
      store->log("info", "External event excluded locally: " + e.id);
    } else {
      throw Error("Recurring events are read-only");
    }
    return {};
  }
  if (op == "events.restore") {
    for (auto value : data["items"].toArray()) {
      auto item = value.toObject();
      store->restoreExternal(item["calendar"].toString(), item["external_id"].toString());
    }
    providers->sync(true);
    return {};
  }
  if (op == "settings.save") {
    auto old = store->config("settings");
    for (auto i = data.begin(); i != data.end(); ++i)
      old[i.key()] = i.value();
    old["timezone"] = deviceZone();
    old["record_stop"] = "exact";
    old["record_existing"] = "leave";
    old["allow_unowned_stop"] = false;
    old["advanced_actions"] = false;
    api->configure(old);
    store->config("settings", old);
    if (old["enabled"].toBool(true) && timer && !timer->isActive()) {
      expandRecurrences();
      engineError.clear();
      timer->start();
    }
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
    QJsonArray normalized;
    for (auto v : data.value("items").toArray()) {
      auto c = v.toObject();
      c["timezone"] = deviceZone();
      c.remove("template");
      c["start_offset"] = 0;
      c["end_offset"] = 0;
      normalized.append(c);
    }
    data["items"] = normalized;
    QSet<QString> ids;
    int odooCalendars = 0;
    for (auto v : data["items"].toArray()) {
      auto c = v.toObject();
      auto id = c["id"].toString();
      if (id.isEmpty() || ids.contains(id))
        throw Error("Calendar IDs must be unique");
      ids.insert(id);
      if (!QStringList{"ics", "file", "google", "odoo"}.contains(c["kind"].toString()))
        throw Error("Invalid calendar provider");
      if (c["kind"].toString() == "odoo" &&
          (++odooCalendars > 1 || id != "odoo-events"))
        throw Error("The Odoo calendar is managed in Settings");
      if (!QTimeZone(c["timezone"].toString("UTC").toUtf8()).isValid())
        throw Error("Unknown calendar timezone");
      if (c["kind"].toString() == "ics" &&
          QUrl(c["location"].toString()).scheme() != "https")
        throw Error("ICS URL must use HTTPS");
    }
    auto old = store->config("calendars");
    store->config("calendars", data);
    for (auto v : old["items"].toArray()) {
      auto oldId = v.toObject()["id"].toString();
      if (!ids.contains(oldId)) {
        store->replaceCalendar(oldId, {});
        store->clearIgnored(oldId);
      }
    }
    providers->sync(true);
    return {};
  }
  if (op == "recurrences.save") {
    QJsonArray normalized;
    for (auto v : data.value("items").toArray()) {
      auto o = v.toObject();
      auto e = o.value("event").toObject();
      e["timezone"] = deviceZone();
      e.remove("template");
      e["start"] = iso(deviceInstant(e.value("start").toString()).toMSecsSinceEpoch());
      e["end"] = iso(deviceInstant(e.value("end").toString()).toMSecsSinceEpoch());
      o["event"] = e;
      normalized.append(o);
    }
    data["items"] = normalized;
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
  if (op == "google.connect") {
    providers->connectGoogle();
    return {};
  }
  if (op == "google.configure") {
    providers->configureGoogle(data["client_id"].toString(),
                               data["client_secret"].toString());
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
  if (op == "odoo.configure") {
    providers->configureOdoo(data["configuration"].toObject(),
                             data["api_key"].toString());
    return {};
  }
  if (op == "odoo.disconnect") {
    providers->disconnectOdoo();
    return {};
  }
  if (op == "odoo.list") {
    providers->listOdoo();
    return {};
  }
  if (op == "sync" || op == "POST /api/v1/sync") {
    providers->sync(true);
    expandRecurrences();
    return {{"_status", 202}, {"result", "Sync requested"}};
  }
  return failure(405, "Method or operation not supported");
}
} // namespace bs
