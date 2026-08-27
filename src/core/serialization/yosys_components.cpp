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

/* Per-component lowering from Silicon semantics to native Yosys cells. */

#include "yosys_helpers.hpp"
#include "yosys_cells.hpp"

#include <core/flipflops.hpp>
#include <core/gates.hpp>
#include <core/register.hpp>
#include <core/subcircuit.hpp>
#include <extraComponents/arithmetic.hpp>
#include <extraComponents/multiplexer.hpp>
#include <extraComponents/utils.hpp>

#include <optional>
#include <ranges>

namespace SILICON::core {

using SILICON::yosys::Json;
using SILICON::yosys::SerializationContext;
using namespace SILICON::yosys::detail;

namespace yosys_component_detail {

  [[nodiscard]] const Bus& requireBusWidth(const Component& component, const bool input,
                                           const std::size_t index,
                                           const std::size_t expectedWidth)
  {
    const auto& bus = requireBus(component, input, index);
    // A disconnected output is a valid unused pin. The serialization context gives it
    // a private signal when the cell is emitted. Optional inputs are encoded through
    // SerializationContext::inputBits(), which also applies their declared default.
    const bool requiresConnection = input;
    if (bus.size() != expectedWidth
        || (requiresConnection && std::ranges::contains(bus, nullptr))) {
      throw std::runtime_error(
          std::format("Cannot export '{}': {} bus {} must be a {}{}-bit bus",
                      component.typeName(), input ? "input" : "output", index,
                      requiresConnection ? "connected " : "", expectedWidth));
    }
    return bus;
}

[[nodiscard]] const Bus& requireScalarBus(const Component& component, const bool input,
                                          const std::size_t index)
{
  return requireBusWidth(component, input, index, 1);
}

void requireBusCounts(const Component& component, const std::size_t expectedInputs,
                      const std::size_t expectedOutputs)
{
  if (component.inputBuses().size() != expectedInputs
      || component.outputBuses().size() != expectedOutputs) {
    throw std::runtime_error(std::format(
        "Cannot export malformed '{}': unexpected bus count", component.typeName()));
  }
}

}  // namespace yosys_component_detail

using namespace yosys_component_detail;

void AndGate::serializeYosys(SerializationContext& context) const
{
  // Yosys $and cells have exactly two vector inputs. Fold Silicon's arbitrary
  // number of equally sized inputs from left to right, writing the final value to Y.
  emitGateFold(context, *this, "$and", false);
}

void OrGate::serializeYosys(SerializationContext& context) const
{
  // Build a chain of binary $or cells so multi-input and bitwise Silicon OR gates
  // retain their original input count and bus width.
  emitGateFold(context, *this, "$or", false);
}

void NotGate::serializeYosys(SerializationContext& context) const
{
  // A Silicon NOT maps directly to an unsigned, width-preserving Yosys $not cell.
  emitUnary(context, "not", "$not", context.bits(requireBus(*this, true, 0)),
            context.bits(requireBus(*this, false, 0)));
}

void NandGate::serializeYosys(SerializationContext& context) const
{
  // First reduce every input with binary $and cells, then invert the reduced vector;
  // Yosys has no native arbitrary-input NAND cell.
  emitGateFold(context, *this, "$and", true);
}

void NorGate::serializeYosys(SerializationContext& context) const
{
  // First reduce every input with binary $or cells, then invert the reduced vector;
  // this preserves Silicon's arbitrary-input NOR behavior.
  emitGateFold(context, *this, "$or", true);
}

void XorGate::serializeYosys(SerializationContext& context) const
{
  // Fold all inputs through binary $xor cells. For more than two inputs this
  // implements the same odd-parity result as Silicon's XOR gate.
  emitGateFold(context, *this, "$xor", false);
}

}  // namespace SILICON::core

