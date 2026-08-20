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
#include <ranges>
#include <stdexcept>
#include <utility>

#include <core/wireUtils.hpp>
#include <logging/logger.hpp>
#include <utils/ranges_wrapper.hpp>

namespace SILICON::core {

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

}  // namespace SILICON::core

namespace SILICON::wireUtils {

using namespace SILICON::core;

bool busValueOverflowsWidth(const BusValue& value, const std::size_t width)
{
  return !fitsUnsigned(value, width);
}

bool fitsUnsigned(const BusValue& value, const std::size_t width)
{
  return value.size() <= width
         || std::ranges::all_of(
             value.begin() + static_cast<std::ptrdiff_t>(width), value.end(),
             [](const State state) { return state == State::LOW; });
}

bool fitsSigned(const BusValue& value, const std::size_t width)
{
  if (value.empty() || width == 0)
    return false;
  if (value.size() <= width)
    return true;

  const State sign = value[width - 1];
  if (!isKnownBinary(sign))
    return false;

  return std::ranges::all_of(
      value.begin() + static_cast<std::ptrdiff_t>(width), value.end(),
      [sign](const State state) { return state == sign; });
}

BusValue normalizeBusValue(const BusValue& value, const std::size_t width,
                           const State extension)
{
  BusValue normalized(value.begin(),
                      value.begin()
                          + static_cast<std::ptrdiff_t>(std::min(value.size(), width)));
  normalized.resize(width, extension);
  return normalized;
}

bool busWillChangeToValue(const Bus& bus, const BusValue& value)
{
  return bus.getCurrentValue() != normalizeBusValue(value, bus.size());
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

}  // namespace SILICON::wireUtils

namespace SILICON::core {

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
    static const SILICON::logging::Logger log("wire");
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
  const auto lockedWire = w.lock();
  return lockedWire ? lockedWire->getCurrentState() : State::ERROR;
}

BusValue operator+(const BusValue& a, const BusValue& b)
{
  // +1 safely accommodates the maximum possible carry-out bit
  const auto width = std::max(a.size(), b.size()) + 1;

  const auto zeroExtend = [width](BusValue value) {
    value.resize(width, State::LOW);
    return value;
  };

  BusValue res = zeroExtend(a);
  const BusValue extB = zeroExtend(b);

  auto carry = State::LOW;

  for (auto&& [idx, s] : res | SILICON::views::enumerate) {
    const State valA = s;
    const State valB = extB[idx];

    s = valA ^ valB ^ carry;
    carry = (valA && valB) || (carry && (valA ^ valB));
  }

  return res;
}

BusValue twosComplement(const BusValue& n)
{
  BusValue res = n | std::views::transform([](const State s) { return !s; })
                   | std::ranges::to<BusValue>();

  State carry = State::HIGH;

  for (State& s : res) {
    const State currentBit = s;

    s = currentBit ^ carry;
    carry = currentBit && carry;
  }

  return res;
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

bool Bus::forceSetCurrentValue(const BusValue& value)
{
  const auto normalized = SILICON::wireUtils::normalizeBusValue(value, size());
  for (auto i = 0uz; i < size(); ++i) {
    const auto currentWire = this->busData[i];

    if (!currentWire)
      continue;

    currentWire->forceSetCurrentState(normalized[i]);
  }
  return SILICON::wireUtils::busValueOverflowsWidth(value, size());
}

bool Bus::forceSetCurrentValue(const BusValue&          value,
                               const Component_weakPtr& authorizedBy)
{
  const auto normalized = SILICON::wireUtils::normalizeBusValue(value, size());
  for (auto i = 0uz; i < size(); ++i) {
    const auto currentWire = this->busData[i];

    if (!currentWire)
      continue;

    currentWire->forceSetCurrentState(normalized[i], authorizedBy);
  }
  return SILICON::wireUtils::busValueOverflowsWidth(value, size());
}

bool Bus::setCurrentValue(const BusValue& value, const Component_weakPtr& requestedBy)
{
  const auto normalized = SILICON::wireUtils::normalizeBusValue(value, size());
  for (auto i = 0uz; i < size(); ++i) {
    const auto currentWire = this->busData[i];

    if (!currentWire)
      continue;

    Wire::safeSetCurrentState(currentWire, normalized[i], requestedBy);
  }
  return SILICON::wireUtils::busValueOverflowsWidth(value, size());
}

BusValue Bus::getCurrentValue() const
{
  return this->busData | std::views::transform([](const auto& wire) {
    return Wire::safeGetCurrentState(wire);
  }) | std::ranges::to<BusValue>();
}

bool Bus::isInErrorState() const
{
  return std::ranges::any_of(busData, [](const auto& el) {
    return Wire::safeGetCurrentState(el) == State::ERROR;
  });
}

bool Bus::hasUnknowns() const
{
  return std::ranges::any_of(busData, [](const auto& el) {
    return Wire::safeGetCurrentState(el) == State::UNKNOWN;
  });
}

bool Bus::sharesWireWith(const Bus& other) const
{
  for (const auto& localWire : busData) {
    if (!localWire)
      continue;

    for (const auto& otherWire : other.busData) {
      if (otherWire && localWire.get() == otherWire.get())
        return true;
    }
  }
  return false;
}

std::strong_ordering Bus::operator<=>(const Bus& other) const
{
  return std::lexicographical_compare_three_way(
      busData.begin(), busData.end(), other.busData.begin(), other.busData.end(),
      [](const auto& a, const auto& b) {
        if (!a && !b)
          return std::strong_ordering::equal;
        if (!a)
          return std::strong_ordering::less;
        if (!b)
          return std::strong_ordering::greater;

        return a->getId() <=> b->getId();
      });
}

bool Bus::operator==(const Bus& other) const
{
  return std::ranges::equal(busData, other.busData, [](const auto& a, const auto& b) {
    return a.get() == b.get();
  });
}

}  // namespace SILICON::core
