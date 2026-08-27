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

/* Strict lowering from a supported Yosys write_json module to Silicon components. */

#include "yosys.hpp"
#include "yosys_cells.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <core/circuit.hpp>
#include <core/flipflops.hpp>
#include <core/gates.hpp>
#include <core/io.hpp>
#include <core/register.hpp>
#include <extraComponents/arithmetic.hpp>
#include <extraComponents/multiplexer.hpp>
#include <extraComponents/utils.hpp>
#include <utils/num_formatting.hpp>

namespace SILICON::yosys {

using namespace SILICON::core;
using namespace SILICON::extra;
namespace {

  [[noreturn]] void fail(const std::string_view context, const std::string_view message)
  {
    throw std::runtime_error(
        std::format("Invalid Yosys JSON at {}: {}", context, message));
  }

  [[nodiscard]] std::string_view binaryDigits(const Json&            value,
                                              const std::string_view context)
  {
    if (!value.is_string())
      fail(context, "expected a binary string or unsigned integer");
    const auto& digits = value.get_ref<const std::string&>();
    if (digits.empty())
      fail(context, "binary value must not be empty");
    if (!std::ranges::all_of(
            digits, [](const char digit) { return digit == '0' || digit == '1'; })) {
      fail(context, "expected a fully defined binary string");
    }
    return digits;
  }

  [[nodiscard]] std::uint64_t parseUnsigned(const Json&            value,
                                            const std::string_view context)
  {
    if (value.is_number_unsigned())
      return value.get<std::uint64_t>();
    if (value.is_number_integer()) {
      const auto result = value.get<std::int64_t>();
      if (result < 0)
        fail(context, "expected an unsigned value");
      return static_cast<std::uint64_t>(result);
    }
    const auto    decimal = formatValue(busValueFromBits(binaryDigits(value, context)),
                                        BusValueFormat::Unsigned);
    std::uint64_t result  = 0;
    const auto [end, error] =
        std::from_chars(decimal.data(), decimal.data() + decimal.size(), result);
    if (error == std::errc::result_out_of_range)
      fail(context, "binary value does not fit in 64 bits");
    if (error != std::errc{} || end != decimal.data() + decimal.size())
      fail(context, "expected a fully defined binary string");
    return result;
  }

  [[nodiscard]] bool isZeroParameter(const Json& value, const std::string_view context)
  {
    if (value.is_number_integer() || value.is_number_unsigned())
      return parseUnsigned(value, context) == 0;
    return std::ranges::all_of(binaryDigits(value, context),
                               [](const char digit) { return digit == '0'; });
  }

  [[nodiscard]] const Json& requireObjectMember(const Json&            object,
                                                const std::string_view key,
                                                const std::string_view context)
  {
    if (!object.is_object())
      fail(context, "expected an object");
    const auto it = object.find(key);
    if (it == object.end())
      fail(context, std::format("missing '{}'", key));
    return *it;
  }

  void requireExactMembers(const Json&                             object,
                           const std::span<const std::string_view> names,
                           const std::string_view                  context,
                           const std::string_view                  memberKind)
  {
    if (!object.is_object())
      fail(context, "expected an object");
    if (object.size() != names.size())
      fail(context, std::format("cell has missing or unexpected {}s", memberKind));
    for (const auto name : names)
      if (!object.contains(name))
        fail(context, std::format("missing '{}' {}", name, memberKind));
  }

  [[nodiscard]] Bus slice(const Bus& bus, const std::size_t offset,
                          const std::size_t width)
  {
    std::vector<Wire_ptr> wires;
    wires.reserve(width);
    for (std::size_t bit = 0; bit < width; ++bit)
      wires.push_back(bus[static_cast<unsigned short>(offset + bit)]);
    return Bus(std::move(wires));
  }

  [[nodiscard]] Bus concatenate(const Bus& lhs, const Bus& rhs)
  {
    auto       wires = static_cast<std::vector<Wire_ptr>>(lhs);
    const auto right = static_cast<std::vector<Wire_ptr>>(rhs);
    wires.insert(wires.end(), right.begin(), right.end());
    return Bus(std::move(wires));
  }

  class Importer {
  public:
    explicit Importer(Json design) : design(std::move(design)) {}

    [[nodiscard]] Circuit run(const std::optional<std::string_view> requestedModule)
    {
      const auto& modules = requireObjectMember(design, "modules", "design");
      if (!modules.is_object() || modules.empty())
        fail("design.modules", "expected at least one module");

      const auto [name, module] = selectModule(modules, requestedModule);
      moduleName                = name;
      importModule(module);

      Component_set componentSet(components.begin(), components.end());
      Circuit       result(componentSet, false);
      result.setName(moduleName);
      return result;
    }

  private:
    enum class ConnectionRole { Driver, Consumer };

    class Cell {
    public:
      Cell(Importer& importer, const Json& cell, std::string context)
        : context(std::move(context)),
          importer(importer),
          parametersJson(requireObjectMember(cell, "parameters", this->context)),
          connectionsJson(requireObjectMember(cell, "connections", this->context))
      {
        if (!cell.is_object())
          fail(this->context, "expected an object");
        const auto& typeJson = requireObjectMember(cell, "type", this->context);
        if (!typeJson.is_string())
          fail(std::format("{}.type", this->context), "expected a string");
        type = typeJson.get<std::string>();
        if (!parametersJson.is_object())
          fail(std::format("{}.parameters", this->context), "expected an object");
        if (!connectionsJson.is_object())
          fail(std::format("{}.connections", this->context), "expected an object");
      }

      [[nodiscard]] const std::string& cellType() const { return type; }
      [[nodiscard]] const std::string& where() const { return context; }

