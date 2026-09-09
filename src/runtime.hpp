#pragma once
#include "api.hpp"
#include "providers.hpp"
#include "scheduler.hpp"
#include <QTimer>
#include <memory>
namespace bs {
class Runtime : public QObject {
  Q_OBJECT
  QString path;
  std::unique_ptr<Store> store;
  std::unique_ptr<Secrets> secrets;
  std::unique_ptr<Scheduler> scheduler;
  Providers *providers = nullptr;
  Api *api = nullptr;
  QTimer *timer = nullptr;
  qint64 recurrenceRefresh = 0;
  QString engineError;
  void migrateRecordingSchedule();
  void expandRecurrences();
  QJsonObject request(const QString &operation, QJsonObject data);

public:
  explicit Runtime(QString p) : path(std::move(p)) {}
  std::function<Outcome(const Due &, const QJsonObject &)> action;
  std::function<QJsonObject()> obsStatus;
public slots:
  void start();
  void shutdown();
  void command(QString operation, QJsonObject data = {});
  void snapshot();
signals:
  void state(QJsonObject data);
  void problem(QString message);
  void openUrl(QString url);
  void googleCalendars(QJsonArray calendars);
  void tokenGenerated(QString token);
};
} // namespace bs
