#include "calendar.hpp"
#include <QCryptographicHash>
#include <libical/ical.h>
#include <memory>
#include <mutex>
namespace bs {
namespace {
std::recursive_mutex icalMutex;
QString str(const char *s) { return s ? QString::fromUtf8(s) : QString(); }
QString stable(const QString &c, const QString &id) {
  return QString::fromLatin1(
      QCryptographicHash::hash((c + "\n" + id).toUtf8(),
                               QCryptographicHash::Sha256)
          .toHex());
}
struct Context {
  icalcomponent *root;
  QJsonObject calendar;
  QList<Event> events;
  QString error;
  int count = 0;
  QHash<icalcomponent *, int> allDays;
};
QString zoneOf(icalproperty *p, const QString &fallback) {
  auto tz = icalproperty_get_first_parameter(p, ICAL_TZID_PARAMETER);
  auto name = tz ? str(icalparameter_get_tzid(tz)) : fallback;
  const QString prefix = "/freeassociation.sourceforge.net/";
  if (name.startsWith(prefix)) name = name.mid(prefix.size());
  return name;
}
icaltimetype resolved(icalcomponent *root, icalproperty *p, icaltimetype t,
                      const QString &fallback) {
  if (icaltime_is_utc(t))
    return t;
  auto name = zoneOf(p, fallback);
  auto z = icalcomponent_get_timezone(root, name.toUtf8().constData());
  if (!z)
    z = icaltimezone_get_builtin_timezone(name.toUtf8().constData());
  if (!z && name == "UTC")
    z = icaltimezone_get_utc_timezone();
  if (!z)
    throw Error("Unknown ICS timezone: " + name);
  t.zone = z;
  return t;
}
void occurrence(icalcomponent *comp, icaltime_span *span, void *data) {
  auto *c = static_cast<Context *>(data);
  if (++c->count > 20000) {
    c->error = "ICS expansion exceeds 20000 occurrences";
    return;
  }
  try {
    Event e;
    e.calendar = c->calendar["id"].toString();
    auto ext = str(icalcomponent_get_uid(comp));
    if (ext.isEmpty())
      throw Error("VEVENT is missing UID");
    bool recurring = icalcomponent_get_first_property(comp, ICAL_RRULE_PROPERTY) ||
                     icalcomponent_get_first_property(comp, ICAL_RDATE_PROPERTY) ||
                     icalcomponent_get_first_property(comp, ICAL_RECURRENCEID_PROPERTY);
    e.externalId = recurring ? ext + "/" + QString::number(span->start) : ext;
    e.id = stable(e.calendar, e.externalId);
    e.title = str(icalcomponent_get_summary(comp));
    if (e.title.isEmpty())
      e.title = "(Untitled)";
    e.description = str(icalcomponent_get_description(comp));
    e.start = qint64(span->start) * 1000 +
              c->calendar["start_offset"].toInt() * 1000LL;
    e.end =
        qint64(span->end) * 1000 + c->calendar["end_offset"].toInt() * 1000LL;
    if (c->allDays.contains(comp)) {
      auto zone = icalcomponent_get_dtstart(comp).zone;
      auto end = icaltime_from_timet_with_zone(span->start, 0, zone);
      icaltime_adjust(&end, c->allDays.value(comp), 0, 0, 0);
      e.end = qint64(icaltime_as_timet_with_zone(end, zone)) * 1000 +
              c->calendar["end_offset"].toInt() * 1000LL;
    }
    e.source = "ICS";
    e.templateId = c->calendar["template"].toString();
    auto p = icalcomponent_get_first_property(comp, ICAL_DTSTART_PROPERTY);
    e.timezone = zoneOf(p, c->calendar["timezone"].toString("UTC"));
    if (!QTimeZone(e.timezone.toUtf8()).isValid())
      e.timezone = "UTC";
    e.metadata = {{"uid", ext},
                  {"all_day", c->allDays.contains(comp)}};
    e.updated = now();
    e.validate();
    c->events.append(e);
  } catch (const std::exception &e) {
    c->error = QString::fromUtf8(e.what());
  }
}
} // namespace
void CalendarParser::setZoneDirectory(const QString &directory) {
  std::lock_guard<std::recursive_mutex> lock(icalMutex);
  if (!directory.isEmpty()) {
    set_zone_directory(directory.toUtf8().constData());
    icaltimezone_set_builtin_tzdata(1);
  }
}
QList<Event> CalendarParser::ics(const QByteArray &data, const QJsonObject &cal,
                                 qint64 from, qint64 until) {
  std::lock_guard<std::recursive_mutex> lock(icalMutex);
  if (data.size() > 8 * 1024 * 1024)
    throw Error("ICS exceeds 8 MiB limit");
  std::unique_ptr<icalcomponent, decltype(&icalcomponent_free)> root(
      icalparser_parse_string(data.constData()), icalcomponent_free);
  if (!root || icalcomponent_isa(root.get()) != ICAL_VCALENDAR_COMPONENT ||
      icalcomponent_count_errors(root.get()))
    throw Error("Invalid iCalendar document");
  Context c{root.get(), cal, {}, "", 0};
  auto lower = icaltime_from_timet_with_zone(from / 1000, 0,
                                             icaltimezone_get_utc_timezone());
  auto upper = icaltime_from_timet_with_zone(until / 1000, 0,
                                             icaltimezone_get_utc_timezone());
  QHash<QString, QList<Event>> overrides;
  QSet<QString> cancelled;
  for (auto comp =
           icalcomponent_get_first_component(root.get(), ICAL_VEVENT_COMPONENT);
       comp; comp = icalcomponent_get_next_component(root.get(),
                                                     ICAL_VEVENT_COMPONENT)) {
    auto uid = str(icalcomponent_get_uid(comp));
    if (uid.isEmpty())
      throw Error("VEVENT is missing UID");
    if (auto rule = icalcomponent_get_first_property(comp, ICAL_RRULE_PROPERTY)) {
      auto r = icalproperty_get_rrule(rule);
      if (r.freq < ICAL_DAILY_RECURRENCE || r.by_second[0] != ICAL_RECURRENCE_ARRAY_MAX ||
          r.by_minute[0] != ICAL_RECURRENCE_ARRAY_MAX)
        throw Error("Sub-daily ICS expansion is not supported; cached events preserved");
    }
    auto p = icalcomponent_get_first_property(comp, ICAL_DTSTART_PROPERTY);
    auto rid =
        icalcomponent_get_first_property(comp, ICAL_RECURRENCEID_PROPERTY);
    QString overrideKey;
    if (rid) {
      if (icalproperty_get_first_parameter(rid, ICAL_RANGE_PARAMETER))
        throw Error("RECURRENCE-ID RANGE is not supported; sync rejected to "
                    "preserve cached events");
      auto rt = resolved(root.get(), rid, icalproperty_get_recurrenceid(rid),
                         cal["timezone"].toString("UTC"));
      overrideKey =
          uid + "/" + QString::number(icaltime_as_timet_with_zone(rt, rt.zone));
    }
    if (icalcomponent_get_status(comp) == ICAL_STATUS_CANCELLED) {
      if (rid)
        cancelled.insert(overrideKey);
      else
        cancelled.insert(uid);
      continue;
    }
    if (!p)
      throw Error("VEVENT is missing DTSTART");
    auto t = resolved(root.get(), p, icalcomponent_get_dtstart(comp),
                      cal["timezone"].toString("UTC"));
    icalcomponent_set_dtstart(comp, t);
    auto ep = icalcomponent_get_first_property(comp, ICAL_DTEND_PROPERTY);
    if (ep) {
      auto et = resolved(root.get(), ep, icalcomponent_get_dtend(comp),
                         cal["timezone"].toString("UTC"));
      icalcomponent_set_dtend(comp, et);
    } else if (!icalcomponent_get_first_property(comp,
                                                 ICAL_DURATION_PROPERTY)) {
      if (t.is_date) {
        auto et = t;
        icaltime_adjust(&et, 1, 0, 0, 0);
        icalcomponent_set_dtend(comp, et);
      } else
        throw Error("Timed VEVENT requires DTEND or DURATION");
    }
    if (t.is_date) {
      auto et = icalcomponent_get_dtend(comp);
      c.allDays[comp] = QDate(t.year,t.month,t.day).daysTo(QDate(et.year,et.month,et.day));
      if (c.allDays.value(comp) <= 0) throw Error("Invalid all-day duration");
      t.is_date = 0; et.is_date = 0;
      icalcomponent_set_dtstart(comp,t); icalcomponent_set_dtend(comp,et);
    }
    int before = c.events.size();
    icalcomponent_foreach_recurrence(comp, lower, upper, occurrence, &c);
    if (rid) {
      QList<Event> list;
      while (c.events.size() > before) {
        auto e = c.events.takeLast();
        e.externalId = overrideKey;
        e.id = stable(e.calendar, e.externalId);
        list.prepend(e);
      }
      overrides.insert(overrideKey, list);
    }
  }
  if (!c.error.isEmpty())
    throw Error(c.error);
  QList<Event> out;
  QSet<QString> seen;
  for (auto e : c.events) {
    if (cancelled.contains(e.metadata["uid"].toString()) ||
        cancelled.contains(e.externalId) || overrides.contains(e.externalId))
      continue;
    if (seen.contains(e.externalId))
      throw Error("Duplicate UID/occurrence in ICS");
    seen.insert(e.externalId);
    out.append(e);
  }
  for (auto list : overrides)
    for (auto e : list)
      if (!cancelled.contains(e.externalId) &&
          !cancelled.contains(e.metadata["uid"].toString()))
        out.append(e);
  return out;
}
QList<Event> CalendarParser::recurrence(const Event &seed, const QString &rule,
                                        qint64 from, qint64 until) {
  std::lock_guard<std::recursive_mutex> lock(icalMutex);
  auto r = icalrecurrencetype_from_string(rule.toUtf8().constData());
  if (r.freq == ICAL_NO_RECURRENCE)
    throw Error("Invalid RRULE");
  if (r.freq != ICAL_DAILY_RECURRENCE && r.freq != ICAL_WEEKLY_RECURRENCE &&
      r.freq != ICAL_MONTHLY_RECURRENCE && r.freq != ICAL_YEARLY_RECURRENCE) {
    icalrecurrencetype_clear(&r);
    throw Error("Recurrence must be daily or less frequent");
  }
  icalrecurrencetype_clear(&r);
  auto zone = QTimeZone(seed.timezone.toUtf8());
  auto s = QDateTime::fromMSecsSinceEpoch(seed.start, zone),
       e = QDateTime::fromMSecsSinceEpoch(seed.end, zone);
  // Let libical serialize escaping and own the RFC grammar.
  auto root = icalcomponent_new_vcalendar();
  icalcomponent_add_property(root, icalproperty_new_version("2.0"));
  auto comp = icalcomponent_new_vevent();
  icalcomponent_add_component(root, comp);
  icalcomponent_set_uid(comp, seed.id.toUtf8().constData());
  icalcomponent_set_summary(comp, seed.title.toUtf8().constData());
  if (!seed.description.isEmpty())
    icalcomponent_set_description(comp, seed.description.toUtf8().constData());
  auto makeTime = [&](const QDateTime &d) {
    auto t = icaltime_from_string(
        d.toString("yyyyMMdd'T'HHmmss").toUtf8().constData());
    auto z =
        icaltimezone_get_builtin_timezone(seed.timezone.toUtf8().constData());
    if (!z && seed.timezone == "UTC")
      z = icaltimezone_get_utc_timezone();
    t.zone = z;
    return t;
  };
  icalcomponent_set_dtstart(comp, makeTime(s));
  icalcomponent_set_dtend(comp, makeTime(e));
  auto p = icalproperty_new_from_string(("RRULE:" + rule).toUtf8().constData());
  if (!p) {
    icalcomponent_free(root);
    throw Error("Invalid RRULE");
  }
  icalcomponent_add_property(comp, p);
  auto serialized = QByteArray(icalcomponent_as_ical_string(root));
  icalcomponent_free(root);
  auto out = ics(serialized,
                 {{"id", seed.id},
                  {"template", seed.templateId},
                  {"timezone", seed.timezone}},
                 from, until);
  for (auto &v : out) {
    v.source = "Recurring";
    v.enabled = seed.enabled;
    v.metadata["seed"] = seed.id;
  }
  return out;
}
} // namespace bs