      void requireConnections(const std::initializer_list<std::string_view> names) const
      {
        requireExactMembers(connectionsJson, std::span(names.begin(), names.size()),
                            context, "connection");
      }

      template <std::size_t ParameterCount, std::size_t ConnectionCount>
      void requireSchema(
          const std::array<std::string_view, ParameterCount>&  parameterNames,
          const std::array<std::string_view, ConnectionCount>& connectionNames) const
      {
        requireExactMembers(parametersJson, parameterNames, context, "parameter");
        requireExactMembers(connectionsJson, connectionNames, context, "connection");
      }

      [[nodiscard]] const Json& parameter(const std::string_view name) const
      {
        return requireObjectMember(parametersJson, name, context);
      }

      // Exposes the original JSON connection for structural import passes. Normal
      // cell import should prefer consumer()/driver(), which also interns wires and
      // enforces the single-driver rule.
      [[nodiscard]] const Json& connection(const std::string_view name) const
      {
        return requireObjectMember(connectionsJson, name, context);
      }

      [[nodiscard]] std::size_t width(const std::string_view name) const
      {
        const auto result =
            parseUnsigned(parameter(name), std::format("{}.{}", context, name));
        if (result == 0 || result > std::numeric_limits<unsigned short>::max())
          fail(context, std::format("'{}' is outside Silicon's supported width", name));
        return static_cast<std::size_t>(result);
      }

      [[nodiscard]] bool flag(const std::string_view name) const
      {
        const auto result =
            parseUnsigned(parameter(name), std::format("{}.{}", context, name));
        if (result > 1)
          fail(context, std::format("'{}' must be zero or one", name));
        return result == 1;
      }

      [[nodiscard]] bool zeroParameter(const std::string_view name) const
      {
        return isZeroParameter(parameter(name), std::format("{}.{}", context, name));
      }

      [[nodiscard]] Bus
      consumer(const std::string_view           port,
               const std::optional<std::size_t> expectedWidth = std::nullopt) const
      {
        return bus(port, ConnectionRole::Consumer, expectedWidth);
      }

      [[nodiscard]] Bus
      driver(const std::string_view           port,
             const std::optional<std::size_t> expectedWidth = std::nullopt) const
      {
        return bus(port, ConnectionRole::Driver, expectedWidth);
      }

      [[nodiscard]] Wire_ptr consumerBit(const std::string_view port) const
      {
        return consumer(port, 1)[0];
      }

      [[nodiscard]] Wire_ptr driverBit(const std::string_view port) const
      {
        return driver(port, 1)[0];
      }

      [[nodiscard]] bool rawConstant(const std::string_view port,
                                     const std::string_view value,
                                     const std::size_t      expectedWidth = 1) const
      {
        const auto& bits = requireObjectMember(connectionsJson, port, context);
        if (!bits.is_array() || bits.empty() || bits.size() != expectedWidth)
          fail(context, std::format("'{}' must be {} bit{} wide", port, expectedWidth,
                                    expectedWidth == 1 ? "" : "s"));
        return std::ranges::all_of(bits, [value](const Json& bit) {
          return bit.is_string() && bit.get_ref<const std::string&>() == value;
        });
      }

      [[nodiscard]] Wire_ptr optionalActiveHighBit(const std::string_view port) const
      {
        return rawConstant(port, "0") ? nullptr : consumerBit(port);
      }

    private:
      std::string context;
      Importer&   importer;
      const Json& parametersJson;
      const Json& connectionsJson;
      std::string type;

      [[nodiscard]] Bus bus(const std::string_view port, const ConnectionRole role,
                            const std::optional<std::size_t> expectedWidth) const
      {
        return importer.readBus(requireObjectMember(connectionsJson, port, context), role,
                                std::format("{}.{}", context, port), expectedWidth);
      }
    };

    Json                                         design;
    std::string                                  moduleName;
    std::map<std::uint64_t, Wire_ptr>            signals;
    std::map<State, Wire_ptr>                    constants;
    std::map<BusValue, Bus>                      constantBuses;
    std::set<std::uint64_t>                      drivenSignals;
    std::vector<Component_ptr>                   components;

    [[nodiscard]] std::pair<std::string, const Json&>
    selectModule(const Json&                           modules,
                 const std::optional<std::string_view> requested) const
    {
      if (requested) {
        const auto it = modules.find(*requested);
        if (it == modules.end())
          fail("design.modules", std::format("module '{}' was not found", *requested));
        return {std::string(*requested), *it};
      }

      if (modules.size() == 1)
        return {modules.begin().key(), modules.begin().value()};

      const Json* top = nullptr;
      std::string topName;
      for (const auto& [name, module] : modules.items()) {
        if (!module.is_object())
          fail(std::format("design.modules.{}", name), "expected an object");
        const auto attributes = module.find("attributes");
        if (attributes == module.end() || !attributes->is_object())
          continue;
        const auto topAttribute = attributes->find("top");
        if (topAttribute == attributes->end()
            || parseUnsigned(*topAttribute,
                             std::format("design.modules.{}.attributes.top", name))
                   == 0) {
          continue;
        }
        if (top)
          fail("design.modules", "more than one module is marked as top");
        top     = &module;
        topName = name;
      }
      if (!top)
        fail("design.modules", "module selection is ambiguous and no top is marked");
      return {std::move(topName), *top};
    }

    void importModule(const Json& module)
    {
      if (!module.is_object())
        fail(std::format("design.modules.{}", moduleName), "expected an object");

      if (const auto memories = module.find("memories");
          memories != module.end() && (!memories->is_object() || !memories->empty())) {
        fail(moduleContext(), "memories are not supported");
      }

      importPorts(requireObjectMember(module, "ports", moduleContext()));
      importCells(requireObjectMember(module, "cells", moduleContext()));
    }

