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

#include "wire.hpp"

#include <algorithm>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <utility>

#include <core/wireUtils.hpp>
#include <logging/logger.hpp>
#include <utils/ranges_wrapper.hpp>

State operator&&(const State& a, const State& b)
{
  if (a == State::ERROR || b == State::ERROR)
    return State::ERROR;
  if (a == State::LOW || b == State::LOW)
    return State::LOW;
  if (a == State::HIGH && b == State::HIGH)
    return State::HIGH;
  return State::UNKNOWN;
}

State operator||(const State& a, const State& b)
{
  if (a == State::ERROR || b == State::ERROR)
    return State::ERROR;
  if (a == State::HIGH || b == State::HIGH)
    return State::HIGH;
  if (a == State::LOW && b == State::LOW)
    return State::LOW;
  return State::UNKNOWN;
}

State operator!(const State& a)
{
  if (a == State::ERROR)
    return State::ERROR;
  if (a == State::UNKNOWN)
    return State::UNKNOWN;
  if (a == State::LOW)
    return State::HIGH;
  return State::LOW;
}

State operator^(const State& a, const State& b)
{
  if (a == State::ERROR || b == State::ERROR)
    return State::ERROR;
  if (a == State::UNKNOWN || b == State::UNKNOWN)
    return State::UNKNOWN;
  if (a != b)
    return State::HIGH;
  return State::LOW;
}

std::string to_str(State s)
{
  switch (s) {
    case State::HIGH: return "HIGH";
    case State::LOW: return "LOW";
    case State::UNKNOWN: return "UNKNOWN";
    case State::ERROR: return "ERROR";
  }
  throw std::logic_error("Unhandled State enum value");
}

namespace silicon::wire {

bool busValueOverflowsWidth(const unsigned int value, const std::size_t width)
{
  if (width >= std::numeric_limits<unsigned int>::digits)
    return false;
  return value >= (1u << width);
}

bool busWillChangeToValue(const Bus& bus, const unsigned int value)
{
  for (const auto& [index, wire] : bus | silicon::views::enumerate) {
    if (!wire)
      continue;

    const State newState = (value >> index) & 1 ? State::HIGH : State::LOW;
    if (wire->getCurrentState() != newState)
      return true;
  }

  return false;
}

bool isKnownBinary(const State state)
{
  return state == State::LOW || state == State::HIGH;
}

bool mayBe(const State actual, const State expected)
{
  return actual == expected || !isKnownBinary(actual);
}

State normalizeBinaryOrUnknown(const State state)
{
  return isKnownBinary(state) ? state : State::UNKNOWN;
}

State optionalControlStateOrInactive(const std::vector<Bus>& inputs,
                                     const unsigned int index, const State inactiveState)
{
  if (index >= inputs.size() || inputs[index].size() == 0 || !inputs[index][0])
    return inactiveState;

  return Wire::safeGetCurrentState(inputs[index][0]);
}

}  // namespace silicon::wire

Wire::Wire()
{
  this->id           = nextId.fetch_add(1, std::memory_order_relaxed);
  this->currentState = State::ERROR;
}

Wire::Wire(State s)
{
  this->id           = nextId.fetch_add(1, std::memory_order_relaxed);
  this->currentState = s;
}

Wire::Wire(const uint64_t id, const State s) : currentState(s), id(id)
{
  auto next = nextId.load(std::memory_order_relaxed);
  while (next <= id
         && !nextId.compare_exchange_weak(next, id + 1, std::memory_order_relaxed)) {}
}

Wire::Wire(const Wire& other)
  : currentState(other.getCurrentState()),
    authorizedComponent(other.authorizedComponent),
    id(other.id)
{
  // Wire copies intentionally retain identity to preserve existing Bus initializer-list
  // semantics. Only the state transfer needs special handling because it is atomic.
}

Wire& Wire::operator=(const Wire& other)
{
  if (this == &other)
    return *this;

  currentState.store(other.getCurrentState(), std::memory_order_relaxed);
  authorizedComponent = other.authorizedComponent;
  id                  = other.id;
  return *this;
}

State Wire::getCurrentState() const
{
  // State synchronization does not order other wire fields; simulation topology and
  // authorization are immutable while the UI concurrently reads state for painting.
  return this->currentState.load(std::memory_order_relaxed);
}

void Wire::forceSetCurrentState(const State newState)
{
  this->currentState.store(newState, std::memory_order_relaxed);
}

void Wire::clearAuthorizedComponent()
{
  this->authorizedComponent.reset();
}

void Wire::forceSetCurrentState(const State              newState,
                                const Component_weakPtr& authorizedBy)
{
  this->authorizedComponent = authorizedBy;
  this->currentState.store(newState, std::memory_order_relaxed);
}

