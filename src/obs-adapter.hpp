#pragma once
#include "scheduler.hpp"
#include <QHash>
#include <QObject>
#include <obs-frontend-api.h>
namespace bs {
class ObsAdapter : public QObject {
  Q_OBJECT
  QHash<QString, qint64> owners;
  QHash<QString, QString> startKeys;
  QString stopKey;
  bool starting = false;
  bool owned = false;
  bool stopping = false;
  bool stopWhenStarted = false;
  void event(obs_frontend_event event);
  static void callback(obs_frontend_event event, void *data);
  void stop();
public:
  explicit ObsAdapter(QObject *parent = nullptr);
  ~ObsAdapter() override;
  Outcome execute(const Due &d, const QJsonObject &settings);
  QJsonObject status() const;
signals:
  void diagnostic(QString level, QString message);
  void completed(QString key, QString result, QString message);
};
} // namespace bs