    [[nodiscard]] std::string moduleContext() const
    {
      return std::format("design.modules.{}", moduleName);
    }

    [[nodiscard]] Wire_ptr numericWire(const std::uint64_t signal)
    {
      auto& wire = signals[signal];
      if (!wire)
        wire = std::make_shared<Wire>(State::UNKNOWN);
      return wire;
    }

    [[nodiscard]] Wire_ptr constantWire(const std::string_view value,
                                        const std::string_view context)
    {
      if (value == "z")
        fail(context, "high-impedance 'z' has no Silicon equivalent");
      if (value != "0" && value != "1" && value != "x")
        fail(context, std::format("unsupported signal literal '{}'", value));

      const auto state = busValueFromBits(value).front();
      auto&      wire  = constants[state];
      if (!wire) {
        wire = std::make_shared<Wire>(state);
        components.push_back(std::make_shared<ConstantComponent>(wire, BusValue{state}));
      }
      return wire;
    }

    [[nodiscard]] Bus constantBus(const Json& bits, const std::string_view context)
    {
      std::string rawValue;
      rawValue.reserve(bits.size());
      for (std::size_t bit = bits.size(); bit > 0; --bit) {
        const auto& digitJson = bits[bit - 1];
        if (!digitJson.is_string())
          fail(context, "expected a fully literal bus");
        const auto& digit = digitJson.get_ref<const std::string&>();
        if (digit == "z")
          fail(context, "high-impedance 'z' has no Silicon equivalent");
        if (digit != "0" && digit != "1" && digit != "x")
          fail(context, std::format("unsupported signal literal '{}'", digit));
        rawValue += digit;
      }
      const BusValue value = busValueFromBits(rawValue);

      auto [it, inserted] = constantBuses.try_emplace(value);
      if (inserted) {
        it->second = Bus(static_cast<unsigned short>(bits.size()));
        components.push_back(std::make_shared<ConstantComponent>(it->second, value));
      }
      return it->second;
    }

    [[nodiscard]] Bus
    readBus(const Json& bits, const ConnectionRole role, const std::string_view context,
            const std::optional<std::size_t> expectedWidth = std::nullopt)
    {
      if (!bits.is_array() || bits.empty())
        fail(context, "expected a non-empty bit array");
      if (expectedWidth && bits.size() != *expectedWidth)
        fail(context, std::format("expected {} bit{}", *expectedWidth,
                                  *expectedWidth == 1 ? "" : "s"));

      const bool fullyLiteralConsumer =
          role == ConnectionRole::Consumer && bits.size() > 1
          && std::ranges::all_of(bits, [](const Json& bit) { return bit.is_string(); });
      if (fullyLiteralConsumer)
        return constantBus(bits, context);

      std::vector<Wire_ptr> wires;
      wires.reserve(bits.size());
      for (std::size_t index = 0; index < bits.size(); ++index) {
        const auto& bit        = bits[index];
        const auto  bitContext = std::format("{}[{}]", context, index);
        if (bit.is_number_integer() || bit.is_number_unsigned()) {
          const auto signal = parseUnsigned(bit, bitContext);
          if (role == ConnectionRole::Driver && !drivenSignals.insert(signal).second)
            fail(bitContext, std::format("signal {} has more than one driver", signal));
          wires.push_back(numericWire(signal));
          continue;
        }
        if (role == ConnectionRole::Driver || !bit.is_string())
          fail(bitContext, role == ConnectionRole::Consumer
                               ? "expected a signal number or literal"
                               : "expected a numeric signal");
        wires.push_back(constantWire(bit.get_ref<const std::string&>(), bitContext));
      }
      return Bus(std::move(wires));
    }

    void importPorts(const Json& ports)
    {
      if (!ports.is_object())
        fail(std::format("{}.ports", moduleContext()), "expected an object");

      for (const auto& [name, port] : ports.items()) {
        const auto context = std::format("{}.ports.{}", moduleContext(), name);
        if (!port.is_object())
          fail(context, "expected an object");
        const auto& directionJson = requireObjectMember(port, "direction", context);
        const auto& bitsJson      = requireObjectMember(port, "bits", context);
        if (!directionJson.is_string())
          fail(std::format("{}.direction", context), "expected a string");
        const auto direction = directionJson.get<std::string>();

        if (direction == "inout")
          fail(context, "inout ports are not supported");
        if (direction != "input" && direction != "output")
          fail(context, std::format("invalid port direction '{}'", direction));

        if (direction == "input") {
          const Bus bus =
              readBus(bitsJson, ConnectionRole::Driver, std::format("{}.bits", context));
          if (bus.size() == 1)
            components.push_back(std::make_shared<DummyInputComponent>(bus, name));
          else
            components.push_back(std::make_shared<DummyBusInputComponent>(bus, name));
        } else {
          // Yosys flattens a zero-extension assignment into a cell-free output
          // connection whose most-significant bits are literal zeroes. Restore the
          // word-level component when the remaining least-significant bits are all
          // signals; mixed concatenations keep using the generic connection path.
          Bus         bus;
          std::size_t inputWidth = bitsJson.size();
          while (inputWidth > 0 && bitsJson[inputWidth - 1].is_string()
                 && bitsJson[inputWidth - 1].get_ref<const std::string&>() == "0") {
            --inputWidth;
          }

          const bool isUnsignedExtension =
              inputWidth > 0 && inputWidth < bitsJson.size()
              && std::ranges::all_of(
                  bitsJson.begin(), bitsJson.begin() + inputWidth, [](const Json& bit) {
                    return bit.is_number_integer() || bit.is_number_unsigned();
                  });
          if (isUnsignedExtension) {
            Json inputBits = Json::array();
            for (std::size_t bit = 0; bit < inputWidth; ++bit)
              inputBits.push_back(bitsJson[bit]);

            const Bus input = readBus(inputBits, ConnectionRole::Consumer,
                                      std::format("{}.bits", context));
            bus             = Bus(static_cast<unsigned short>(bitsJson.size()));
            components.push_back(std::make_shared<Extender>(
                input, bus, std::string(Extender::UnsignedMode)));
          } else {
            bus = readBus(bitsJson, ConnectionRole::Consumer,
                          std::format("{}.bits", context));
          }

          if (bus.size() == 1)
            components.push_back(std::make_shared<DummyOutputComponent>(bus, name));
          else
            components.push_back(std::make_shared<DummyBusOutputComponent>(bus, name));
        }
      }
    }

