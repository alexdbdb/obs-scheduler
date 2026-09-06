#pragma once
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QTimeZone>
#include <stdexcept>

namespace bs {
struct Error : std::runtime_error {
  explicit Error(const QString &s) : std::runtime_error(s.toStdString()) {}
};
QString uid();
qint64 now();
QDateTime instant(const QString &text);
QString iso(qint64 ms);
QStringList actionTypes();
struct Action {
  QString id, type, reference = "start";
  qint64 offset = 0; // seconds
  QJsonObject parameters;
  QJsonObject json() const;
  static Action parse(const QJsonObject &o);
};
struct Template {
  QString id, name;
  QList<Action> actions;
  QJsonObject json() const;
  static Template parse(const QJsonObject &o);
};
struct Event {
  QString id, externalId, title, description,
      timezone = "UTC", source = "Manual", calendar, templateId;
  qint64 start = 0, end = 0, updated = 0;
  bool enabled = true;
  QJsonObject metadata;
  QJsonObject json() const;
  static Event parse(const QJsonObject &o);
  void validate() const;
};
struct Due {
  Event event;
  Action action;
  qint64 time = 0;
  QString key() const { return event.id + "/" + action.id; }
};
QList<Due> plan(const QList<Event> &events, const QList<Template> &templates);
bool stopAllowed(bool active, bool owned, bool allowUnowned);
} // namespace bs
