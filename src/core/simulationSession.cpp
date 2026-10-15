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

#include "simulationSession.hpp"

#include <stdexcept>
#include <utility>

#include <core/elaboration.hpp>
#include <core/serialization/component_registry.hpp>

namespace SILICON::simulation {

using namespace SILICON::core;
using namespace SILICON::waveform;
using namespace SILICON::waveform::fst;

Session::Session(std::shared_ptr<Circuit>              sourceCircuit,
                 Simulator::CancellationCheck          isCancelled,
                 const SILICON::core::CircuitResolver* resolver)
{
  if (!sourceCircuit)
    throw std::invalid_argument("Session requires a valid Circuit pointer");

  CircuitElaborator elaborator(ComponentRegistry::instance(), resolver);
  runtimeSimulator = std::make_unique<Simulator>(elaborator.elaborate(*sourceCircuit), 0,
                                                 nullptr, std::move(isCancelled));
}

Simulator::RunResult Session::run(const uint64_t               duration,
                                  Simulator::CancellationCheck isCancelled)
{
  return runtimeSimulator->run(duration, std::move(isCancelled));
}

Simulator::RunResult Session::runUntilIdle(Simulator::CancellationCheck isCancelled)
{
  return runtimeSimulator->runUntilIdle(std::move(isCancelled));
}

Simulator::RunResult
Session::simulateWaveform(const uint64_t duration, std::span<const Sample> inputSnapshots,
                          std::span<const Simulator::WaveformInputDriver> inputDrivers,
                          Simulator::CancellationCheck                    isCancelled)
{
  return runtimeSimulator->simulateWaveform(duration, inputSnapshots, inputDrivers,
                                            std::move(isCancelled));
}

Simulator::RunResult Session::setBus(Bus bus, const BusValue& value,
                                     Simulator::CancellationCheck isCancelled)
{
  return runtimeSimulator->setBus(std::move(bus), value, std::move(isCancelled));
}

Simulator::RunResult Session::setBus(Bus bus, const BusValue& value,
                                     const Component_weakPtr&     source,
                                     Simulator::CancellationCheck isCancelled)
{
  return runtimeSimulator->setBus(std::move(bus), value, source, std::move(isCancelled));
}

void Session::setTraceBuses(std::vector<CircuitWriter::NamedBus> buses)
{
  runtimeSimulator->setTraceBuses(std::move(buses));
}

void Session::setTraceSink(Simulator::TraceSink sink)
{
  runtimeSimulator->setTraceSink(std::move(sink));
}

void Session::setFstWriter(std::unique_ptr<CircuitWriter> writer)
{
  runtimeSimulator->setFstWriter(std::move(writer));
}

void Session::enableFstTracing(std::string_view fileName, CircuitWriter::Options options)
{
  runtimeSimulator->enableFstTracing(fileName, std::move(options));
}

uint64_t Session::getCurrentTime() const
{
  return runtimeSimulator->getCurrentTime();
}

}  // namespace SILICON::simulation