    void importCells(const Json& cells)
    {
      if (!cells.is_object())
        fail(std::format("{}.cells", moduleContext()), "expected an object");

      for (const auto& [name, cell] : cells.items())
        importCell(name, cell);
    }

    [[nodiscard]] static std::vector<Bus>
    split(const Bus& packed, const std::size_t laneWidth, const std::size_t laneCount)
    {
      std::vector<Bus> lanes;
      lanes.reserve(laneCount);
      for (std::size_t lane = 0; lane < laneCount; ++lane)
        lanes.push_back(slice(packed, lane * laneWidth, laneWidth));
      return lanes;
    }

    [[nodiscard]] static std::size_t laneCount(const std::size_t      selectionWidth,
                                               const std::string_view type,
                                               const std::string_view context)
    {
      if (selectionWidth >= std::numeric_limits<std::size_t>::digits)
        fail(context, std::format("{} selection width is too large", type));
      return std::size_t{1} << selectionWidth;
    }

    void connectAndAdd(Component_ptr component, std::vector<Bus> inputs,
                       std::vector<Bus> outputs)
    {
      component->setInputs(inputs);
      component->setOutputs(outputs);
      components.push_back(std::move(component));
    }

    void addWithZeroDelay(Component_ptr          component,
                          const std::string_view property = "delay")
    {
      component->setProperty(property, 0);
      components.push_back(std::move(component));
    }

    template <typename ComponentType>
    void addMuxLike(const std::size_t selectionWidth, const std::size_t busWidth,
                    std::vector<Bus> inputs, std::vector<Bus> outputs)
    {
      auto component = std::make_shared<ComponentType>();
      component->setProperty("selectionSize", static_cast<int>(selectionWidth));
      component->setProperty("busSize", static_cast<int>(busWidth));
      component->setProperty("delay", 0);
      connectAndAdd(std::move(component), std::move(inputs), std::move(outputs));
    }

    template <typename FlipFlopType>
    void addFlipFlop(std::shared_ptr<FlipFlopType> flipFlop, const bool positiveClock)
    {
      flipFlop->setProperty("triggerEdge",
                            positiveClock ? std::string("PET") : std::string("NET"));
      addWithZeroDelay(std::move(flipFlop), "propagationDelay");
    }

    [[nodiscard]] std::pair<Bus, Bus> equalWidthUnary(const Cell&            cell,
                                                      const std::string_view type)
    {
      cell.requireConnections({"A", "Y"});
      const auto aWidth = cell.width("A_WIDTH");
      const auto yWidth = cell.width("Y_WIDTH");
      Bus        a      = cell.consumer("A", aWidth);
      Bus        y      = cell.driver("Y", yWidth);
      if (aWidth != yWidth)
        fail(cell.where(),
             std::format("{} requires equal input and output widths", type));
      return {std::move(a), std::move(y)};
    }

    template <typename GateType> void importBinaryGate(const Cell& cell)
    {
      cell.requireConnections({"A", "B", "Y"});
      const auto aWidth = cell.width("A_WIDTH");
      const auto bWidth = cell.width("B_WIDTH");
      const auto yWidth = cell.width("Y_WIDTH");
      const Bus  a      = cell.consumer("A", aWidth);
      const Bus  b      = cell.consumer("B", bWidth);
      const Bus  y      = cell.driver("Y", yWidth);
      if (aWidth != bWidth || aWidth != yWidth)
        fail(cell.where(), "binary gate widths must match its connections");

      auto gate = std::make_shared<GateType>();
      gate->setProperty("bitwise", true);
      gate->setProperty("size", static_cast<int>(yWidth));
      gate->setProperty("delay", 0);
      connectAndAdd(std::move(gate), {a, b}, {y});
    }

    template <typename GateType> void importFineBinaryGate(const Cell& cell)
    {
      cell.requireSchema(std::array<std::string_view, 0>{},
                         std::array<std::string_view, 3>{"A", "B", "Y"});
      const Bus a = cell.consumer("A", 1);
      const Bus b = cell.consumer("B", 1);
      const Bus y = cell.driver("Y", 1);

      auto gate = std::make_shared<GateType>(std::vector<Wire_ptr>{a[0], b[0]}, y[0]);
      addWithZeroDelay(std::move(gate));
    }

    void importNot(const Cell& cell)
    {
      const auto [a, y] = equalWidthUnary(cell, "$not");
      for (std::size_t bit = 0; bit < y.size(); ++bit) {
        auto gate = std::make_shared<NotGate>(a[static_cast<unsigned short>(bit)],
                                              y[static_cast<unsigned short>(bit)]);
        addWithZeroDelay(std::move(gate));
      }
    }

    void importPos(const Cell& cell)
    {
      cell.requireConnections({"A", "Y"});
      const auto aWidth = cell.width("A_WIDTH");
      const auto yWidth = cell.width("Y_WIDTH");
      const Bus  a      = cell.consumer("A", aWidth);
      const Bus  y      = cell.driver("Y", yWidth);

      if (aWidth != yWidth) {
        components.push_back(std::make_shared<Extender>(
            a, y,
            std::string(cell.flag("A_SIGNED") ? Extender::SignedMode
                                              : Extender::UnsignedMode)));
        return;
      }

      auto merger = std::make_shared<WireMerger>();
      merger->setProperty("size", static_cast<int>(y.size()));
      connectAndAdd(std::move(merger), split(a, 1, a.size()), {y});
    }

