#include "runtime.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QTextStream>
#include <QTimer>
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if (argc < 2)
    return 2;
  bs::Runtime runtime(QString::fromUtf8(argv[1]));
  runtime.action = [](const bs::Due &, const QJsonObject &) {
    return bs::Outcome{"failed", "Headless test service has no OBS adapter"};
  };
  QObject::connect(&runtime, &bs::Runtime::problem,
                   [](QString m) { QTextStream(stderr) << m << Qt::endl; });
  runtime.start();
  if (qEnvironmentVariableIsSet("BS_TEST_TOKEN")) {
    auto token = qgetenv("BS_TEST_TOKEN");
    runtime.command("settings.save",
                    {{"api_token_hash",
                      QString::fromLatin1(QCryptographicHash::hash(
                                              token, QCryptographicHash::Sha256)
                                              .toHex())},
                     {"api_enabled", true},
                     {"api_port",qEnvironmentVariableIsSet("BS_TEST_PORT") ? qEnvironmentVariableIntValue("BS_TEST_PORT") : 8766}});
  }
  if (argc > 2 && QString::fromUtf8(argv[2]) == "--initialize") {
    runtime.shutdown();
    return 0;
  }
  QObject::connect(&app, &QCoreApplication::aboutToQuit, &runtime,
                   &bs::Runtime::shutdown);
  return app.exec();
}
