#pragma once
#include "model.hpp"
namespace bs {
class CalendarParser {
public:
  static void setZoneDirectory(const QString &directory);
  static QList<Event> ics(const QByteArray &data, const QJsonObject &calendar,
                          qint64 from, qint64 until);
  static QList<Event> recurrence(const Event &seed, const QString &rrule,
                                 qint64 from, qint64 until);
};
} // namespace bs