    void importAdd(const Cell& cell)
    {
      cell.requireConnections({"A", "B", "Y"});
      if (cell.flag("A_SIGNED") || cell.flag("B_SIGNED"))
        fail(cell.where(), "signed $add cells are not supported");
      const auto aWidth = cell.width("A_WIDTH");
      const auto bWidth = cell.width("B_WIDTH");
      const auto yWidth = cell.width("Y_WIDTH");
      const Bus  a      = cell.consumer("A", aWidth);
      const Bus  b      = cell.consumer("B", bWidth);
      const Bus  y      = cell.driver("Y", yWidth);

      if (aWidth == bWidth && (yWidth == aWidth || yWidth == aWidth + 1)) {
        const Bus  sum   = slice(y, 0, aWidth);
        const auto carry = yWidth == aWidth + 1 ? y[static_cast<unsigned short>(aWidth)]
                                                : std::make_shared<Wire>(State::UNKNOWN);
        auto adder = std::make_shared<AdderNBits>(std::array<Bus, 2>{a, b}, sum, carry);
        addWithZeroDelay(std::move(adder));
        return;
      }

      const auto arithmeticWidth = std::max({aWidth, bWidth, yWidth});
      const Bus  extendedA       = resizeArithmeticOperand(a, arithmeticWidth, false);
      const Bus  extendedB       = resizeArithmeticOperand(b, arithmeticWidth, false);

      auto sumWires = static_cast<std::vector<Wire_ptr>>(y);
      while (sumWires.size() < arithmeticWidth)
        sumWires.push_back(std::make_shared<Wire>(State::UNKNOWN));

      auto adder = std::make_shared<AdderNBits>(std::array<Bus, 2>{extendedA, extendedB},
                                                Bus(std::move(sumWires)),
                                                std::make_shared<Wire>(State::UNKNOWN));
      addWithZeroDelay(std::move(adder));
    }

    [[nodiscard]] Bus resizeArithmeticOperand(const Bus& operand, const std::size_t width,
                                              const bool signExtend)
    {
      if (operand.size() > width)
        return slice(operand, 0, width);
      if (operand.size() == width)
        return operand;

      Bus  result(static_cast<unsigned short>(width));
      auto extender = std::make_shared<Extender>(
          operand, result,
          std::string(signExtend ? Extender::SignedMode : Extender::UnsignedMode));
      components.push_back(std::move(extender));
      return result;
    }

    void importSub(const Cell& cell)
    {
      cell.requireConnections({"A", "B", "Y"});
      const auto aWidth           = cell.width("A_WIDTH");
      const auto bWidth           = cell.width("B_WIDTH");
      const auto yWidth           = cell.width("Y_WIDTH");
      const bool signedArithmetic = cell.flag("A_SIGNED") && cell.flag("B_SIGNED");
      const Bus  b                = cell.consumer("B", bWidth);
      const Bus  y                = cell.driver("Y", yWidth);

      // A complementer is serialized as 0 - B. Recover it directly so repeated
      // Yosys round trips do not add a redundant zero-plus adder each time.
      if (cell.rawConstant("A", "0", aWidth)) {
        auto complementer = std::make_shared<Complementer>(
            resizeArithmeticOperand(b, yWidth, signedArithmetic), y);
        addWithZeroDelay(std::move(complementer));
        return;
      }

      const Bus a = cell.consumer("A", aWidth);

      // Yosys evaluates binary arithmetic at the widest operand/result width, sign
      // extending only when both operands are signed, and then truncates to Y_WIDTH.
      const auto arithmeticWidth = std::max({aWidth, bWidth, yWidth});
      const Bus extendedA = resizeArithmeticOperand(a, arithmeticWidth, signedArithmetic);
      const Bus extendedB = resizeArithmeticOperand(b, arithmeticWidth, signedArithmetic);

      Bus  complementedB(static_cast<unsigned short>(arithmeticWidth));
      auto complementer = std::make_shared<Complementer>(extendedB, complementedB);
      addWithZeroDelay(std::move(complementer));

      auto sumWires = static_cast<std::vector<Wire_ptr>>(y);
      while (sumWires.size() < arithmeticWidth)
        sumWires.push_back(std::make_shared<Wire>(State::UNKNOWN));
      Bus sum(std::move(sumWires));

      auto adder =
          std::make_shared<AdderNBits>(std::array<Bus, 2>{extendedA, complementedB}, sum,
                                       std::make_shared<Wire>(State::UNKNOWN));
      addWithZeroDelay(std::move(adder));
    }

    void importMux(const Cell& cell)
    {
      cell.requireConnections({"A", "B", "S", "Y"});
      const auto width = cell.width("WIDTH");
      const Bus  a     = cell.consumer("A", width);
      const Bus  b     = cell.consumer("B", width);
      const Bus  s     = cell.consumer("S", 1);
      const Bus  y     = cell.driver("Y", width);

      std::vector<Bus> inputs =
          width == 1 ? std::vector<Bus>{concatenate(a, b), s} : std::vector<Bus>{a, b, s};
      addMuxLike<Multiplexer>(1, width, std::move(inputs), {y});
    }

