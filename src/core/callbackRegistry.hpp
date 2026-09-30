/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstdint>
#include <exception>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <logging/logger.hpp>

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

  void notify(Args... args) const noexcept
  {
    try {
      std::vector<Callback> snapshot;
      snapshot.reserve(callbacks.size());
      for (const auto& [id, callback] : callbacks) {
        static_cast<void>(id);
        snapshot.push_back(callback);
      }
      for (auto& callback : snapshot) {
        try {
          callback(args...);
        } catch (const std::exception& error) {
          reportFailure(error.what());
        } catch (...) {
          reportFailure("Unknown callback exception");
        }
      }
    } catch (const std::exception& error) {
      reportFailure(error.what());
    } catch (...) {
      reportFailure("Unknown callback notification failure");
    }
  }

private:
  static void reportFailure(const std::string_view message) noexcept
  {
    try {
      SILICON::logging::Logger::error("callbacks", message);
    } catch (...) {
      // Observer reporting must not escape the notification boundary either.
    }
  }

  std::unordered_map<std::uint64_t, Callback> callbacks;
  std::uint64_t                               nextId = 0;
};

}  // namespace SILICON::core
