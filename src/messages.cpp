#include "messages.hpp"
#include <QJsonObject>

namespace bs {
MessageCatalog::MessageCatalog(const QJsonArray &catalog) {
  for (const auto value : catalog) {
    const auto object = value.toObject();
    Entry entry{object["key"].toString(), object["source"].toString(), {}, 0};
    auto pattern = QRegularExpression::escape(entry.source);
    for (int i = 1; i <= 9 && pattern.contains("%" + QString::number(i)); ++i) {
      pattern.replace(QRegularExpression::escape("%" + QString::number(i)), "(.*?)");
      ++entry.arguments;
    }
    entry.pattern = QRegularExpression("\\A" + pattern + "\\z",
                                     QRegularExpression::DotMatchesEverythingOption);
    entries.append(entry);
  }
}
QString MessageCatalog::translate(
    const QString &text, const std::function<QString(const QString &)> &lookup,
    int depth) const {
  if (text.isEmpty() || depth > 4)
    return text;
  for (const auto &entry : entries) {
    const auto match = entry.pattern.match(text);
    if (!match.hasMatch())
      continue;
    auto result = lookup(entry.key);
    if (result.isEmpty() || result == entry.key)
      result = entry.source;
    // Replace all placeholders in one pass so a server-supplied "%2" cannot
    // become a second placeholder or alter another captured argument.
    QString output;
    int offset = 0;
    static const QRegularExpression placeholder("%([1-9])");
    auto positions = placeholder.globalMatch(result);
    while (positions.hasNext()) {
      const auto position = positions.next();
      output += result.mid(offset, position.capturedStart() - offset);
      const int index = position.captured(1).toInt();
      output += index <= entry.arguments
                    ? translate(match.captured(index), lookup, depth + 1)
                    : position.captured();
      offset = position.capturedEnd();
    }
    return output + result.mid(offset);
  }
  return text; // External service details and identifiers are kept verbatim.
}
}
