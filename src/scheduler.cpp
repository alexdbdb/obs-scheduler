#include "scheduler.hpp"
namespace bs {
QList<Due> Scheduler::pending() const {
  if (revision != store.revision()) {
    cached = plan(store.events(), store.templates());
    revision = store.revision();
  }
  QSet<QString> finished;
  for (auto v : store.query("SELECT key FROM executions"))
    finished.insert(v.toObject()["key"].toString());
  QList<Due> r;
  auto sources = store.config("calendars")["items"].toArray();
  QHash<QString, bool> enabled;
  for (auto s : sources)
    enabled[s.toObject()["id"].toString()] =
        s.toObject()["enabled"].toBool(true);
  for (auto &d : cached)
    if (enabled.value(d.event.calendar, true) && !finished.contains(d.key()))
      r.append(d);
  return r;
}
void Scheduler::tick(qint64 time) {
  auto cfg = store.config("settings");
  if (!cfg["enabled"].toBool(true))
    return;
  const qint64 tolerance = qMax(1, cfg["tolerance_seconds"].toInt(60)) * 1000LL;
  for (auto d : pending()) {
    if (d.time > time + 300000)
      break;
    auto deferred =
        store.query("SELECT * FROM deferred WHERE key=?", {d.key()});
    QString state;
    qint64 target = d.time;
    if (!deferred.isEmpty()) {
      auto row = deferred.first().toObject();
      state = row["state"].toString();
      target = qint64(row["due"].toDouble());
      if (state.startsWith("ask"))
        continue;
    }
    bool stop =
        d.action.type == "record.stop" || d.action.type == "stream.stop";
    QString policy =
        cfg[d.action.type.startsWith("record") ? "record_stop" : "stream_stop"]
            .toString("exact");
    if (stop && policy == "ask" && state.isEmpty() && d.time - time <= 300000 &&
        d.time >= time - tolerance) {
      store.defer(d, d.time, "ask-stop");
      if (ask)
        ask(d, "stop");
      continue;
    }
    if (target > time)
      continue;
    if (state.isEmpty() && time - target > tolerance) {
      auto missed = cfg["missed"].toString("ignore");
      if (missed == "ignore") {
        if (store.claim(d))
          store.finish(d.key(), "skipped", "Missed action outside tolerance");
        continue;
      }
      if (missed == "ask") {
        store.defer(d, d.time, "ask-missed");
        if (ask)
          ask(d, "missed");
        continue;
      }
      // Immediate catch-up is bounded: never start an event that has already
      // ended.
      if (d.event.end < time && (d.action.type == "record.start" ||
                                 d.action.type == "stream.start")) {
        if (store.claim(d))
          store.finish(d.key(), "skipped", "Event has already ended");
        continue;
      }
    }
    if (stop && policy == "grace" && state.isEmpty()) {
      auto until = d.time + qMax(0, cfg["grace_minutes"].toInt(15)) * 60000LL;
      store.defer(d, until, "grace");
      if (until > time)
        continue;
    }
    if (stop && policy == "ask" && state.isEmpty()) {
      store.defer(d, time, "ask-stop");
      if (ask)
        ask(d, "stop");
      continue;
    }
    if (!store.claim(d))
      continue;
    try {
      auto result = execute ? execute(d)
                            : Outcome{"failed", "Action adapter unavailable"};
      store.finish(d.key(), result.result, result.message);
      store.log(result.result,
                d.event.id + " " + d.action.type + " " + result.message);
    } catch (const std::exception &) {
      store.finish(d.key(), "failed",
                   "Action failed; inspect plugin diagnostics");
    }
  }
}
void Scheduler::answer(const QString &key, int minutes) {
  for (auto d : pending())
    if (d.key() == key) {
      if (minutes < 0) {
        if (store.claim(d))
          store.finish(key, "cancelled", "Cancelled by operator");
      } else
        store.defer(d, qMax(now(), d.time) + minutes * 60000LL, "approved");
      return;
    }
}
} // namespace bs
