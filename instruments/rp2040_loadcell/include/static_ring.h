#pragma once

#include <stddef.h>

namespace loadcell {

// Single-context fixed-capacity queue. The DRDY ISR queue is intentionally
// separate because it has different interrupt-ownership rules.
template <typename T, size_t Capacity>
class StaticRing {
 public:
  static_assert(Capacity > 0U, "ring capacity must be nonzero");

  bool push(const T &value) {
    if (count_ == Capacity) {
      return false;
    }
    values_[head_] = value;
    head_ = (head_ + 1U) % Capacity;
    ++count_;
    return true;
  }

  bool peek(T *value) const {
    if ((value == nullptr) || (count_ == 0U)) {
      return false;
    }
    *value = values_[tail_];
    return true;
  }

  bool pop(T *value) {
    if (!peek(value)) {
      return false;
    }
    tail_ = (tail_ + 1U) % Capacity;
    --count_;
    return true;
  }

  void clear() {
    head_ = 0U;
    tail_ = 0U;
    count_ = 0U;
  }

  size_t size() const { return count_; }
  constexpr size_t capacity() const { return Capacity; }
  bool empty() const { return count_ == 0U; }
  bool full() const { return count_ == Capacity; }

 private:
  T values_[Capacity]{};
  size_t head_ = 0U;
  size_t tail_ = 0U;
  size_t count_ = 0U;
};

}  // namespace loadcell