    void importBmux(const Cell& cell)
    {
      cell.requireConnections({"A", "S", "Y"});
      const auto width          = cell.width("WIDTH");
      const auto selectionWidth = cell.width("S_WIDTH");
      const auto lanes          = laneCount(selectionWidth, "$bmux", cell.where());
      if (lanes > std::numeric_limits<std::size_t>::max() / width)
        fail(cell.where(), "$bmux packed input width is too large");
      const Bus a = cell.consumer("A", width * lanes);
      const Bus s = cell.consumer("S", selectionWidth);
      const Bus y = cell.driver("Y", width);

      std::vector<Bus> inputs;
      if (width == 1) {
        inputs = {a, s};
      } else {
        inputs = split(a, width, lanes);
        inputs.push_back(s);
      }
      addMuxLike<Multiplexer>(selectionWidth, width, std::move(inputs), {y});
    }

    void importDemux(const Cell& cell)
    {
      cell.requireConnections({"A", "S", "Y"});
      const auto width          = cell.width("WIDTH");
      const auto selectionWidth = cell.width("S_WIDTH");
      const auto lanes          = laneCount(selectionWidth, "$demux", cell.where());
      if (lanes > std::numeric_limits<std::size_t>::max() / width)
        fail(cell.where(), "$demux packed output width is too large");
      const Bus a = cell.consumer("A", width);
      const Bus s = cell.consumer("S", selectionWidth);
      const Bus y = cell.driver("Y", width * lanes);

      if (width == 1 && cell.rawConstant("A", "1")) {
        auto decoder = std::make_shared<Decoder>(a, s, y);
        decoder->setProperty("delay", 0);
        components.push_back(std::move(decoder));
        return;
      }

      auto outputs = width == 1 ? std::vector<Bus>{y} : split(y, width, lanes);
      addMuxLike<Demultiplexer>(selectionWidth, width, {a, s}, std::move(outputs));
    }

    void validateScalarStorage(const Cell&                                   cell,
                               const std::initializer_list<std::string_view> ports)
    {
      cell.requireConnections(ports);
      if (cell.width("WIDTH") != 1)
        fail(cell.where(), "only scalar flip-flop cells are supported");
      if (!cell.flag("SET_POLARITY") || !cell.flag("CLR_POLARITY"))
        fail(cell.where(), "only active-high SET and CLR are supported");
    }

    void importDffsr(const Cell& cell)
    {
      validateScalarStorage(cell, {"CLK", "SET", "CLR", "D", "Q"});
      auto dff = std::make_shared<DFlipFlop>(
          cell.consumerBit("D"), cell.consumerBit("CLK"), cell.consumerBit("CLR"),
          cell.consumerBit("SET"), cell.driverBit("Q"), std::make_shared<Wire>());
      addFlipFlop(std::move(dff), cell.flag("CLK_POLARITY"));
    }

    void importDff(const Cell& cell)
    {
      cell.requireConnections({"CLK", "D", "Q"});
      if (cell.width("WIDTH") != 1)
        fail(cell.where(), "only scalar flip-flop cells are supported");

      auto dff = std::make_shared<DFlipFlop>(
          cell.consumerBit("D"), cell.consumerBit("CLK"), nullptr, nullptr,
          cell.driverBit("Q"), std::make_shared<Wire>());
      addFlipFlop(std::move(dff), cell.flag("CLK_POLARITY"));
    }

    void importDffsre(const Cell& cell)
    {
      validateScalarStorage(cell, {"CLK", "EN", "SET", "CLR", "D", "Q"});
      if (!cell.flag("EN_POLARITY"))
        fail(cell.where(), "only active-high EN is supported");
      auto dff = std::make_shared<EFlipFlop>(
          cell.consumerBit("D"), cell.consumerBit("EN"), cell.consumerBit("CLK"),
          cell.consumerBit("CLR"), cell.consumerBit("SET"), cell.driverBit("Q"),
          std::make_shared<Wire>());
      addFlipFlop(std::move(dff), cell.flag("CLK_POLARITY"));
    }

    void importDlatch(const Cell& cell)
    {
      cell.requireConnections({"EN", "D", "Q"});
      if (cell.width("WIDTH") != 1)
        fail(cell.where(), "only scalar D latch cells are supported");
      if (!cell.flag("EN_POLARITY"))
        fail(cell.where(), "only active-high EN is supported");

      auto latch =
          std::make_shared<DLatch>(cell.consumerBit("D"), cell.consumerBit("EN"),
                                   cell.driverBit("Q"), std::make_shared<Wire>());
      addWithZeroDelay(std::move(latch), "propagationDelay");
    }

    void importAdffe(const Cell& cell)
    {
      cell.requireConnections({"CLK", "ARST", "EN", "D", "Q"});
      const auto width = cell.width("WIDTH");
      if (!cell.flag("EN_POLARITY") || !cell.flag("ARST_POLARITY"))
        fail(cell.where(), "only active-high EN and ARST are supported");
      if (!cell.zeroParameter("ARST_VALUE"))
        fail(cell.where(), "only an all-zero asynchronous reset value is supported");

      const auto clock  = cell.consumerBit("CLK");
      const auto reset  = cell.consumerBit("ARST");
      const auto enable = cell.consumerBit("EN");
      const Bus  data   = cell.consumer("D", width);
      const Bus  q      = cell.driver("Q", width);

      const bool positiveClock = cell.flag("CLK_POLARITY");
      if (width == 1) {
        auto dff = std::make_shared<EFlipFlop>(data[0], enable, clock, reset, nullptr,
                                               q[0], std::make_shared<Wire>());
        addFlipFlop(std::move(dff), positiveClock);
        return;
      }
      if (!positiveClock)
        fail(cell.where(), "multi-bit registers require a positive-edge clock");

      addWithZeroDelay(std::make_shared<Register>(data, clock, enable, reset, q));
    }

