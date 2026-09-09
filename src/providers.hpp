#pragma once
#include "secrets.hpp"
#include "store.hpp"
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QTcpServer>
#include <QThreadPool>
#include <functional>
namespace bs {
// Providers publish normalized snapshots; none has access to the OBS adapter.
class Providers : public QObject {
  Q_OBJECT
  Store &store;
  Secrets &secrets;
  QNetworkAccessManager network;
  QTcpServer oauth;
  QThreadPool parsing;
  QSet<QString> busy;
  QHash<QString, qint64> last;
  QByteArray access, verifier, state;
  qint64 accessExpires = 0;
  QString redirect;
  quint64 googleGeneration = 0;
  bool connecting = false;
  QJsonObject googleClient() const;
  QHash<QTcpSocket *, QByteArray> callbacks;
  void request(const QUrl &url, const QByteArray &bearer,
               std::function<void(QByteArray, QString)> done);
  void token(const QByteArray &body, std::function<void(QString)> done);
  void authorized(std::function<void(QString)> done);
  void googlePage(QJsonObject cal, QString page, QList<Event> events,
                  int pages = 0);
  void parse(QJsonObject cal, QByteArray data);
  void complete(const QJsonObject &cal, const QList<Event> &events,
                const QString &error);

public:
  Providers(Store &s, Secrets &v, QObject *parent = nullptr);
  ~Providers() override;
  void sync(bool force);
  void connectGoogle();
  void disconnectGoogle();
  void listGoogle();
  QJsonObject googleStatus() const;
signals:
  void changed();
  void problem(QString message);
  void openUrl(QString url);
  void googleCalendars(QJsonArray calendars);
};
} // namespace bs
