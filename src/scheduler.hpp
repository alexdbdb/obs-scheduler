#pragma once
#include "store.hpp"
#include <functional>
namespace bs {
struct Outcome {
  QString result, message;
};
class Scheduler {
  Store &store;
  mutable quint64 revision = ~quint64(0);
  mutable QList<Due> cached;

public:
  explicit Scheduler(Store &s) : store(s) {}
  std::function<Outcome(const Due &)> execute;
  std::function<void(const Due &, const QString &)> ask;
  void tick(qint64 time);
  void answer(const QString &key,
              int minutes); // -1 cancel, 0 scheduled/now, >0 extension
  QList<Due> pending() const;
};
} // namespace bs
