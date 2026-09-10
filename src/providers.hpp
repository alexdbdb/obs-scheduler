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
// OAuth token endpoints expect HTML form encoding, which differs from URL
// query encoding for characters such as '+'. Fields contain decoded strings.
QByteArray formUrlEncoded(const QList<QPair<QString, QString>> &fields);
QString googleApiError(const QByteArray &body, int httpStatus);

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
  quint64 odooGeneration = 0;
  int odooUid = 0;
  bool connecting = false;
  QJsonObject googleClient() const;
  QHash<QTcpSocket *, QByteArray> callbacks;
  void request(const QUrl &url, const QByteArray &bearer,
               std::function<void(QByteArray, QString)> done);
  void token(const QByteArray &body, std::function<void(QString)> done);
  void authorized(std::function<void(QString)> done);
  void googlePage(QJsonObject cal, QString page, QList<Event> events,
                  int pages = 0, qint64 windowStart = 0);
  void odooPost(const QUrl &url, const QJsonObject &body,
                const QList<QPair<QByteArray, QByteArray>> &headers,
                std::function<void(QJsonValue, QString)> done);
  void odooAuthenticate(std::function<void(QString)> done);
  void odooCall(const QString &model, const QString &method,
                const QJsonArray &args, const QJsonObject &kwargs,
                std::function<void(QJsonValue, QString)> done);
  void odooPage(QJsonObject calendar, int offset, QList<Event> events,
                qint64 windowStart = 0);
  void parse(QJsonObject cal, QByteArray data);
  void complete(const QJsonObject &cal, const QList<Event> &events,
                const QString &error);

public:
  Providers(Store &s, Secrets &v, QObject *parent = nullptr);
  ~Providers() override;
  void sync(bool force);
  void configureGoogle(QString clientId, QString clientSecret = {});
  void connectGoogle();
  void disconnectGoogle();
  void listGoogle();
  QJsonObject googleStatus() const;
  void configureOdoo(QJsonObject configuration, QString apiKey = {});
  void disconnectOdoo();
  void listOdoo();
  QJsonObject odooStatus() const;
signals:
  void changed();
  void problem(QString message);
  void openUrl(QString url);
  void googleCalendars(QJsonArray calendars);
  void odooOptions(QJsonObject options);
};
} // namespace bs
