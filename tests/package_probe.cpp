#include <QCoreApplication>
#include <QSslSocket>
#include <QTextStream>

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if (argc != 2)
    return 2;
  QCoreApplication::setLibraryPaths({QString::fromLocal8Bit(argv[1])});
  if (!QSslSocket::availableBackends().contains("schannel") ||
      !QSslSocket::setActiveBackend("schannel") || !QSslSocket::supportsSsl()) {
    QTextStream(stderr) << "Packaged Schannel backend could not initialize\n";
    return 1;
  }
  QTextStream(stdout) << "Packaged TLS backend initialized against OBS Qt runtime\n";
}
