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

/* Internal helpers shared by the per-component Yosys lowerings. */
#pragma once

#include <format>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <core/component.hpp>
#include <core/serialization/yosys.hpp>

namespace silicon::yosys::detail {

[[nodiscard]] inline Json
directions(std::initializer_list<std::pair<std::string_view, std::string_view>> values)
{
  Json result = Json::object();
  for (const auto& [port, direction] : values)
    result[std::string(port)] = direction;
  return result;
}

[[nodiscard]] inline const Bus& requireBus(const Component& component, const bool input,
                                           const std::size_t index)
{
  const auto& buses = input ? component.inputBuses() : component.outputBuses();
  if (index >= buses.size() || buses[index].size() == 0) {
    throw std::runtime_error(
        std::format("Cannot export '{}': {} bus {} is missing or empty",
                    component.typeName(), input ? "input" : "output", index));
  }
  return buses[index];
}

[[nodiscard]] inline Json unsignedParameters(const std::size_t aWidth,
                                             const std::size_t bWidth,
                                             const std::size_t yWidth)
{
  return Json{{"A_SIGNED", SerializationContext::parameter(0, 1)},
              {"B_SIGNED", SerializationContext::parameter(0, 1)},
              {"A_WIDTH", SerializationContext::parameter(aWidth)},
              {"B_WIDTH", SerializationContext::parameter(bWidth)},
              {"Y_WIDTH", SerializationContext::parameter(yWidth)}};
}

inline void emitUnary(SerializationContext& context, const std::string_view suffix,
                      const std::string_view type, const Json& input, const Json& output)
{
  context.addCell(suffix, type,
                  Json{{"A_SIGNED", SerializationContext::parameter(0, 1)},
                       {"A_WIDTH", SerializationContext::parameter(input.size())},
                       {"Y_WIDTH", SerializationContext::parameter(output.size())}},
                  directions({{"A", "input"}, {"Y", "output"}}),
                  Json{{"A", input}, {"Y", output}});
}

inline void emitBinary(SerializationContext& context, const std::string_view suffix,
                       const std::string_view type, const Json& lhs, const Json& rhs,
                       const Json& output)
{
  context.addCell(suffix, type, unsignedParameters(lhs.size(), rhs.size(), output.size()),
                  directions({{"A", "input"}, {"B", "input"}, {"Y", "output"}}),
                  Json{{"A", lhs}, {"B", rhs}, {"Y", output}});
}

/**
 * Yosys binary logic cells have two inputs. Silicon gates may have any number, so
 * fold their inputs through temporary vectors and optionally invert the final value.
 */
inline void emitGateFold(SerializationContext& context, const Component& component,
                         const std::string_view operation, const bool invert)
{
  const auto& inputs = component.inputBuses();
  const auto& output = requireBus(component, false, 0);
  if (inputs.empty())
    throw std::runtime_error(
        std::format("Cannot export '{}' without inputs", component.typeName()));

  const std::size_t width = output.size();
  for (std::size_t index = 0; index < inputs.size(); ++index) {
    if (inputs[index].size() != width) {
      throw std::runtime_error(
          std::format("Cannot export '{}': input {} width does not match output width",
                      component.typeName(), index));
    }
  }

  Json accumulator = context.bits(inputs.front());
  if (inputs.size() == 1) {
    emitUnary(context, invert ? "invert" : "buffer", invert ? "$not" : "$pos",
              accumulator, context.bits(output));
    return;
  }

  for (std::size_t index = 1; index < inputs.size(); ++index) {
    const bool writesOutput = index + 1 == inputs.size() && !invert;
    Json       next = writesOutput ? context.bits(output) : context.allocateBits(width);
    emitBinary(context, std::format("fold_{}", index), operation, accumulator,
               context.bits(inputs[index]), next);
    accumulator = std::move(next);
  }
  if (invert)
    emitUnary(context, "invert", "$not", accumulator, context.bits(output));
}

}  // namespace silicon::yosys::detail
