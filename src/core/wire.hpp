/*
  Copyright (C) 2026 Giulio Cocconi

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
#include <algorithm>
#include <atomic>
#include <compare>
#include <format>
#include <memory>
#include <set>
#include <string>
#include <vector>

/* --- State values ---------------------------------------------------------------------
 * Each wire could hold one of four states. UNKNOWN can be HIGH or LOW, for example
 * HIGH || UNKNOWN == HIGH, cause (HIGH || HIGH) == (HIGH || LOW) == HIGH.
 *
 * UNKNOWN and ERROR states are only used as simulation internal values, they should not
 * be assignable as inputs. */

enum class State {
  LOW,
  HIGH,
  UNKNOWN,
  ERROR,
};

State operator&&(const State& a, const State& b);
State operator||(const State& a, const State& b);
State operator^(const State& a, const State& b);
State operator!(const State& a);

std::string to_str(State s);

class Component;
using Component_weakPtr = std::weak_ptr<Component>;
using Component_ptr     = std::shared_ptr<Component>;
using Component_set     = std::set<Component_ptr, std::owner_less<Component_ptr>>;

class Wire {
private:
  static inline std::atomic<uint64_t> nextId{0};

  State             currentState;
  Component_weakPtr authorizedComponent;
  uint64_t          id;

public:
  Wire();
  explicit Wire(State s);

  [[nodiscard]] State    getCurrentState() const;
  [[nodiscard]] uint64_t getId() const { return id; }

  void forceSetCurrentState(State newState);

  /**
   * @brief Forces the wire state without authorization check.
   * @param newState The new state to set
   * @param authorizedBy The component that authorized this change
   */
  void forceSetCurrentState(State newState, const Component_weakPtr& authorizedBy);
  void setCurrentState(State newState, const Component_weakPtr& requestedBy);

  static void  safeSetCurrentState(const std::weak_ptr<Wire>& w, State newState,
                                   const Component_weakPtr& requestedBy);
  static State safeGetCurrentState(const std::weak_ptr<Wire>& w);
};

using Wire_ptr = std::shared_ptr<Wire>;

class Bus {
private:
  std::vector<Wire_ptr> busData;

public:
  Bus() = default;
  explicit Bus(unsigned short size);
  explicit Bus(std::vector<Wire_ptr> busData);
  Bus(std::initializer_list<Wire> initList);
  Bus(std::initializer_list<Wire_ptr> initList);

  void setSize(unsigned short size);

  int forceSetCurrentValue(unsigned int value);

  /**
   * @brief Forces the bus value without authorization check.
   * @param value The value to set
   * @param authorizedBy The component that authorized this change
   * @return Non-zero if value exceeds bus size (overflow), zero otherwise
   */
  int forceSetCurrentValue(unsigned int value, const Component_weakPtr& authorizedBy);
  int setCurrentValue(unsigned int value, const Component_weakPtr& requestedBy);

  [[nodiscard]] unsigned int getCurrentValue() const;
  [[nodiscard]] bool         isInErrorState() const;

  const Wire_ptr& operator[](unsigned short index) const
  {
    return this->busData.at(index);
  }

  Wire_ptr& operator[](unsigned short index) { return this->busData.at(index); }

  explicit operator std::vector<Wire_ptr>() const { return this->busData; }
  explicit operator std::vector<Wire_ptr>() { return this->busData; }

  auto begin() { return this->busData.begin(); }
  auto end() { return this->busData.end(); }

  [[nodiscard]] auto begin() const { return this->busData.begin(); }
  [[nodiscard]] auto end() const { return this->busData.end(); }

  [[nodiscard]] auto size() { return this->busData.size(); }
  [[nodiscard]] auto size() const { return this->busData.size(); }

  bool                 operator==(const Bus& other) const;
  std::strong_ordering operator<=>(const Bus& other) const;
};