    template <bool Enabled, bool SetReset> void importSiliconDff(const Cell& cell)
    {
      static constexpr auto parameterNames = [] {
        std::array<std::string_view, 1 + Enabled + 2 * SetReset> names{};
        std::size_t                                              index = 0;
        names[index++]                                                 = "CLK_POLARITY";
        if constexpr (Enabled)
          names[index++] = "EN_POLARITY";
        if constexpr (SetReset) {
          names[index++] = "SET_POLARITY";
          names[index]   = "CLR_POLARITY";
        }
        return names;
      }();
      static constexpr auto connectionNames = [] {
        std::array<std::string_view, 4 + Enabled + 2 * SetReset> names{};
        std::size_t                                              index = 0;
        names[index++]                                                 = "D";
        if constexpr (Enabled)
          names[index++] = "EN";
        names[index++] = "CLK";
        if constexpr (SetReset) {
          names[index++] = "SET";
          names[index++] = "CLR";
        }
        names[index++] = "Q";
        names[index]   = "QN";
        return names;
      }();
      cell.requireSchema(parameterNames, connectionNames);

      if constexpr (Enabled) {
        if (!cell.flag("EN_POLARITY"))
          fail(cell.where(), "SILICON enabled flip-flops require active-high EN");
      }
      if constexpr (SetReset) {
        if (!cell.flag("SET_POLARITY") || !cell.flag("CLR_POLARITY"))
          fail(cell.where(), "SILICON flip-flops require active-high SET and CLR");
      }

      const bool positive = cell.flag("CLK_POLARITY");
      const auto d        = cell.consumerBit("D");
      const auto clock    = cell.consumerBit("CLK");
      const auto enable   = [&] {
        if constexpr (Enabled)
          return cell.consumerBit("EN");
        return Wire_ptr{};
      }();
      const auto set = [&] {
        if constexpr (SetReset)
          return cell.optionalActiveHighBit("SET");
        return Wire_ptr{};
      }();
      const auto clear = [&] {
        if constexpr (SetReset)
          return cell.optionalActiveHighBit("CLR");
        return Wire_ptr{};
      }();
      const auto q  = cell.driverBit("Q");
      const auto qn = cell.driverBit("QN");

      if constexpr (Enabled)
        addFlipFlop(std::make_shared<EFlipFlop>(d, enable, clock, clear, set, q, qn),
                    positive);
      else
        addFlipFlop(std::make_shared<DFlipFlop>(d, clock, clear, set, q, qn), positive);
    }

    void importSiliconJkff(const Cell& cell)
    {
      static constexpr auto parameters = std::to_array<std::string_view>(
          {"CLK_POLARITY", "SET_POLARITY", "CLR_POLARITY"});
      static constexpr auto connections =
          std::to_array<std::string_view>({"J", "K", "CLK", "SET", "CLR", "Q", "QN"});
      cell.requireSchema(parameters, connections);
      if (!cell.flag("SET_POLARITY") || !cell.flag("CLR_POLARITY"))
        fail(cell.where(), "SILICON_JKFF requires active-high SET and CLR");

      auto flipFlop = std::make_shared<JKFlipFlop>(
          cell.consumerBit("J"), cell.consumerBit("K"), cell.consumerBit("CLK"),
          cell.optionalActiveHighBit("CLR"), cell.optionalActiveHighBit("SET"),
          cell.driverBit("Q"), cell.driverBit("QN"));
      addFlipFlop(std::move(flipFlop), cell.flag("CLK_POLARITY"));
    }

    void importSiliconDlatch(const Cell& cell)
    {
      static constexpr auto parameters = std::to_array<std::string_view>({"EN_POLARITY"});
      static constexpr auto connections =
          std::to_array<std::string_view>({"D", "EN", "Q", "QN"});
      cell.requireSchema(parameters, connections);
      if (!cell.flag("EN_POLARITY"))
        fail(cell.where(), "SILICON_DLATCH requires active-high EN");

      auto latch = std::make_shared<DLatch>(cell.consumerBit("D"), cell.consumerBit("EN"),
                                            cell.driverBit("Q"), cell.driverBit("QN"));
      addWithZeroDelay(std::move(latch), "propagationDelay");
    }

    void importSiliconHalfAdder(const Cell& cell)
    {
      static constexpr std::array<std::string_view, 0> parameters{};
      static constexpr auto                            connections =
          std::to_array<std::string_view>({"A", "B", "SUM", "COUT"});
      cell.requireSchema(parameters, connections);
      auto adder = std::make_shared<HalfAdder>(
          std::array<Wire_ptr, 2>{cell.consumerBit("A"), cell.consumerBit("B")},
          cell.driverBit("SUM"), cell.driverBit("COUT"));
      addWithZeroDelay(std::move(adder));
    }

    void importSiliconFullAdder(const Cell& cell)
    {
      static constexpr std::array<std::string_view, 0> parameters{};
      static constexpr auto                            connections =
          std::to_array<std::string_view>({"A", "B", "CIN", "SUM", "COUT"});
      cell.requireSchema(parameters, connections);
      auto adder = std::make_shared<FullAdder>(
          std::array<Wire_ptr, 2>{cell.consumerBit("A"), cell.consumerBit("B")},
          cell.consumerBit("CIN"), cell.driverBit("SUM"), cell.driverBit("COUT"));
      addWithZeroDelay(std::move(adder));
    }

    void importSiliconAdder(const Cell& cell)
    {
      static constexpr auto parameters =
          std::to_array<std::string_view>({"WIDTH", "A_SIGNED", "B_SIGNED"});
      static constexpr auto connections =
          std::to_array<std::string_view>({"A", "B", "SUM", "COUT"});
      cell.requireSchema(parameters, connections);
      const auto width = cell.width("WIDTH");
      if (cell.flag("A_SIGNED") || cell.flag("B_SIGNED"))
        fail(cell.where(), "SILICON_ADDER supports unsigned operands only");

      const auto a    = cell.consumer("A", width);
      const auto b    = cell.consumer("B", width);
      const auto sum  = cell.driver("SUM", width);
      const auto cout = cell.driverBit("COUT");
      auto adder      = std::make_shared<AdderNBits>(std::array<Bus, 2>{a, b}, sum, cout);
      addWithZeroDelay(std::move(adder));
    }

