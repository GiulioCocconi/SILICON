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

#include "yosys_test_helpers.hpp"

TEST(YosysTest, YosysAcceptsEveryBuiltInLowering)
{
#ifndef SILICON_TEST_YOSYS_EXECUTABLE
  GTEST_SKIP() << "Yosys executable was not found when tests were configured";
#else
  std::vector<Component_ptr> components;
  components.push_back(std::make_shared<AndGate>(
      std::vector<Wire_ptr>{std::make_shared<Wire>(), std::make_shared<Wire>(),
                            std::make_shared<Wire>()},
      std::make_shared<Wire>()));
  components.push_back(std::make_shared<OrGate>(
      std::vector<Wire_ptr>{std::make_shared<Wire>(), std::make_shared<Wire>()},
      std::make_shared<Wire>()));
  components.push_back(
      std::make_shared<NotGate>(std::make_shared<Wire>(), std::make_shared<Wire>()));
  components.push_back(std::make_shared<NandGate>(
      std::vector<Wire_ptr>{std::make_shared<Wire>(), std::make_shared<Wire>()},
      std::make_shared<Wire>()));
  components.push_back(std::make_shared<NorGate>(
      std::vector<Wire_ptr>{std::make_shared<Wire>(), std::make_shared<Wire>()},
      std::make_shared<Wire>()));
  components.push_back(std::make_shared<XorGate>(
      std::array<Wire_ptr, 2>{std::make_shared<Wire>(), std::make_shared<Wire>()},
      std::make_shared<Wire>()));
  components.push_back(std::make_shared<Extender>(Bus(3), Bus(5)));
  components.push_back(std::make_shared<Complementer>(Bus(4), Bus(4)));
  components.push_back(std::make_shared<HalfAdder>(
      std::array<Wire_ptr, 2>{std::make_shared<Wire>(), std::make_shared<Wire>()},
      std::make_shared<Wire>(), std::make_shared<Wire>()));
  components.push_back(std::make_shared<FullAdder>(
      std::array<Wire_ptr, 2>{std::make_shared<Wire>(), std::make_shared<Wire>()},
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>()));
  components.push_back(std::make_shared<AdderNBits>(std::array<Bus, 2>{Bus(4), Bus(4)},
                                                    Bus(4), std::make_shared<Wire>()));
  components.push_back(std::make_shared<Comparator>(std::array<Bus, 2>{Bus(4), Bus(4)},
                                                    std::make_shared<Wire>()));
  components.push_back(
      std::make_shared<Multiplexer>(Bus(4), Bus(2), std::make_shared<Wire>()));
  auto busMux = std::make_shared<Multiplexer>(Bus(4), Bus(2), std::make_shared<Wire>());
  busMux->setProperty("busSize", 2);
  components.push_back(busMux);
  components.push_back(std::make_shared<Demultiplexer>(Bus(1), Bus(2), Bus(4)));
  auto busDemux = std::make_shared<Demultiplexer>(Bus(1), Bus(2), Bus(4));
  busDemux->setProperty("busSize", 2);
  components.push_back(busDemux);
  components.push_back(std::make_shared<Decoder>(Bus(1), Bus(2), Bus(4)));
  components.push_back(
      std::make_shared<WireSplitter>(Bus(2), std::vector<Bus>{Bus(1), Bus(1)}));
  components.push_back(
      std::make_shared<WireMerger>(std::vector<Bus>{Bus(1), Bus(1)}, Bus(2)));
  components.push_back(std::make_shared<DFlipFlop>(
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>(),
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>()));
  components.push_back(std::make_shared<EFlipFlop>(
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>(),
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>(),
      std::make_shared<Wire>()));
  components.push_back(
      std::make_shared<DLatch>(std::make_shared<Wire>(), std::make_shared<Wire>(),
                               std::make_shared<Wire>(), std::make_shared<Wire>()));
  components.push_back(std::make_shared<JKFlipFlop>(
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>(),
      std::make_shared<Wire>(), std::make_shared<Wire>(), std::make_shared<Wire>(),
      std::make_shared<Wire>()));
  components.push_back(std::make_shared<Register>(Bus(4), std::make_shared<Wire>(),
                                                  std::make_shared<Wire>(),
                                                  std::make_shared<Wire>(), Bus(4)));
  components.push_back(std::make_shared<Register>(Bus(1), std::make_shared<Wire>(),
                                                  std::make_shared<Wire>(),
                                                  std::make_shared<Wire>(), Bus(4)));
  components.push_back(std::make_shared<Register>(Bus(4), std::make_shared<Wire>(),
                                                  std::make_shared<Wire>(),
                                                  std::make_shared<Wire>(), Bus(1)));
  auto serialRegister = std::make_shared<Register>(Bus(2), std::make_shared<Wire>(),
                                                   std::make_shared<Wire>(),
                                                   std::make_shared<Wire>(), Bus(2));
  serialRegister->setProperty("inputType", std::string(Register::SerialType));
  serialRegister->setProperty("outputType", std::string(Register::SerialType));
  components.push_back(serialRegister);

  for (std::size_t index = 0; index < components.size(); ++index) {
    SCOPED_TRACE(std::format("component {} ({})", index, components[index]->typeName()));
    const auto circuit = circuitWithBoundaryPorts(components[index]);
    EXPECT_EQ(validateWithYosys(circuit, std::format("component_{}", index)), 0);
    EXPECT_NO_THROW(
        (void)SILICON::yosys::deserialize(legalizeExpressions(SILICON::yosys::serialize(circuit, "top"))));
    const auto verilog = SILICON::verilog::write(circuit, "top");
    EXPECT_EQ(verilog.find("SILICON_"), std::string::npos);
    EXPECT_NO_THROW((void)importVerilog(verilog, "top"));
  }
#endif
}

