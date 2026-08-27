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
#include <atomic>
#include <compare>
#include <cstdint>
#include <format>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace SILICON::core {

/* --- State values ---------------------------------------------------------------------
 * Each wire could hold one of four states. UNKNOWN can be HIGH or LOW, for example
 * HIGH || UNKNOWN == HIGH, cause (HIGH || HIGH) == (HIGH || LOW) == HIGH.
 *
 * UNKNOWN may also be assigned by users to model an indeterminate input. ERROR is
 * reserved for simulation failures and conflicting drivers. */

enum class State : unsigned char {
  LOW     = '0',
  HIGH    = '1',
  UNKNOWN = 'X',
  ERROR   = 'E',
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

  /** @brief State shared between the simulation worker and UI readers. */
  std::atomic<State> currentState;
  Component_weakPtr  authorizedComponent;
  uint64_t           id;

public:
  Wire();
  explicit Wire(State s);
  Wire(uint64_t id, State s);

  /**
   * @brief Copies a wire while preserving its existing identifier and authorization.
   *
   * Explicitly defined because std::atomic is not implicitly copyable.
   * @param other Wire to copy
   */
  Wire(const Wire& other);

  /**
   * @brief Assigns state, authorization, and identifier from another wire.
   * @param other Wire to assign
   * @return This wire
   */
  Wire& operator=(const Wire& other);

  /**
   * @brief Reads the current state safely while simulation may update it.
   * @return Current logic state
   */
  [[nodiscard]] State    getCurrentState() const;
  [[nodiscard]] uint64_t getId() const { return id; }

  void forceSetCurrentState(State newState);

  /**
   * @brief Clears the component currently authorized to drive this wire.
   *
   * Runtime elaboration uses this when a source-level root wire is intentionally
   * shared with newly cloned runtime components. The next runtime driver then claims
   * authorization through the normal state-update path.
   */
  void clearAuthorizedComponent();

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

using BusValue = std::vector<State>;

BusValue operator+(const BusValue& a, const BusValue& b);
BusValue twosComplement(const BusValue& n);

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

  bool forceSetCurrentValue(const BusValue& value);

  /**
   * @brief Forces the bus value without authorization check.
   * @param value The value to set
   * @param authorizedBy The component that authorized this change
   * @return Non-zero if value exceeds bus size (overflow), zero otherwise
   */
  [[nodiscard]] bool forceSetCurrentValue(const BusValue&          value,
                                          const Component_weakPtr& authorizedBy);
  [[nodiscard]] bool setCurrentValue(const BusValue&          value,
                                     const Component_weakPtr& requestedBy);

  [[nodiscard]] BusValue getCurrentValue() const;
  [[nodiscard]] bool     isInErrorState() const;
  [[nodiscard]] bool     hasUnknowns() const;
  [[nodiscard]] bool     sharesWireWith(const Bus& other) const;

  const Wire_ptr& operator[](const unsigned short index) const
  {
    return this->busData.at(index);
  }

  Wire_ptr& operator[](const unsigned short index) { return this->busData.at(index); }

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

}  // namespace SILICON::core