    template <bool ParallelInput, bool ParallelOutput>
    void importSiliconRegister(const Cell& cell)
    {
      if constexpr (ParallelInput && !ParallelOutput) {
        static constexpr auto parameters = std::to_array<std::string_view>(
            {"WIDTH", "CLK_POLARITY", "EN_POLARITY", "CLR_POLARITY", "LOAD_POLARITY"});
        static constexpr auto connections =
            std::to_array<std::string_view>({"DATA", "CLK", "EN", "CLR", "LOAD", "OUT"});
        cell.requireSchema(parameters, connections);
      } else {
        static constexpr auto parameters = std::to_array<std::string_view>(
            {"WIDTH", "CLK_POLARITY", "EN_POLARITY", "CLR_POLARITY"});
        static constexpr auto connections =
            std::to_array<std::string_view>({"DATA", "CLK", "EN", "CLR", "OUT"});
        cell.requireSchema(parameters, connections);
      }

      const auto width = cell.width("WIDTH");
      if (width <= 1)
        fail(cell.where(),
             std::format("{} WIDTH must be greater than one", cell.cellType()));
      if (!cell.flag("CLK_POLARITY") || !cell.flag("EN_POLARITY")
          || !cell.flag("CLR_POLARITY")) {
        fail(cell.where(),
             std::format("{} supports positive polarities only", cell.cellType()));
      }
      if constexpr (ParallelInput && !ParallelOutput) {
        if (!cell.flag("LOAD_POLARITY"))
          fail(cell.where(),
               std::format("{} supports positive polarities only", cell.cellType()));
      }

      std::vector<Bus> inputs{cell.consumer("DATA", ParallelInput ? width : 1),
                              Bus{cell.consumerBit("CLK")}, Bus{cell.consumerBit("EN")},
                              Bus{cell.consumerBit("CLR")}};
      if constexpr (ParallelInput && !ParallelOutput)
        inputs.emplace_back(Bus{cell.consumerBit("LOAD")});
      std::vector<Bus> outputs{cell.driver("OUT", ParallelOutput ? width : 1)};

      auto reg = std::make_shared<Register>();
      reg->setProperty("size", static_cast<int>(width));
      reg->setProperty("inputType", std::string(ParallelInput ? Register::ParallelType
                                                              : Register::SerialType));
      reg->setProperty("outputType", std::string(ParallelOutput ? Register::ParallelType
                                                                : Register::SerialType));
      reg->setProperty("delay", 0);
      connectAndAdd(std::move(reg), std::move(inputs), std::move(outputs));
    }

    void importCell(const std::string_view name, const Json& cell)
    {
      using CellHandler = void (Importer::*)(const Cell&);
      static constexpr auto handlers =
          std::to_array<std::pair<std::string_view, CellHandler>>({
              {cells::Dff, &Importer::importSiliconDff<false, false>},
              {cells::Dffe, &Importer::importSiliconDff<true, false>},
              {cells::Dlatch, &Importer::importSiliconDlatch},
              {cells::Dffsr, &Importer::importSiliconDff<false, true>},
              {cells::Dffsre, &Importer::importSiliconDff<true, true>},
              {cells::Jkff, &Importer::importSiliconJkff},
              {cells::HalfAdder, &Importer::importSiliconHalfAdder},
              {cells::FullAdder, &Importer::importSiliconFullAdder},
              {cells::Adder, &Importer::importSiliconAdder},
              {cells::Pipo, &Importer::importSiliconRegister<true, true>},
              {cells::Piso, &Importer::importSiliconRegister<true, false>},
              {cells::Sipo, &Importer::importSiliconRegister<false, true>},
              {cells::Siso, &Importer::importSiliconRegister<false, false>},
              {"$and", &Importer::importBinaryGate<AndGate>},
              {"$or", &Importer::importBinaryGate<OrGate>},
              {"$xor", &Importer::importBinaryGate<XorGate>},
              {"$not", &Importer::importNot},
              {"$_NAND_", &Importer::importFineBinaryGate<NandGate>},
              {"$_NOR_", &Importer::importFineBinaryGate<NorGate>},
              {"$pos", &Importer::importPos},
              {"$add", &Importer::importAdd},
              {"$sub", &Importer::importSub},
              {"$mux", &Importer::importMux},
              {"$bmux", &Importer::importBmux},
              {"$demux", &Importer::importDemux},
              {"$dff", &Importer::importDff},
              {"$dffsr", &Importer::importDffsr},
              {"$dffsre", &Importer::importDffsre},
              {"$dlatch", &Importer::importDlatch},
              {"$adffe", &Importer::importAdffe},
          });

      Cell view(*this, cell, std::format("{}.cells.{}", moduleContext(), name));
      for (const auto& [type, handler] : handlers) {
        if (view.cellType() == type)
          return (this->*handler)(view);
      }

      if (view.cellType().starts_with("SILICON_"))
        fail(view.where(), std::format("unsupported SILICON technology cell type '{}'",
                                       view.cellType()));
      fail(view.where(), std::format("unsupported cell type '{}'", view.cellType()));
    }
  };

}  // namespace

Circuit deserialize(const std::string_view                json,
                    const std::optional<std::string_view> moduleName)
{
  try {
    return Importer(Json::parse(json)).run(moduleName);
  } catch (const nlohmann::json::exception& error) {
    throw std::runtime_error(std::format("Invalid Yosys JSON: {}", error.what()));
  }
}

}  // namespace SILICON::yosys
