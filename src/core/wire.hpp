/*
 Copyright (c) 2025. Giulio Cocconi

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
#include <functional>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <vector>

// Each wire could hold one of three states
enum class State {
  LOW,
  HIGH,
  ERROR,
};

State operator&&(const State& a, const State& b);
State operator||(const State& a, const State& b);
State operator^(const State& a, const State& b);
State operator!(const State& a);

std::string to_str(State s);

// Following SICP 3.3.4 there are no gates as devices but only wires with
// associate update actions.
using action     = std::function<void()>;
using action_ptr = std::shared_ptr<action>;

class Component;
using Component_weakPtr = std::weak_ptr<Component>;
using Component_ptr     = std::shared_ptr<Component>;
using Component_set     = std::set<Component_weakPtr, std::owner_less<Component_weakPtr>>;

class Wire {
private:
  static inline std::atomic<uint64_t> nextId{0};

  State                   currentState;
  std::vector<action_ptr> updateActions;
  Component_weakPtr       authorizedComponent;
  uint64_t                id;

public:
  Wire();
  explicit Wire(State s);

  State    getCurrentState() const;
  void     forceSetCurrentState(const State newState);
  uint64_t getId() const { return id; }

  void setCurrentState(State newState, const Component_weakPtr& requestedBy);

  void addUpdateAction(const action_ptr& a);
  void deleteUpdateAction(const action_ptr& a);

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

  int forceSetCurrentValue(const unsigned int value);

  int setCurrentValue(unsigned int value, const Component_weakPtr& requestedBy);

  [[nodiscard]] unsigned int getCurrentValue() const;

  [[nodiscard]] bool isInErrorState() const;

  Wire_ptr& operator[](unsigned short index) { return this->busData.at(index); }
  explicit  operator std::vector<Wire_ptr>() const { return this->busData; }
  explicit  operator std::vector<Wire_ptr>() { return this->busData; }

  auto begin() { return this->busData.begin(); }
  auto end() { return this->busData.end(); }

  [[nodiscard]] auto begin() const { return this->busData.begin(); }
  [[nodiscard]] auto end() const { return this->busData.end(); }

  [[nodiscard]] auto size() { return this->busData.size(); }
  [[nodiscard]] auto size() const { return this->busData.size(); }

  bool                 operator==(const Bus& other) const;
  std::strong_ordering operator<=>(const Bus& other) const;
};
