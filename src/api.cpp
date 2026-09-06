#include "api.hpp"
#include "model.hpp"
#include <QCryptographicHash>
#include <QJsonDocument>
namespace bs {
Api::Api(QObject *p) : QObject(p) {
  for (auto path :
       {"/api/v1/status", "/api/v1/events", "/api/v1/templates", "/api/v1/sync",
        "/api/v1/actions/record/start", "/api/v1/actions/record/stop",
        "/api/v1/actions/stream/start", "/api/v1/actions/stream/stop"})
    http.route(path,
               [this](const QHttpServerRequest &r) { return dispatch(r); });
  http.route("/api/v1/events/<arg>",
             [this](const QString &, const QHttpServerRequest &r) {
               return dispatch(r);
             });
}
void Api::configure(const QJsonObject &s) {
  if (listener) {
    listener->close();
    delete listener;
    listener = nullptr;
  }
  if (!s["api_enabled"].toBool())
    return;
  digest = QByteArray::fromHex(s["api_token_hash"].toString().toLatin1());
  if (digest.size() != 32)
    throw Error("Generate an API token before enabling the API");
  QHostAddress address(s["api_host"].toString("127.0.0.1"));
  if (address.isNull())
    throw Error("API host must be a numeric IP address");
  if (!address.isLoopback() && !s["api_network_acknowledged"].toBool())
    throw Error("Network exposure requires explicit acknowledgement");
  int port = s["api_port"].toInt(8766);
  if (port < 1024 || port > 65535)
    throw Error("API port must be 1024–65535");
  listener = new QTcpServer(this);
  listener->setMaxPendingConnections(32);
  if (!listener->listen(address, quint16(port))) {
    delete listener;
    listener = nullptr;
    throw Error("API port unavailable");
  }
  http.bind(listener);
}
QHttpServerResponse Api::dispatch(const QHttpServerRequest &r) {
  using Code = QHttpServerResponder::StatusCode;
  auto error = [](Code c, const QString &m) {
    return QHttpServerResponse(QJsonObject{{"error", m}}, c);
  };
  auto auth = r.value("Authorization");
  auto hash = QCryptographicHash::hash(auth.mid(7), QCryptographicHash::Sha256);
  unsigned diff = 0;
  for (int i = 0; i < 32; i++)
    diff |= unsigned(hash[i] ^ digest[i]);
  if (!auth.startsWith("Bearer ") || diff)
    return error(Code::Unauthorized, "Invalid or missing Bearer token");
  if (r.value("Origin").size())
    return error(Code::Forbidden,
                 "Browser cross-origin API requests are disabled");
  if (r.body().size() > 1024 * 1024)
    return error(Code::PayloadTooLarge, "Request exceeds 1 MiB");
  QJsonObject body;
  if (!r.body().isEmpty()) {
    QJsonParseError e;
    auto doc = QJsonDocument::fromJson(r.body(), &e);
    if (e.error != QJsonParseError::NoError || !doc.isObject())
      return error(Code::BadRequest, "Expected a JSON object");
    body = doc.object();
  }
  QString method;
  switch (r.method()) {
  case QHttpServerRequest::Method::Get:
    method = "GET";
    break;
  case QHttpServerRequest::Method::Post:
    method = "POST";
    break;
  case QHttpServerRequest::Method::Put:
    method = "PUT";
    break;
  case QHttpServerRequest::Method::Delete:
    method = "DELETE";
    break;
  default:
    return error(Code::MethodNotAllowed, "Method not allowed");
  }
  try {
    auto response = call(method + " " + r.url().path(), body);
    int status = response.take("_status").toInt(200);
    return QHttpServerResponse(response, Code(status));
  } catch (const Error &e) {
    return error(Code::BadRequest, QString::fromUtf8(e.what()));
  } catch (const std::exception &) {
    return error(Code::InternalServerError, "Internal failure");
  }
}
} // namespace bs
