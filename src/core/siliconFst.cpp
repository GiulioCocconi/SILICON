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

#include "siliconFst.hpp"

#include <format>
#include <string>
#include <vector>

namespace SILICON::waveform::fst {

using namespace SILICON::core;

namespace {

  CircuitWriter::Options optionsForCircuit(const Circuit&         circuit,
                                           CircuitWriter::Options options)
  {
    if (options.topScopeName.empty() && !circuit.getName().empty())
      options.topScopeName = circuit.getName();

    return options;
  }

  std::vector<CircuitWriter::NamedBus> collectCircuitIoBuses(const Circuit& circuit)
  {
    std::vector<CircuitWriter::NamedBus> buses;

    const auto inputs = circuit.getInputs();
    buses.reserve(inputs.size() + circuit.getOutputs().size());

    for (std::size_t i = 0; i < inputs.size(); ++i)
      buses.emplace_back(std::format("input_{}", i), inputs[i]);

    const auto outputs = circuit.getOutputs();
    for (std::size_t i = 0; i < outputs.size(); ++i)
      buses.emplace_back(std::format("output_{}", i), outputs[i]);

    return buses;
  }

  std::vector<CircuitWriter::NamedBus>
  registeredBusesFor(const std::vector<CircuitWriter::NamedBus>& namedBuses)
  {
    std::vector<CircuitWriter::NamedBus> buses;
    buses.reserve(namedBuses.size());

    for (const auto& [name, bus] : namedBuses) {
      if (bus.size() == 0)
        continue;

      buses.emplace_back(name, bus);
    }

    return buses;
  }

  std::vector<TraceWriter::TraceSignal>
  traceSignalsFor(const std::vector<CircuitWriter::NamedBus>& namedBuses)
  {
    std::vector<TraceWriter::TraceSignal> signals;
    signals.reserve(namedBuses.size());

    for (const auto& [name, bus] : namedBuses) {
      if (bus.size() == 0)
        continue;

      signals.push_back({name, bus.size()});
    }

    return signals;
  }

  std::string encodeBusValue(const Bus& bus)
  {
    std::string value;
    value.reserve(bus.size());

    for (auto it = bus.end(); it != bus.begin();) {
      --it;
      if (!*it) {
        value.push_back('x');
      } else {
        value.push_back(CircuitWriter::stateToFstValue((*it)->getCurrentState()));
      }
    }

    return value;
  }

}  // namespace

CircuitWriter::CircuitWriter(std::string_view fileName, const Circuit& circuit)
  : CircuitWriter(fileName, circuit, Options{})
{
}

CircuitWriter::CircuitWriter(std::string_view fileName, const Circuit& circuit,
                             Options options)
  : CircuitWriter(fileName, collectCircuitIoBuses(circuit),
                  optionsForCircuit(circuit, std::move(options)))
{
}

CircuitWriter::CircuitWriter(std::string_view             fileName,
                             const std::vector<NamedBus>& buses)
  : CircuitWriter(fileName, buses, Options{})
{
}

CircuitWriter::CircuitWriter(std::string_view             fileName,
                             const std::vector<NamedBus>& namedBuses, Options options)
  : buses(registeredBusesFor(namedBuses)),
    writer(fileName, traceSignalsFor(namedBuses), std::move(options))
{
}

void CircuitWriter::emitSnapshot(uint64_t time)
{
  std::vector<std::string> values;
  values.reserve(buses.size());

  for (const auto& [name, bus] : buses)
    values.push_back(encodeBusValue(bus));

  writer.emitSnapshot(time, values);
}

void CircuitWriter::flush()
{
  writer.flush();
}

std::optional<fstHandle> CircuitWriter::handleForBus(std::string_view name) const
{
  return writer.handleForSignal(name);
}

char CircuitWriter::stateToFstValue(const State state)
{
  switch (state) {
    case State::LOW: return '0';
    case State::HIGH: return '1';
    case State::UNKNOWN: return 'x';
    case State::ERROR: return 'z';
  }

  return 'x';
}

}  // namespace SILICON::waveform::fst