namespace SILICON::extra {
using namespace SILICON::core;
using SILICON::yosys::Json;
using SILICON::yosys::SerializationContext;
using namespace SILICON::core::yosys_component_detail;

void Extender::serializeYosys(SerializationContext& context) const
{
  if (inputBuses().size() != 1 || outputBuses().size() != 1)
    throw std::runtime_error(
        "Cannot export malformed 'Extender': expected 1 input and 1 output");

  const auto inSize  = getPropertyValue<int>("inSize");
  const auto outSize = getPropertyValue<int>("outSize");
  const auto mode    = getPropertyValue<std::string>("mode");
  if (!inSize || !outSize || !mode || *inSize < 1 || *outSize < 1
      || (*mode != SignedMode && *mode != UnsignedMode)) {
    throw std::runtime_error(
        "Cannot export malformed 'Extender': buses do not match its properties");
  }
  const auto& input = requireBusWidth(*this, true, 0, static_cast<std::size_t>(*inSize));
  const auto& output =
      requireBusWidth(*this, false, 0, static_cast<std::size_t>(*outSize));

  context.addCell(
      "extend", "$pos",
      Json{{"A_SIGNED", SerializationContext::parameter(*mode == SignedMode, 1)},
           {"A_WIDTH", SerializationContext::parameter(*inSize)},
           {"Y_WIDTH", SerializationContext::parameter(*outSize)}},
      directions({{"A", "input"}, {"Y", "output"}}),
      Json{{"A", context.bits(input)}, {"Y", context.bits(output)}});
}

void Complementer::serializeYosys(SerializationContext& context) const
{
  if (inputBuses().size() != 1 || outputBuses().size() != 1)
    throw std::runtime_error(
        "Cannot export malformed 'Complementer': expected 1 input and 1 output");

  const auto width = getPropertyValue<int>("size");
  if (!width || *width < 1) {
    throw std::runtime_error(
        "Cannot export malformed 'Complementer': buses do not match its size property");
  }
  const auto  busWidth = static_cast<std::size_t>(*width);
  const auto& input    = requireBusWidth(*this, true, 0, busWidth);
  const auto& output   = requireBusWidth(*this, false, 0, busWidth);

  Json zero = Json::array();
  for (int bit = 0; bit < *width; ++bit)
    zero.push_back("0");

  context.addCell("complement", "$sub",
                  Json{{"A_SIGNED", SerializationContext::parameter(0, 1)},
                       {"B_SIGNED", SerializationContext::parameter(0, 1)},
                       {"A_WIDTH", SerializationContext::parameter(*width)},
                       {"B_WIDTH", SerializationContext::parameter(*width)},
                       {"Y_WIDTH", SerializationContext::parameter(*width)}},
                  directions({{"A", "input"}, {"B", "input"}, {"Y", "output"}}),
                  Json{{"A", std::move(zero)},
                       {"B", context.bits(input)},
                       {"Y", context.bits(output)}});
}

void HalfAdder::serializeYosys(SerializationContext& context) const
{
  if (inputBuses().size() != 2 || outputBuses().size() != 2)
    throw std::runtime_error(
        "Cannot export malformed 'HalfAdder': expected 2 inputs and 2 outputs");

  context.addCell(
      "half_adder", SILICON::yosys::cells::HalfAdder, Json::object(),
      directions({{"A", "input"}, {"B", "input"}, {"SUM", "output"}, {"COUT", "output"}}),
      Json{{"A", context.bits(requireScalarBus(*this, true, 0))},
           {"B", context.bits(requireScalarBus(*this, true, 1))},
           {"SUM", context.bits(requireScalarBus(*this, false, 0))},
           {"COUT", context.bits(requireScalarBus(*this, false, 1))}});
}

void FullAdder::serializeYosys(SerializationContext& context) const
{
  if (inputBuses().size() != 3 || outputBuses().size() != 2)
    throw std::runtime_error(
        "Cannot export malformed 'FullAdder': expected 3 inputs and 2 outputs");

  context.addCell("full_adder", SILICON::yosys::cells::FullAdder, Json::object(),
                  directions({{"A", "input"},
                              {"B", "input"},
                              {"CIN", "input"},
                              {"SUM", "output"},
                              {"COUT", "output"}}),
                  Json{{"A", context.bits(requireScalarBus(*this, true, 0))},
                       {"B", context.bits(requireScalarBus(*this, true, 1))},
                       {"CIN", context.bits(requireScalarBus(*this, true, 2))},
                       {"SUM", context.bits(requireScalarBus(*this, false, 0))},
                       {"COUT", context.bits(requireScalarBus(*this, false, 1))}});
}

void AdderNBits::serializeYosys(SerializationContext& context) const
{
  if (inputBuses().size() != 2 || outputBuses().size() != 2)
    throw std::runtime_error(
        "Cannot export malformed 'AdderNBits': expected 2 inputs and 2 outputs");
  const auto width = getPropertyValue<int>("size");
  if (!width || *width < 1) {
    throw std::runtime_error(
        "Cannot export malformed 'AdderNBits': buses do not match its size property");
  }
  const auto  busWidth = static_cast<std::size_t>(*width);
  const auto& a        = requireBusWidth(*this, true, 0, busWidth);
  const auto& b        = requireBusWidth(*this, true, 1, busWidth);
  const auto& sum      = requireBusWidth(*this, false, 0, busWidth);
  const auto& carry    = requireScalarBus(*this, false, 1);

  context.addCell(
      "adder", SILICON::yosys::cells::Adder,
      Json{{"WIDTH", SerializationContext::parameter(*width)},
           {"A_SIGNED", SerializationContext::parameter(0, 1)},
           {"B_SIGNED", SerializationContext::parameter(0, 1)}},
      directions({{"A", "input"}, {"B", "input"}, {"SUM", "output"}, {"COUT", "output"}}),
      Json{{"A", context.bits(a)},
           {"B", context.bits(b)},
           {"SUM", context.bits(sum)},
           {"COUT", context.bits(carry)}});
}

void Multiplexer::serializeYosys(SerializationContext& context) const
{
  // $bmux expects all selectable lanes packed consecutively into A and uses S as
  // their zero-based index. A one-bit Silicon mux already stores every lane in one
  // packed bus; a wider mux exposes one bus per lane, which is packed here.
  const int         busWidth       = getPropertyValue<int>("busSize").value_or(1);
  const int         selectionWidth = getPropertyValue<int>("selectionSize").value_or(1);
  const std::size_t laneCount      = std::size_t{1} << selectionWidth;
  const std::size_t selectionIndex = busWidth == 1 ? 1 : laneCount;

  std::vector<Json> lanes;
  if (busWidth == 1) {
    lanes.push_back(context.bits(requireBus(*this, true, 0)));
  } else {
    if (inputBuses().size() != laneCount + 1)
      throw std::runtime_error("Cannot export malformed multi-bus multiplexer");
    for (std::size_t lane = 0; lane < laneCount; ++lane)
      lanes.push_back(context.bits(requireBus(*this, true, lane)));
  }

  context.addCell("mux", "$bmux",
                  Json{{"WIDTH", SerializationContext::parameter(busWidth)},
                       {"S_WIDTH", SerializationContext::parameter(selectionWidth)}},
                  directions({{"A", "input"}, {"S", "input"}, {"Y", "output"}}),
                  Json{{"A", SerializationContext::concatenate(lanes)},
                       {"S", context.bits(requireBus(*this, true, selectionIndex))},
                       {"Y", context.bits(requireBus(*this, false, 0))}});
}

void Demultiplexer::serializeYosys(SerializationContext& context) const
{
  // $demux routes A into the S-selected WIDTH-bit slice of Y and clears every other
  // slice. Pack Silicon's separate output buses in lane order to form that Y vector.
  const int         busWidth       = getPropertyValue<int>("busSize").value_or(1);
  const int         selectionWidth = getPropertyValue<int>("selectionSize").value_or(1);
  std::vector<Json> outputs;
  for (const auto& output : outputBuses())
    outputs.push_back(context.bits(output));

  context.addCell("demux", "$demux",
                  Json{{"WIDTH", SerializationContext::parameter(busWidth)},
                       {"S_WIDTH", SerializationContext::parameter(selectionWidth)}},
                  directions({{"A", "input"}, {"S", "input"}, {"Y", "output"}}),
                  Json{{"A", context.bits(requireBus(*this, true, 0))},
                       {"S", context.bits(requireBus(*this, true, 1))},
                       {"Y", SerializationContext::concatenate(outputs)}});
}

void Decoder::serializeYosys(SerializationContext& context) const
{
  // A decoder is a one-bit $demux: the enable signal is its data input and S chooses
  // which output bit receives it, producing zero on all unselected bits.
  const int selectionWidth = getPropertyValue<int>("selectionSize").value_or(1);
  context.addCell("decode", "$demux",
                  Json{{"WIDTH", SerializationContext::parameter(1)},
                       {"S_WIDTH", SerializationContext::parameter(selectionWidth)}},
                  directions({{"A", "input"}, {"S", "input"}, {"Y", "output"}}),
                  Json{{"A", context.bits(requireBus(*this, true, 0))},
                       {"S", context.bits(requireBus(*this, true, 1))},
                       {"Y", context.bits(requireBus(*this, false, 0))}});
}

void WireSplitter::serializeYosys(SerializationContext& context) const
{
  // Splitter outputs are aliases for consecutive input bits. Packing the one-bit
  // outputs in order and passing through $pos expresses that wiring in cell form.
  std::vector<Json> outputs;
  for (const auto& output : outputBuses())
    outputs.push_back(context.bits(output));
  emitUnary(context, "split", "$pos", context.bits(requireBus(*this, true, 0)),
            SerializationContext::concatenate(outputs));
}

void WireMerger::serializeYosys(SerializationContext& context) const
{
  // Merge the ordered input buses into one LSB-first vector, then use $pos as a
  // width-preserving connection to the component's output bus.
  std::vector<Json> inputs;
  for (const auto& input : inputBuses())
    inputs.push_back(context.bits(input));
  emitUnary(context, "merge", "$pos", SerializationContext::concatenate(inputs),
            context.bits(requireBus(*this, false, 0)));
}

}  // namespace SILICON::extra