void Wire::setCurrentState(const State newState, const Component_weakPtr& requestedBy)
{
  // Every wire has a mechanism to detect graphs error: the component that
  // controls the wire can be only one at a time and it's stored in the
  // authorizedComponent pointer. If another component tries to modify its
  // status then the wire go into the ERROR state, since the graph is malformed.
  if (!this->authorizedComponent.lock())
    this->authorizedComponent = requestedBy;

  const bool changeIsAuthorized = this->authorizedComponent.lock() == requestedBy.lock();

  if (!changeIsAuthorized) {
    static const Logger log("wire");
    log.error("Unauthorized wire state change detected");
  }

  this->forceSetCurrentState(changeIsAuthorized ? newState : State::ERROR);
}

void Wire::safeSetCurrentState(const std::weak_ptr<Wire>& w, State newState,
                               const Component_weakPtr& requestedBy)
{
  if (const auto lockedWire = w.lock()) {
    lockedWire->setCurrentState(newState, requestedBy);
  }
}

State Wire::safeGetCurrentState(const std::weak_ptr<Wire>& w)
{
  if (const auto lockedWire = w.lock()) {
    return lockedWire->getCurrentState();
  }
  return State::ERROR;
}

Bus::Bus(const unsigned short size)
{
  this->busData.reserve(size);
  for (unsigned short i = 0; i < size; i++)
    this->busData.push_back(std::make_shared<Wire>(State::UNKNOWN));
}

void Bus::setSize(const unsigned short size)
{
  const size_t oldSize = this->busData.size();
  this->busData.resize(size);
  for (size_t i = oldSize; i < size; i++)
    this->busData[i] = std::make_shared<Wire>(State::UNKNOWN);
}

Bus::Bus(std::vector<Wire_ptr> busData)
{
  this->busData = std::move(busData);
}

Bus::Bus(std::initializer_list<Wire_ptr> initList)
  : busData(initList.begin(), initList.end())
{
}

Bus::Bus(std::initializer_list<Wire> initList) : busData(initList.size())
{
  size_t i = 0;
  for (const auto& val : initList) {
    busData[i++] = std::make_shared<Wire>(val);
  }
}

int Bus::forceSetCurrentValue(const unsigned int value)
{
  for (unsigned short i = 0; i < this->size(); i++) {
    if (this->busData[i]) {
      State s = (value >> i) & 1 ? State::HIGH : State::LOW;
      this->busData[i]->forceSetCurrentState(s);
    }
  }
  return silicon::wire::busValueOverflowsWidth(value, this->size());
}

int Bus::forceSetCurrentValue(const unsigned int       value,
                              const Component_weakPtr& authorizedBy)
{
  for (unsigned short i = 0; i < this->size(); i++) {
    if (this->busData[i]) {
      State s = (value >> i) & 1 ? State::HIGH : State::LOW;
      this->busData[i]->forceSetCurrentState(s, authorizedBy);
    }
  }
  return silicon::wire::busValueOverflowsWidth(value, this->size());
}

int Bus::setCurrentValue(const unsigned int value, const Component_weakPtr& requestedBy)
{
  for (unsigned short i = 0; i < this->size(); i++) {
    if (this->busData[i]) {
      const State s = (value >> i) & 1 ? State::HIGH : State::LOW;
      Wire::safeSetCurrentState(this->busData[i], s, requestedBy);
    }
  }
  return silicon::wire::busValueOverflowsWidth(value, this->size());
}

unsigned int Bus::getCurrentValue() const
{
  if (isInErrorState() || hasUnknowns())
    throw std::logic_error("int Bus::getCurrentValue() called on a bus in UNKNOWN / "
                           "ERROR state, use the string version instead");

  unsigned int res = 0;
  for (unsigned int i = 0; i < this->size(); i++) {
    if (!this->busData[i])
      return 0;
    State s = this->busData[i]->getCurrentState();
    if (s == State::HIGH)
      res |= (1 << i);
  }
  return res;
}

std::string Bus::getCurrentValueString() const
{
  return busData | std::views::reverse | std::views::transform([](const auto& wire) {
           switch (Wire::safeGetCurrentState(wire)) {
             case State::HIGH: return '1';
             case State::LOW: return '0';
             case State::UNKNOWN: return 'X';
             case State::ERROR: return 'E';
             default: throw std::runtime_error("Unknown state");
           }
           std::unreachable();
         })
         | std::ranges::to<std::string>();
}

bool Bus::isInErrorState() const
{
  return std::ranges::any_of(
      busData, [](const auto& el) { return el->getCurrentState() == State::ERROR; });
}

bool Bus::hasUnknowns() const
{
  return std::ranges::any_of(
      busData, [](const auto& el) { return el->getCurrentState() == State::UNKNOWN; });
}

std::strong_ordering Bus::operator<=>(const Bus& other) const
{
  return std::lexicographical_compare_three_way(
      busData.begin(), busData.end(), other.busData.begin(), other.busData.end(),
      [](const auto& a, const auto& b) { return a->getId() <=> b->getId(); });
}

bool Bus::operator==(const Bus& other) const
{
  return std::ranges::equal(busData, other.busData, [](const auto& a, const auto& b) {
    return a.get() == b.get();
  });
}
