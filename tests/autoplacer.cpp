#include <QApplication>
#include <QGraphicsRectItem>
#include <core/circuit.hpp>
#include <core/gates.hpp>
#include <gtest/gtest.h>
#include <ui/circuit/components/graphicalLogicComponent.hpp>
#include <ui/circuit/diagram/circuitAutoplacer.hpp>

using namespace SILICON::core;
using namespace SILICON::ui;

namespace {
class AutoplacerCancellation : public testing::Test {
protected:
  static void SetUpTestSuite()
  {
    static int          argc   = 1;
    static char         name[] = "autoplacer_tests";
    static char*        argv[] = {name, nullptr};
    static QApplication app(argc, argv);
  }
};
}  // namespace

TEST_F(AutoplacerCancellation, BeforeFirstCandidateLeavesNoGeometry)
{
  Circuit                  circuit;
  CircuitAutoplacerOptions options;
  options.isCancelled = [] { return true; };
  auto result         = CircuitAutoplacer::compute(circuit, {}, options);
  EXPECT_TRUE(result.cancelled);
  EXPECT_TRUE(result.components.empty());
  EXPECT_TRUE(result.wires.empty());
}

TEST_F(AutoplacerCancellation, DuringRoutingDiscardsCandidateAndKeepsBest)
{
  auto net    = std::make_shared<Wire>();
  auto source = std::make_shared<AndGate>();
  auto target = std::make_shared<AndGate>();
  source->setInputs({Bus{std::make_shared<Wire>()}, Bus{std::make_shared<Wire>()}});
  source->setOutputs({Bus{net}});
  target->setOutputs({Bus{std::make_shared<Wire>()}});
  target->setInputs({Bus{net}, Bus{std::make_shared<Wire>()}});
  Circuit                 circuit(Component_set{source, target}, false);
  GraphicalLogicComponent first(source, new QGraphicsRectItem(0, 0, 80, 80), nullptr);
  GraphicalLogicComponent second(target, new QGraphicsRectItem(0, 0, 80, 80), nullptr);
  first.setPorts({{"a", {-20, 20}}, {"b", {-20, 60}}}, {{"o", {100, 40}}});
  second.setPorts({{"a", {-20, 20}}, {"b", {-20, 60}}}, {{"o", {100, 40}}});
  std::vector<GraphicalLogicComponent*> components{&first, &second};
  CircuitAutoplacerOptions              options;
  options.candidateCount = 2;
  // Enumerate every cancellation checkpoint through the first completed
  // candidate, including router and wire-merger polls. None may expose a prefix.
  unsigned checks = 0, firstCompleteChecks = 0;
  options.isCancelled = [&] {
    ++checks;
    return false;
  };
  options.progress = [&](int done, int) {
    if (done == 1 && !firstCompleteChecks)
      firstCompleteChecks = checks;
  };
  auto complete = CircuitAutoplacer::compute(circuit, components, options);
  ASSERT_FALSE(complete.cancelled);
  ASSERT_FALSE(complete.wires.empty());
  ASSERT_GT(firstCompleteChecks, 2u);
  for (unsigned cancelAt = 1; cancelAt <= firstCompleteChecks; ++cancelAt) {
    checks              = 0;
    options.progress    = {};
    options.isCancelled = [&] { return ++checks >= cancelAt; };
    auto result         = CircuitAutoplacer::compute(circuit, components, options);
    EXPECT_TRUE(result.cancelled) << cancelAt;
    EXPECT_TRUE(result.components.empty()) << cancelAt;
    EXPECT_TRUE(result.wires.empty()) << cancelAt;
  }
  bool cancelled      = false;
  options.isCancelled = [&] { return cancelled; };
  options.progress    = [&](int done, int) {
    if (done == 1)
      cancelled = true;
  };
  auto best = CircuitAutoplacer::compute(circuit, components, options);
  EXPECT_FALSE(best.cancelled);
  EXPECT_FALSE(best.wires.empty());
  // Interrupt inside the next attempt, after the previous candidate was saved.
  cancelled           = false;
  checks              = 0;
  options.isCancelled = [&] { return cancelled && ++checks >= 4; };
  auto interrupted    = CircuitAutoplacer::compute(circuit, components, options);
  EXPECT_FALSE(interrupted.cancelled);
  EXPECT_EQ(interrupted.components, best.components);
  ASSERT_EQ(interrupted.wires.size(), best.wires.size());
  for (std::size_t i = 0; i < best.wires.size(); ++i) {
    EXPECT_EQ(interrupted.wires[i].bus, best.wires[i].bus);
    EXPECT_EQ(interrupted.wires[i].points, best.wires[i].points);
  }
  EXPECT_EQ(first.pos(), QPointF());
  EXPECT_EQ(second.pos(), QPointF());
}
