#pragma once
#include <QHttpServer>
#include <QTcpServer>
#include <functional>
namespace bs {
class Api : public QObject {
  Q_OBJECT
  QHttpServer http;
  QTcpServer *listener = nullptr;
  QByteArray digest;
  QHttpServerResponse dispatch(const QHttpServerRequest &request);

public:
  explicit Api(QObject *parent = nullptr);
  std::function<QJsonObject(QString, QJsonObject)> call;
  void configure(const QJsonObject &settings);
  bool running() const { return listener && listener->isListening(); }
};
} // namespace bs
