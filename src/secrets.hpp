#pragma once
#include <QString>
namespace bs {
class Secrets {
  QString directory;

public:
  explicit Secrets(QString path) : directory(std::move(path)) {}
  void write(const QString &name, const QByteArray &value);
  QByteArray read(const QString &name) const;
  void remove(const QString &name);
  static QByteArray random();
};
} // namespace bs
