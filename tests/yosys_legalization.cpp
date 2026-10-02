#include "yosys_test_helpers.hpp"

namespace {
using SILICON::yosys::Json;
using SILICON::yosys::SerializationContext;

Json expression(const std::string& type, int aWidth, int bWidth, int yWidth,
                bool aSigned = false, bool bSigned = false)
{
  int next = 2;
  Json a = signalBits(next, aWidth);
  Json b = signalBits(next, bWidth);
  Json y = signalBits(next, yWidth);
  Json parameters{{"A_SIGNED", SerializationContext::parameter(aSigned, 1)},
                  {"A_WIDTH", SerializationContext::parameter(aWidth)},
                  {"Y_WIDTH", SerializationContext::parameter(yWidth)}};
  Json connections{{"A", a}, {"Y", y}};
  if (type != "$logic_not") {
    parameters["B_SIGNED"] = SerializationContext::parameter(bSigned, 1);
    parameters["B_WIDTH"] = SerializationContext::parameter(bWidth);
    connections["B"] = b;
  }
  return Json{{"modules",
               {{"top", {{"ports", Json::object()},
                          {"cells", {{"op", {{"type", type},
                                              {"parameters", parameters},
                                              {"connections", connections}}}}}}}}}};
}
}  // namespace

#ifdef SILICON_TEST_YOSYS_PLUGIN_AVAILABLE
namespace {
const Json& operation(const Json& design)
{
  return design.at("modules").at("top").at("cells").at("op");
}
int parameter(const Json& cell, const char* name)
{
  return std::stoi(cell.at("parameters").at(name).get<std::string>(), nullptr, 2);
}
}  // namespace

TEST(YosysLegalizationTest, LogicalPassPreservesWholeOperandTruthAndNarrowsResults)
{
  for (const auto& [type, mode] : std::array<std::pair<const char*, int>, 3>{
           {{"$logic_and", 0}, {"$logic_or", 1}, {"$logic_not", 2}}}) {
    for (int width : {1, 4}) {
      auto source = expression(type, width, 3, 3);
      const auto first = Json::parse(legalizeExpressions(source.dump(), "silicon_logic"));
      const auto& cell = operation(first);
      EXPECT_EQ(cell.at("type"), "SILICON_LOGIC");
      EXPECT_EQ(parameter(cell, "MODE"), mode);
      EXPECT_EQ(parameter(cell, "A_WIDTH"), width);
      EXPECT_EQ(cell.at("connections").at("A").size(), width);
      EXPECT_EQ(cell.at("connections").at("Y").size(), 1);
      EXPECT_EQ(operation(Json::parse(legalizeExpressions(first.dump(), "silicon_logic"))), cell);
      EXPECT_NO_THROW((void)SILICON::yosys::deserialize(first.dump()));
    }
  }
}

TEST(YosysLegalizationTest, ComparisonPassNormalizesWidthsModesAndSignedness)
{
  for (const auto& [type, mode] : std::array<std::pair<const char*, int>, 5>{
           {{"$eq", 0}, {"$lt", 1}, {"$le", 2}, {"$gt", 3}, {"$ge", 4}}}) {
    for (const auto& [aSigned, bSigned] : {std::pair{false, false},
                                         std::pair{true, true}, std::pair{true, false}}) {
      const auto first = Json::parse(legalizeExpressions(
          expression(type, 3, 5, 3, aSigned, bSigned).dump(), "silicon_compare"));
      const auto& cell = operation(first);
      EXPECT_EQ(cell.at("type"), "SILICON_COMPARE");
      EXPECT_EQ(parameter(cell, "WIDTH"), 5);
      EXPECT_EQ(parameter(cell, "MODE"), mode);
      EXPECT_EQ(parameter(cell, "SIGNED"), aSigned && bSigned);
      EXPECT_EQ(cell.at("connections").at("A").size(), 5);
      EXPECT_EQ(cell.at("connections").at("B").size(), 5);
      EXPECT_EQ(cell.at("connections").at("Y").size(), 1);
      EXPECT_EQ(cell.at("connections").at("A").at(3),
                aSigned && bSigned ? cell.at("connections").at("A").at(2) : Json("0"));
      EXPECT_EQ(operation(Json::parse(legalizeExpressions(first.dump(), "silicon_compare"))), cell);
      EXPECT_NO_THROW((void)SILICON::yosys::deserialize(first.dump()));
    }
  }
}

