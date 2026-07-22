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

#include <core/serialization/component_registry.hpp>

namespace silicon::simulation {

SimulationSession::SimulationSession(std::shared_ptr<Circuit>     sourceCircuit,
                                     Simulator::CancellationCheck isCancelled)
  : sourceCircuit(std::move(sourceCircuit)),
    elaborator(ComponentRegistry::instance())
{
  if (!this->sourceCircuit)
    throw std::invalid_argument("SimulationSession requires a valid Circuit pointer");

  rebuild(std::move(isCancelled));
}

void SimulationSession::rebuild(Simulator::CancellationCheck isCancelled)
{
  auto replacementRuntime   = elaborator.elaborate(*sourceCircuit);
  auto replacementSimulator = std::make_unique<Simulator>(
      replacementRuntime, 0, false, nullptr, std::move(isCancelled));

  replacementSimulator->setTraceBuses(traceBuses);
  if (traceSink)
    replacementSimulator->setTraceSink(traceSink);

  runtime          = std::move(replacementRuntime);
  runtimeSimulator = std::move(replacementSimulator);
}

Simulator::RunResult SimulationSession::run(const uint64_t               duration,
                                            Simulator::CancellationCheck isCancelled)
{
  return runtimeSimulator->run(duration, std::move(isCancelled));
}

Simulator::RunResult
SimulationSession::runUntilIdle(Simulator::CancellationCheck isCancelled)
{
  return runtimeSimulator->runUntilIdle(std::move(isCancelled));
}

Simulator::RunResult SimulationSession::simulateWaveform(
    const uint64_t duration, std::span<const SiliconWaveformSample> inputSnapshots,
    std::span<const Simulator::WaveformInputDriver> inputDrivers,
    Simulator::CancellationCheck                    isCancelled)
{
  return runtimeSimulator->simulateWaveform(duration, inputSnapshots, inputDrivers,
                                            std::move(isCancelled));
}

Simulator::RunResult SimulationSession::setBus(Bus bus, const unsigned int value,
                                               Simulator::CancellationCheck isCancelled)
{
  return runtimeSimulator->setBus(std::move(bus), value, std::move(isCancelled));
}

Simulator::RunResult SimulationSession::setBus(Bus bus, const unsigned int value,
                                               const Component_weakPtr&     source,
                                               Simulator::CancellationCheck isCancelled)
{
  return runtimeSimulator->setBus(std::move(bus), value, source, std::move(isCancelled));
}

void SimulationSession::setTraceBuses(std::vector<SiliconFstWriter::NamedBus> buses)
{
  traceBuses = std::move(buses);
  runtimeSimulator->setTraceBuses(traceBuses);
}

void SimulationSession::setTraceSink(Simulator::TraceSink sink)
{
  traceSink = std::move(sink);
  runtimeSimulator->setTraceSink(traceSink);
}

void SimulationSession::setFstWriter(std::unique_ptr<SiliconFstWriter> writer)
{
  runtimeSimulator->setFstWriter(std::move(writer));
}

void SimulationSession::enableFstTracing(std::string_view          fileName,
                                         SiliconFstWriter::Options options)
{
  runtimeSimulator->enableFstTracing(fileName, std::move(options));
}

uint64_t SimulationSession::getCurrentTime() const
{
  return runtimeSimulator->getCurrentTime();
}

}  // namespace silicon::simulation
