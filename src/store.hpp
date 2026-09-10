#pragma once
#include "model.hpp"
#include <sqlite3.h>
namespace bs {
class Store {
  sqlite3 *db = nullptr;
  quint64 generation = 0;

public:
  quint64 revision() const { return generation; }
  explicit Store(const QString &path);
  ~Store();
  Store(const Store &) = delete;
  void sql(const QString &sql);
  QJsonArray query(const QString &sql, const QVariantList &args = {}) const;
  void run(const QString &sql, const QVariantList &args = {});
  QList<Event> events() const;
  QList<Template> templates() const;
  Event event(const QString &id) const;
  void put(Event e);
  void put(const Template &t);
  void erase(const QString &id);
  void ignoreExternal(const Event &event);
  void clearIgnored(const QString &calendar);
  void replaceCalendar(const QString &id, const QList<Event> &events);
  QJsonObject config(const QString &key) const;
  void config(const QString &key, const QJsonObject &value);
  bool claim(const Due &d);
  bool finished(const QString &key) const;
  void finish(const QString &key, const QString &result,
              const QString &message);
  void defer(const Due &d, qint64 until, const QString &state);
  QJsonArray history() const;
  void log(const QString &level, const QString &message);
};
} // namespace bs