namespace SILICON::core {

namespace {

  [[nodiscard]] bool connected(const Json& bits)
  {
    return bits.size() == 1 && bits[0].is_number_integer();
}

[[nodiscard]] bool positiveClock(const Component& component)
{
  const auto edge = component.getPropertyValue<std::string>("triggerEdge");
  if (!edge || (*edge != "PET" && *edge != "NET"))
    throw std::runtime_error(std::format(
        "Cannot export '{}': invalid triggerEdge property", component.typeName()));
  return *edge == "PET";
}

void emitDff(SerializationContext& context, const Component& component, Json data,
             std::optional<Json> enable, Json clock, Json clear, Json preset,
             const Bus& q, const Bus& qn, const std::string_view baseCell,
             const std::string_view controlledCell, const std::string_view instanceName)
{
  const bool hasControls = connected(clear) || connected(preset);
  Json parameters{
      {"CLK_POLARITY",
       SerializationContext::parameter(positiveClock(component), 1)}};
  Json portDirections = directions({{"D", "input"}, {"CLK", "input"}});
  Json connections{{"D", std::move(data)}, {"CLK", std::move(clock)}};
  if (enable) {
    parameters["EN_POLARITY"] = SerializationContext::parameter(1, 1);
    portDirections["EN"]      = "input";
    connections["EN"]         = std::move(*enable);
  }
  if (hasControls) {
    parameters["SET_POLARITY"] = SerializationContext::parameter(1, 1);
    parameters["CLR_POLARITY"] = SerializationContext::parameter(1, 1);
    portDirections["SET"]      = "input";
    portDirections["CLR"]      = "input";
    connections["SET"]         = std::move(preset);
    connections["CLR"]         = std::move(clear);
  }
  portDirections["Q"]  = "output";
  portDirections["QN"] = "output";
  connections["Q"]     = context.bits(q);
  connections["QN"]    = context.bits(qn);
  context.addCell(instanceName, hasControls ? controlledCell : baseCell,
                  std::move(parameters), std::move(portDirections),
                  std::move(connections));
}

}  // namespace

void DFlipFlop::serializeYosys(SerializationContext& context) const
{
  requireBusCounts(*this, 4, 2);
  const auto& q  = requireScalarBus(*this, false, 0);
  const auto& qn = requireScalarBus(*this, false, 1);
  emitDff(context, *this, context.inputBits(*this, 0, 1), std::nullopt,
          context.inputBits(*this, 1, 1), context.inputBits(*this, 2, 1),
          context.inputBits(*this, 3, 1), q, qn, SILICON::yosys::cells::Dff,
          SILICON::yosys::cells::Dffsr, "dff");
}

void EFlipFlop::serializeYosys(SerializationContext& context) const
{
  requireBusCounts(*this, 5, 2);
  const auto& q  = requireScalarBus(*this, false, 0);
  const auto& qn = requireScalarBus(*this, false, 1);
  emitDff(context, *this, context.inputBits(*this, 0, 1), context.inputBits(*this, 1, 1),
          context.inputBits(*this, 2, 1), context.inputBits(*this, 3, 1),
          context.inputBits(*this, 4, 1), q, qn, SILICON::yosys::cells::Dffe,
          SILICON::yosys::cells::Dffsre, "dffe");
}

void DLatch::serializeYosys(SerializationContext& context) const
{
  requireBusCounts(*this, 2, 2);
  const auto& q      = requireScalarBus(*this, false, 0);
  const auto& qn     = requireScalarBus(*this, false, 1);
  context.addCell(
      "dlatch", SILICON::yosys::cells::Dlatch,
      Json{{"EN_POLARITY", SerializationContext::parameter(1, 1)}},
      directions({{"D", "input"}, {"EN", "input"}, {"Q", "output"}, {"QN", "output"}}),
      Json{{"D", context.inputBits(*this, 0, 1)},
           {"EN", context.inputBits(*this, 1, 1)},
           {"Q", context.bits(q)},
           {"QN", context.bits(qn)}});
}

void JKFlipFlop::serializeYosys(SerializationContext& context) const
{
  requireBusCounts(*this, 5, 2);
  const auto& q      = requireScalarBus(*this, false, 0);
  const auto& qn     = requireScalarBus(*this, false, 1);
  context.addCell(
      "jkff", SILICON::yosys::cells::Jkff,
      Json{{"CLK_POLARITY", SerializationContext::parameter(positiveClock(*this), 1)},
           {"SET_POLARITY", SerializationContext::parameter(1, 1)},
           {"CLR_POLARITY", SerializationContext::parameter(1, 1)}},
      directions({{"J", "input"},
                  {"K", "input"},
                  {"CLK", "input"},
                  {"SET", "input"},
                  {"CLR", "input"},
                  {"Q", "output"},
                  {"QN", "output"}}),
      Json{{"J", context.inputBits(*this, 0, 1)},
           {"K", context.inputBits(*this, 1, 1)},
           {"CLK", context.inputBits(*this, 2, 1)},
           {"SET", context.inputBits(*this, 4, 1)},
           {"CLR", context.inputBits(*this, 3, 1)},
           {"Q", context.bits(q)},
           {"QN", context.bits(qn)}});
}

void Register::serializeYosys(SerializationContext& context) const
{
  const auto widthProperty = getPropertyValue<int>("size");
  const auto inputType     = getPropertyValue<std::string>("inputType");
  const auto outputType    = getPropertyValue<std::string>("outputType");
  if (!widthProperty || *widthProperty <= 1 || !inputType || !outputType
      || (*inputType != ParallelType && *inputType != SerialType)
      || (*outputType != ParallelType && *outputType != SerialType)) {
    throw std::runtime_error("Cannot export malformed 'Register': invalid properties");
  }
  const auto width          = static_cast<std::size_t>(*widthProperty);
  const bool parallelIn     = *inputType == ParallelType;
  const bool parallelOut    = *outputType == ParallelType;
  const auto expectedInputs = parallelIn && !parallelOut ? 5U : 4U;
  if (inputBuses().size() != expectedInputs || outputBuses().size() != 1)
    throw std::runtime_error("Cannot export malformed 'Register': unexpected bus count");

  const auto  expectedDataWidth   = parallelIn ? width : 1;
  const auto  expectedOutputWidth = parallelOut ? width : 1;
  const auto& data                = requireBusWidth(*this, true, 0, expectedDataWidth);
  const auto& clock               = requireScalarBus(*this, true, 1);
  const auto& enable              = requireScalarBus(*this, true, 2);
  const auto& clear               = requireScalarBus(*this, true, 3);
  const auto& output              = requireBusWidth(*this, false, 0, expectedOutputWidth);
  Json parameters{{"WIDTH", SerializationContext::parameter(width)},
                  {"CLK_POLARITY", SerializationContext::parameter(1, 1)},
                  {"EN_POLARITY", SerializationContext::parameter(1, 1)},
                  {"CLR_POLARITY", SerializationContext::parameter(1, 1)}};
  Json connections{{"DATA", context.bits(data)},
                   {"CLK", context.bits(clock)},
                   {"EN", context.bits(enable)},
                   {"CLR", context.bits(clear)},
                   {"OUT", context.bits(output)}};

  if (parallelIn && !parallelOut) {
    parameters["LOAD_POLARITY"] = SerializationContext::parameter(1, 1);
    connections["LOAD"]         = context.bits(requireScalarBus(*this, true, 4));
    context.addCell("piso", SILICON::yosys::cells::Piso, std::move(parameters),
                    directions({{"DATA", "input"},
                                {"CLK", "input"},
                                {"EN", "input"},
                                {"CLR", "input"},
                                {"LOAD", "input"},
                                {"OUT", "output"}}),
                    std::move(connections));
    return;
  }

  const auto cellType = parallelIn    ? SILICON::yosys::cells::Pipo
                        : parallelOut ? SILICON::yosys::cells::Sipo
                                      : SILICON::yosys::cells::Siso;
  const auto cellName = parallelIn ? "pipo" : parallelOut ? "sipo" : "siso";
  context.addCell(cellName, cellType, std::move(parameters),
                  directions({{"DATA", "input"},
                              {"CLK", "input"},
                              {"EN", "input"},
                              {"CLR", "input"},
                              {"OUT", "output"}}),
                  std::move(connections));
}

void SubcircuitComponent::serializeYosys(SerializationContext& context) const
{
  // Resolve the referenced definition by slug, serialize it once as its own Yosys
  // module, and instantiate that module with this component's buses as named ports.
  const auto slug = getPropertyValue<std::string>("slug").value_or(std::string());
  context.addSubcircuitInstance(slug, inputBuses(), outputBuses());
}

}  // namespace SILICON::core
