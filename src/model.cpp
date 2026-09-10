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
QString recordingFilename(const Event &event) {
  auto title = event.title.simplified();
  // OBS treats '%' as a filename-format token and the remaining characters
  // are invalid on at least one supported desktop platform.
  title.replace(QRegularExpression(R"([<>:"/\\|?*%\x00-\x1F\x7F])"), "_");
  title.replace(QRegularExpression("_+"), "_");
  while (title.endsWith('.') || title.endsWith(' '))
    title.chop(1);
  if (title.isEmpty())
    title = "Event";
  if (title.size() > 160)
    title = title.left(160).trimmed();
  const auto date = QDateTime::fromMSecsSinceEpoch(event.start, Qt::UTC)
                        .toTimeZone(QTimeZone::systemTimeZone())
                        .date()
                        .toString("yyyy-MM-dd");
  return date + " - " + title;
}
QDateTime instant(const QString &s) {
  const auto text = s.trimmed();
  static QRegularExpression offset("(Z|z|[+-][0-9]{2}:?[0-9]{2})$");
  if (!offset.match(text).hasMatch())
    throw Error("Datetime must be ISO 8601 with Z or an explicit offset");

  // Qt accepts the ISO form with milliseconds on most platforms, but the
  // parser has historically been stricter about fractional seconds and
  // compact numeric offsets on some Windows/Qt combinations. Normalize the
  // harmless variants before parsing and fall back to ISODate for values
  // without a fractional part.
  auto normalized = text;
  if (normalized.endsWith('z'))
    normalized[normalized.size() - 1] = 'Z';
  static QRegularExpression compactOffset("([+-][0-9]{2})([0-9]{2})$");
  const auto compact = compactOffset.match(normalized);
  if (compact.hasMatch())
    normalized.replace(compact.capturedStart(), compact.capturedLength(),
                       compact.captured(1) + ":" + compact.captured(2));

  auto d = QDateTime::fromString(normalized, Qt::ISODateWithMs);
  if (!d.isValid())
    d = QDateTime::fromString(normalized, Qt::ISODate);
  if (!d.isValid())
    throw Error("Datetime must be ISO 8601 with Z or an explicit offset");
  return d.toUTC();
}
QString deviceZone() { return QString::fromUtf8(QTimeZone::systemTimeZoneId()); }
QDateTime deviceInstant(const QString &text) {
  static const QRegularExpression offset("(Z|z|[+-][0-9]{2}:?[0-9]{2})$");
  if (offset.match(text.trimmed()).hasMatch())
    return instant(text);
  // Parse the wall-clock components in UTC first so Qt cannot normalize a DST gap.
  auto local = QDateTime::fromString(text.trimmed() + "Z", Qt::ISODateWithMs);
  if (!local.isValid())
    throw Error("Invalid local date and time");
  QDateTime result(local.date(), local.time(), QTimeZone::systemTimeZone());
  // Reject nonexistent local times rather than silently moving a recording.
  if (!result.isValid() || result.date() != local.date() || result.time() != local.time())
    throw Error("This local time does not exist on this device");
  return result.toUTC();
}
QStringList actionTypes() { return {"record.start", "record.stop"}; }
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
  e.timezone = o["timezone"].toString(deviceZone());
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
  if (!QStringList{"Manual", "Recurring", "ICS", "GoogleCalendar",
                   "OdooEvent", "API"}
           .contains(source))
    throw Error("Invalid event source");
}
QList<Due> plan(const QList<Event> &events) {
  QList<Due> out;
  for (const auto &e : events) {
    if (!e.enabled)
      continue;
    out.append({e, Action{"record.start", "record.start", "start", 0, {}}, e.start});
    out.append({e, Action{"record.stop", "record.stop", "end", 0, {}}, e.end});
  }
  std::sort(out.begin(), out.end(), [](const Due &a, const Due &b) {
    if (a.time != b.time)
      return a.time < b.time;
    // Join an adjacent recording before releasing the preceding owner's lease.
    if (a.action.type != b.action.type)
      return a.action.type == "record.start";
    return a.event.id < b.event.id;
  });
  return out;
}
bool stopAllowed(bool active, bool owned, bool allowUnowned) {
  return active && (owned || allowUnowned);
}
} // namespace bs
