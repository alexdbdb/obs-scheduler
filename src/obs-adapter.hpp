#pragma once
#include "scheduler.hpp"
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <obs-frontend-api.h>
namespace bs {
class ObsAdapter : public QObject {
  Q_OBJECT
  struct Output {
    QSet<QString> owners, waiting;
    QString pending, pendingKey, stopKey;
    QString restart;
    qint64 requested = 0;
  };
  Output recording, streaming;
  QNetworkAccessManager network;
  void event(obs_frontend_event event);
  static void callback(obs_frontend_event event, void *data);
  Outcome output(const Due &d, const QJsonObject &settings);

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
