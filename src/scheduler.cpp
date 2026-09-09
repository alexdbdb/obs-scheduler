#include "scheduler.hpp"
#include <QJsonDocument>
#include <algorithm>
namespace bs {
QList<Due> Scheduler::pending() const {
  if (revision != store.revision()) {
    cached = plan(store.events());
    revision = store.revision();
  }
  QSet<QString> finished;
  for (auto v : store.query("SELECT key FROM executions"))
    finished.insert(v.toObject()["key"].toString());
  QHash<QString, bool> enabled;
  for (auto v : store.config("calendars")["items"].toArray())
    enabled[v.toObject()["id"].toString()] = v.toObject()["enabled"].toBool(true);
  QHash<QString, Due> due;
  for (const auto &d : cached)
    if (enabled.value(d.event.calendar, true) && !finished.contains(d.key()))
      due.insert(d.key(), d);
  // A dispatched start keeps its original stop even if the source is removed,
  // disabled or edited. Pausing the scheduler only pauses future starts.
  for (auto v : store.query("SELECT data FROM config WHERE key LIKE 'recording/%'")) {
    auto e = Event::parse(QJsonDocument::fromJson(v.toObject()["data"].toString().toUtf8()).object());
    Due stop{e, {"record.stop", "record.stop", "end", 0, {}}, e.end};
    if (!finished.contains(stop.key()))
      due.insert(stop.key(), stop);
  }
  auto out = due.values();
  std::sort(out.begin(), out.end(), [](const Due &a, const Due &b) {
    if (a.time != b.time) return a.time < b.time;
    if (a.action.type != b.action.type) return a.action.type == "record.start";
    return a.key() < b.key();
  });
  return out;
}
void Scheduler::tick(qint64 time) {
  const bool enabled = store.config("settings")["enabled"].toBool(true);
  for (const auto &d : pending()) {
    if (d.time > time) break;
    const bool start = d.action.type == "record.start";
    if (start && !enabled) continue;
    if (start && (time >= d.event.end || time - d.time > 60000)) {
      if (store.claim(d))
        store.finish(d.key(), "skipped", "Recording start was missed");
      continue;
    }
    if (!store.claim(d)) continue;
    try {
      auto result = execute ? execute(d) : Outcome{"failed", "OBS unavailable"};
      store.finish(d.key(), result.result, result.message);
      store.log(result.result, d.event.id + " " + d.action.type + " " + result.message);
    } catch (const std::exception &) {
      store.finish(d.key(), "failed", "Recording action failed; inspect diagnostics");
    }
  }
}
} // namespace bs