TEST(YosysTest, ExportsSubcircuitsAsHierarchicalModules)
{
  auto registry = ComponentRegistry::empty();
  registerAllComponents(registry);
  SILICON::project::ProjectContext         project;
  SILICON::project::ProjectCircuitResolver resolver{project, registry};
  project.upsertDocument({SILICON::project::documentPathForSlug(
                              SILICON::project::DocumentType::Circuit, "and_child"),
                          andSubcircuitDocument()});

  {
    auto instance = std::make_shared<SubcircuitComponent>(&resolver);
    instance->setProperty("slug", std::string("and_child"));
    auto circuit = circuitWithBoundaryPorts(instance);

    const auto json = nlohmann::json::parse(SILICON::yosys::serialize(circuit, "top"));
    ASSERT_TRUE(json.at("modules").contains("top"));
    ASSERT_TRUE(json.at("modules").contains("and_child"));
    EXPECT_TRUE(std::ranges::any_of(
        json.at("modules").at("top").at("cells"),
        [](const auto& cell) { return cell.at("type") == "and_child"; }));
#ifdef SILICON_TEST_YOSYS_EXECUTABLE
    EXPECT_EQ(validateWithYosys(circuit, "hierarchy"), 0);
    const auto verilog = SILICON::verilog::write(circuit, "top");
    EXPECT_NE(verilog.find("module and_child"), std::string::npos);
    EXPECT_NO_THROW((void)importVerilog(verilog, "top"));
#endif
  }
}

TEST(YosysTest, YosysReadJsonAcceptsExport)
{
#ifndef SILICON_TEST_YOSYS_EXECUTABLE
  GTEST_SKIP() << "Yosys executable was not found when tests were configured";
#else
  auto    a = std::make_shared<Wire>();
  auto    b = std::make_shared<Wire>();
  auto    y = std::make_shared<Wire>();
  Circuit circuit(
      Component_set{
          std::make_shared<DummyInputComponent>(Bus{a}, "a"),
          std::make_shared<DummyInputComponent>(Bus{b}, "b"),
          std::make_shared<AndGate>(std::vector<Wire_ptr>{a, b}, y),
          std::make_shared<DummyOutputComponent>(Bus{y}, "y"),
      },
      false);
  EXPECT_EQ(validateWithYosys(circuit, "simple"), 0);
#endif
}
