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
  void tick(qint64 time);
  QList<Due> pending() const;
};
} // namespace bs
