#pragma once
#include <QJsonArray>
#include <QRegularExpression>
#include <QVector>
#include <functional>

namespace bs {
// Translate at the presentation boundary: saved logs and API responses remain
// stable, while old and new diagnostics follow the user's current OBS locale.
class MessageCatalog {
  struct Entry {
    QString key, source;
    QRegularExpression pattern;
    int arguments;
  };
  QVector<Entry> entries;
public:
  explicit MessageCatalog(const QJsonArray &catalog);
  QString translate(const QString &text,
                    const std::function<QString(const QString &)> &lookup,
                    int depth = 0) const;
};
}
