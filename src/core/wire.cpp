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

#include "wire.hpp"

#include <stdexcept>

State operator&&(const State& a, const State& b)
{
  if (a == State::ERROR || b == State::ERROR)
    return State::ERROR;

  if (a == State::HIGH && b == State::HIGH)
    return State::HIGH;

  return State::LOW;
}

State operator||(const State& a, const State& b)
{
  if (a == State::ERROR || b == State::ERROR)
    return State::ERROR;

  if (a == State::HIGH || b == State::HIGH)
    return State::HIGH;

  return State::LOW;
}

State operator!(const State& a)
{
  if (a == State::ERROR)
    return State::ERROR;

  if (a == State::LOW)
    return State::HIGH;

  return State::LOW;
}

State operator^(const State& a, const State& b)
{
  if (a == State::ERROR || b == State::ERROR)
    return State::ERROR;

  if (a != b)
    return State::HIGH;

  return State::LOW;
}

std::string to_str(State s)
{
  switch (s) {
    case State::HIGH: return "HIGH";
    case State::LOW: return "LOW";
    case State::ERROR: return "ERROR";
  }
  throw std::logic_error("Unhandled State enum value");
}

Wire::Wire()
{
  this->id                  = nextId.fetch_add(1, std::memory_order_relaxed);
  this->currentState        = State::ERROR;
  this->updateActions       = {};
  this->authorizedComponent = {};
}

Wire::Wire(State s)
{
  this->id           = nextId.fetch_add(1, std::memory_order_relaxed);
  this->currentState = s;
}

State Wire::getCurrentState() const
{
  return this->currentState;
}

void Wire::forceSetCurrentState(const State newState)
{
  if (this->currentState == newState)
    return;

  this->currentState = newState;

  for (const action_ptr& a : this->updateActions)
    if (a)
      (*a)();
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

  if (!changeIsAuthorized)
    std::cout << "Change not authorized";

  State s = changeIsAuthorized ? newState : State::ERROR;

  this->forceSetCurrentState(s);
}

void Wire::deleteUpdateAction(const action_ptr& a)
{
  const auto b   = this->updateActions.begin();
  const auto e   = this->updateActions.end();
  const auto pos = std::find(b, e, a);

  if (pos != e)
    this->updateActions.erase(pos);
}
void Wire::safeSetCurrentState(const std::weak_ptr<Wire>& w, State newState,
                               const Component_weakPtr& requestedBy)
{
  // Little hack necessary because the component's action logic doesn't know if its output
  // is connected. Without this, each action would need to check for the output wire's
  // existence every time it runs.

  const auto lockedWire = w.lock();
  if (!lockedWire) {
    std::cout << "Wire not found";
    return;
  }

  lockedWire->setCurrentState(newState, requestedBy);
}

State Wire::safeGetCurrentState(const std::weak_ptr<Wire>& w)
{
  const auto lockedWire = w.lock();
  return lockedWire ? lockedWire->getCurrentState() : State::ERROR;
}

void Wire::addUpdateAction(const action_ptr& a)
{
  if (!a)
    throw std::invalid_argument("Cannot add null update action");

  this->updateActions.push_back(a);

  // When I add the action I need to run it right away in order to make it work
  // when the state of the inputs is changed before the component is created!
  (*a)();
}

Bus::Bus(const unsigned short size)
{
  this->busData.reserve(size);
  for (unsigned short i = 0; i < size; i++)
    this->busData.push_back(std::make_shared<Wire>(State::ERROR));
}

void Bus::setSize(const unsigned short size)
{
  const size_t oldSize = this->busData.size();

  this->busData.resize(size);

  // `resize` adds nullpointers if the new size is greater, so we need to fill them up
  for (size_t i = oldSize; i < size; i++)
    this->busData[i] = std::make_shared<Wire>(State::ERROR);
}

Bus::Bus(std::vector<Wire_ptr> busData)
{
  this->busData = busData;

  for (Wire_ptr& w : busData)
    if (!w)
      w = std::make_shared<Wire>(State::LOW);
}

Bus::Bus(std::initializer_list<Wire_ptr> initList) : busData(initList.size())
{
  size_t i = 0;
  for (const auto& val : initList) {
    busData[i++] = val;
  }
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
    if (!this->busData[i])
      continue;

    State s = (value >> i) & 1 ? State::HIGH : State::LOW;
    this->busData[i]->forceSetCurrentState(s);
  }

  // Overflow flag
  return (value >= (1u << this->size()));
}

int Bus::setCurrentValue(const unsigned int value, const Component_weakPtr& requestedBy)
{
  for (unsigned short i = 0; i < this->size(); i++) {
    if (!this->busData[i])
      continue;
    const State s = (value >> i) & 1 ? State::HIGH : State::LOW;
    Wire::safeSetCurrentState(this->busData[i], s, requestedBy);
  }
  return (value >= (1u << this->size()));
}

unsigned int Bus::getCurrentValue() const
{
  unsigned int res = 0;

  for (unsigned int i = 0; i < this->size(); i++) {
    // When the ith bit of the bus is not connected then the value of the whole bus MUST
    // be 0
    if (!this->busData[i])
      return 0;

    State s = this->busData[i]->getCurrentState();
    if (s == State::ERROR)
      throw std::logic_error("Bus::getCurrentValue() called on a bus in ERROR state");

    if (s == State::HIGH)
      res |= (1 << i);
  }
  return res;
}
bool Bus::isInErrorState() const
{
  using std::ranges::any_of;
  return any_of(busData,
                [](const auto& el) { return el->getCurrentState() == State::ERROR; });
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