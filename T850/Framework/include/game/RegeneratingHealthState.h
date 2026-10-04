#pragma once

#include <algorithm>
#include <cmath>

namespace t850::game {

class RegeneratingHealthState {
public:
  bool Configure(int maximumHealth, float regenerationSeconds) {
    if (maximumHealth <= 0 || !std::isfinite(regenerationSeconds) ||
        regenerationSeconds <= 0.0f) {
      return false;
    }
    maximumHealth_ = maximumHealth;
    regenerationSeconds_ = regenerationSeconds;
    Reset();
    return true;
  }

  void Reset() {
    currentHealth_ = maximumHealth_;
    regenerationElapsed_ = 0.0f;
  }

  bool ApplyDamage(int amount) {
    if (amount <= 0 || IsDead()) return false;
    currentHealth_ = (std::max)(0, currentHealth_ - amount);
    regenerationElapsed_ = 0.0f;
    return true;
  }

  bool ApplyContact(bool touching, int amount, bool& contactLatch) {
    const bool enteredContact = touching && !contactLatch;
    contactLatch = touching;
    return enteredContact && ApplyDamage(amount);
  }

  bool Update(float deltaSeconds) {
    if (IsDead() || currentHealth_ >= maximumHealth_ ||
        !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) {
      return false;
    }
    regenerationElapsed_ += deltaSeconds;
    bool regenerated = false;
    while (regenerationElapsed_ >= regenerationSeconds_ &&
           currentHealth_ < maximumHealth_) {
      regenerationElapsed_ -= regenerationSeconds_;
      ++currentHealth_;
      regenerated = true;
    }
    if (currentHealth_ >= maximumHealth_) regenerationElapsed_ = 0.0f;
    return regenerated;
  }

  int Current() const { return currentHealth_; }
  int Maximum() const { return maximumHealth_; }
  bool IsDead() const { return currentHealth_ <= 0; }
  float RegenerationSeconds() const { return regenerationSeconds_; }

private:
  int maximumHealth_ = 1;
  int currentHealth_ = 1;
  float regenerationSeconds_ = 60.0f;
  float regenerationElapsed_ = 0.0f;
};

} // namespace t850::game