TEST(YosysLegalizationTest, ShiftPassNormalizesWidthDirectionAndArithmeticMode)
{
  for (const auto& [type, direction] : std::array<std::pair<const char*, int>, 3>{
           {{"$shl", 0}, {"$shr", 1}, {"$sshr", 1}}}) {
    for (bool sign : {false, true}) {
      for (const auto& [aWidth, yWidth] : {std::pair{3, 5}, std::pair{5, 3}}) {
        const auto first = Json::parse(legalizeExpressions(
            expression(type, aWidth, 2, yWidth, sign).dump(), "silicon_shift"));
        const auto& cell = operation(first);
        EXPECT_EQ(cell.at("type"), "SILICON_SHIFT");
        EXPECT_EQ(parameter(cell, "WIDTH"), yWidth);
        EXPECT_EQ(parameter(cell, "DIRECTION"), direction);
        EXPECT_EQ(parameter(cell, "ARITHMETIC"), type == std::string("$sshr") && sign);
        EXPECT_EQ(cell.at("connections").at("A").size(), yWidth);
        EXPECT_EQ(cell.at("connections").at("B").size(), 2);
        EXPECT_EQ(operation(Json::parse(legalizeExpressions(first.dump(), "silicon_shift"))), cell);
        EXPECT_NO_THROW((void)SILICON::yosys::deserialize(first.dump()));
      }
    }
  }
  EXPECT_THROW((void)legalizeExpressions(expression("$shr", 3, 2, 3, false, true).dump(),
                                          "silicon_shift"), std::runtime_error);
}

TEST(YosysLegalizationTest, FinalValidationRejectsUnsupportedCell)
{
  EXPECT_THROW((void)legalizeExpressions(expression("$mul", 3, 3, 3).dump(),
                                          "silicon_validate"), std::runtime_error);
}
#endif

TEST(YosysLegalizationEndToEndTest, ImportsComparisonsAndShiftsFromVerilog)
{
#ifdef SILICON_TEST_YOSYS_PLUGIN_AVAILABLE
  struct Case { const char* expression; const char* component; int a; int b; int y; int width; };
  const std::array cases{
      Case{"a == b", "Comparator", 2, 2, 1, 1},
      Case{"a < b", "Comparator", 2, 3, 1, 1},
      Case{"a <= b", "Comparator", 2, 3, 1, 1},
      Case{"a > b", "Comparator", 3, 2, 1, 1},
      Case{"a >= b", "Comparator", 3, 2, 1, 1},
      Case{"a << b", "Shifter", 6, 1, 4, 3},
      Case{"a >> b", "Shifter", 6, 1, 3, 3},
      Case{"$signed(a) >>> b", "Shifter", 6, 1, 7, 3},
  };
  for (const auto& item : cases) {
    SCOPED_TRACE(item.expression);
    const auto source = std::format(
        "module top(input [2:0] a, input [2:0] b, output [{}:0] y); assign y = {}; endmodule",
        item.width - 1, item.expression);
    const auto circuit = std::make_shared<Circuit>(importVerilog(source, "top"));
    EXPECT_EQ(componentTypes(*circuit).count(item.component), 1);
    EXPECT_EQ(evaluateBinaryCircuit(circuit, item.a, item.b), valueFor(item.width, item.y));
  }
#else
  GTEST_SKIP() << "The SILICON Yosys plugin is unavailable";
#endif
}

TEST(YosysCanonicalImporterTest, ImportsCanonicalExpressionCellsWithoutYosys)
{
  using SILICON::yosys::Json;
  using SILICON::yosys::SerializationContext;
  struct Case {
    const char* type;
    Json parameters;
    const char* component;
  };
  const std::array cases{
      Case{"SILICON_LOGIC", {{"MODE", SerializationContext::parameter(0)},
                              {"A_WIDTH", SerializationContext::parameter(3)},
                              {"B_WIDTH", SerializationContext::parameter(3)}}, "AndGate"},
      Case{"SILICON_COMPARE", {{"WIDTH", SerializationContext::parameter(3)},
                                {"MODE", SerializationContext::parameter(2)},
                                {"SIGNED", SerializationContext::parameter(1)}}, "Comparator"},
      Case{"SILICON_SHIFT", {{"WIDTH", SerializationContext::parameter(3)},
                              {"B_WIDTH", SerializationContext::parameter(3)},
                              {"DIRECTION", SerializationContext::parameter(1)},
                              {"ARITHMETIC", SerializationContext::parameter(1)}}, "Shifter"},
  };
  for (const auto& item : cases) {
    SCOPED_TRACE(item.type);
    auto design = expression("$eq", 3, 3, 1);
    auto& cell = design["modules"]["top"]["cells"]["op"];
    cell["type"] = item.type;
    cell["parameters"] = item.parameters;
    if (std::string_view(item.type) == "SILICON_SHIFT") {
      auto& y = cell["connections"]["Y"];
      y.push_back(9);
      y.push_back(10);
    }
    const auto circuit = SILICON::yosys::deserialize(design.dump());
    EXPECT_EQ(componentTypes(circuit).count(item.component), 1);
    if (auto comparator = findComponent<Comparator>(circuit)) {
      EXPECT_EQ(comparator->getPropertyValue<std::string>("mode"), "<=");
      EXPECT_TRUE(comparator->getPropertyValue<bool>("signed"));
    }
    if (auto shifter = findComponent<Shifter>(circuit)) {
      EXPECT_EQ(shifter->getPropertyValue<std::string>("mode"),
                std::string(Shifter::RIGHT_MODE));
      EXPECT_TRUE(shifter->getPropertyValue<bool>("signed"));
    }
  }
}
