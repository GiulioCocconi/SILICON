/*
 Copyright (c) 2026. Giulio Cocconi
 ...
 */

#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace SILICON::core {

template <typename... Args> class CallbackRegistry {
public:
  using Callback = std::function<void(Args...)>;

  [[nodiscard]] std::uint64_t add(Callback callback)
  {
    const auto id = ++nextId;
    callbacks.emplace(id, std::move(callback));
    return id;
  }

  void remove(const std::uint64_t id) { callbacks.erase(id); }

  void notify(Args... args)
  {
    std::vector<Callback> snapshot;
    snapshot.reserve(callbacks.size());
    for (const auto& [id, callback] : callbacks)
      snapshot.push_back(callback);
    for (auto& callback : snapshot)
      callback(args...);
  }

private:
  std::unordered_map<std::uint64_t, Callback> callbacks;
  std::uint64_t                               nextId = 0;
};

}  // namespace SILICON::core
