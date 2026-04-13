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
#include <core/circuit.hpp>
#include <memory>
#include <queue>
#include <stdexcept>
#include <vector>

struct TimedEvent {
  uint64_t          time;
  Wire_ptr          targetWire;
  State             newState;
  Component_weakPtr source;

  bool operator>(const TimedEvent& other) const { return time > other.time; }
};

class Simulator {
public:
  explicit Simulator(std::shared_ptr<Circuit> c);
  ~Simulator();

  void run(uint64_t duration);
  void recompile();

  // Reactive Simulation Methods
  void setBus(Bus bus, unsigned int value);
  void simulateBus(const Bus& bus);

  void updateWire(const Wire_ptr& target, State newState, uint64_t delay,
                  const Component_weakPtr& source);
  [[nodiscard]] uint64_t getCurrentTime() const { return currentTime; }

private:
  std::shared_ptr<Circuit>              circuit;
  std::vector<Circuit::SimulationBlock> executionBlocks;
  std::priority_queue<TimedEvent, std::vector<TimedEvent>, std::greater<>> eventQueue;

  uint64_t             topologyListenerId = 0;
  uint64_t             currentTime        = 0;
  static constexpr int MAX_DELTA          = 1000;

  bool cyclicStateChanged = false;

  void evaluateBlock(const Circuit::SimulationBlock& block);
};
