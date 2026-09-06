#include "model.hpp"
#include <QRegularExpression>
#include <QUuid>
#include <algorithm>
#include <cmath>
namespace bs {
QString uid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }
QString iso(qint64 ms) {
  return QDateTime::fromMSecsSinceEpoch(ms, Qt::UTC)
      .toString(Qt::ISODateWithMs);
}
QDateTime instant(const QString &s) {
  static QRegularExpression offset("(Z|[+-][0-9]{2}:[0-9]{2})$");
  auto d = QDateTime::fromString(s, Qt::ISODateWithMs);
  if (!d.isValid() || !offset.match(s).hasMatch())
    throw Error("Datetime must be ISO 8601 with Z or an explicit offset");
  return d.toUTC();
}
QStringList actionTypes() {
  return {"record.start",  "record.stop",    "record.pause",  "record.resume",
          "stream.start",  "stream.stop",    "scene.current", "scene.preview",
          "source.enable", "source.disable", "source.show",   "source.hide",
          "replay.start",  "replay.stop",    "replay.save",   "hotkey.trigger",
          "command.run",   "webhook.http"};
}
QJsonObject Action::json() const {
  return {{"id", id},
          {"type", type},
          {"reference", reference},
          {"offset", double(offset)},
          {"parameters", parameters}};
}
Action Action::parse(const QJsonObject &o) {
  Action a;
  a.id = o["id"].toString();
  if (a.id.isEmpty())
    a.id = uid();
  a.type = o["type"].toString();
  a.reference = o["reference"].toString("start");
  double n = o["offset"].toDouble();
  if (!std::isfinite(n) || std::abs(n) > 366 * 86400 || std::floor(n) != n)
    throw Error("Invalid action offset");
  a.offset = qint64(n);
  a.parameters = o["parameters"].toObject();
  if (!actionTypes().contains(a.type) ||
      (a.reference != "start" && a.reference != "end"))
    throw Error("Invalid action type or reference");
  return a;
}
QJsonObject Template::json() const {
  QJsonArray a;
  for (auto &v : actions)
    a.append(v.json());
  return {{"id", id}, {"name", name}, {"actions", a}};
}
Template Template::parse(const QJsonObject &o) {
  Template t;
  t.id = o["id"].toString();
  if (t.id.isEmpty())
    t.id = uid();
  t.name = o["name"].toString().trimmed();
  if (t.name.isEmpty())
    throw Error("Template name is required");
  QSet<QString> ids;
  for (auto v : o["actions"].toArray()) {
    auto a = Action::parse(v.toObject());
    if (ids.contains(a.id))
      throw Error("Duplicate action ID");
    ids.insert(a.id);
    t.actions.append(a);
  }
  if (t.actions.size() > 128)
    throw Error("Too many actions");
  return t;
}
QJsonObject Event::json() const {
  return {{"id", id},
          {"external_id", externalId},
          {"title", title},
          {"description", description},
          {"start", iso(start)},
          {"end", iso(end)},
          {"timezone", timezone},
          {"source", source},
          {"source_calendar", calendar},
          {"template", templateId},
          {"enabled", enabled},
          {"metadata", metadata},
          {"last_updated", iso(updated)}};
}
Event Event::parse(const QJsonObject &o) {
  Event e;
  e.id = o["id"].toString();
  if (e.id.isEmpty())
    e.id = uid();
  e.externalId = o["external_id"].toString();
  e.title = o["title"].toString().trimmed();
  e.description = o["description"].toString();
  e.start = instant(o["start"].toString()).toMSecsSinceEpoch();
  e.end = instant(o["end"].toString()).toMSecsSinceEpoch();
  e.timezone = o["timezone"].toString("UTC");
  e.source = o["source"].toString("Manual");
  e.calendar = o["source_calendar"].toString();
  e.templateId = o["template"].toString();
  e.enabled = o["enabled"].toBool(true);
  e.metadata = o["metadata"].toObject();
  e.updated = o.contains("last_updated") ? instant(o["last_updated"].toString()).toMSecsSinceEpoch() : now();
  e.validate();
  return e;
}
void Event::validate() const {
  if (id.isEmpty() || title.isEmpty() || title.size() > 1024 || end <= start ||
      end - start > 366LL * 86400000)
    throw Error("Event requires title and end after start (maximum 366 days)");
  if (!QTimeZone(timezone.toUtf8()).isValid())
    throw Error("Unknown timezone");
  if (!QStringList{"Manual", "Recurring", "ICS", "GoogleCalendar", "API"}
           .contains(source))
    throw Error("Invalid event source");
}
QList<Due> plan(const QList<Event> &events, const QList<Template> &templates) {
  QHash<QString, Template> map;
  for (auto &t : templates)
    map.insert(t.id, t);
  QList<Due> out;
  for (auto &e : events)
    if (e.enabled && map.contains(e.templateId))
      for (auto &a : map[e.templateId].actions)
        out.append(
            {e, a,
             (a.reference == "start" ? e.start : e.end) + a.offset * 1000});
  std::sort(out.begin(), out.end(), [](const Due &a, const Due &b) {
    return a.time == b.time ? a.key() < b.key() : a.time < b.time;
  });
  return out;
}
bool stopAllowed(bool active, bool owned, bool allowUnowned) {
  return active && (owned || allowUnowned);
}
} // namespace bs
