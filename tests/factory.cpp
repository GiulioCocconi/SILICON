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

#include "tests.hpp"

#include <algorithm>
#include <set>

#include <core/circuitDocument.hpp>
#include <core/io.hpp>
#include <core/memory.hpp>
#include <core/serialization/component_registration.hpp>
#include <core/serialization/component_registry.hpp>
#include <core/subcircuit.hpp>
#include <nlohmann/json.hpp>

using namespace SILICON::core;

class FactoryTest : public ::testing::Test {
protected:
  ComponentRegistry registry;

  FactoryTest() { registerAllComponents(registry); }
};

TEST_F(FactoryTest, CreateReturnsNonNullAndMatchesTypeName)
{
  for (const auto& typeName : registry.availableTypes()) {
    auto comp = registry.create(typeName);
    ASSERT_NE(comp, nullptr);
    EXPECT_EQ(comp->typeName(), typeName);
  }
}

TEST_F(FactoryTest, UnknownTypeThrows)
{
  EXPECT_THROW(registry.create("NonExistentComponent"), std::runtime_error);
}

TEST_F(FactoryTest, AvailableTypesIsNonEmpty)
{
  EXPECT_FALSE(registry.availableTypes().empty());
}

TEST_F(FactoryTest, CreatedComponentsHaveExpectedDefaultBuses)
{
  for (const auto& typeName : registry.availableTypes()) {
    SCOPED_TRACE(typeName);
    auto comp = registry.create(typeName);
    if (const auto memory = std::dynamic_pointer_cast<Memory>(comp)) {
      EXPECT_EQ(memory->getInputs().size(), 1);
      ASSERT_EQ(memory->getOutputs().size(), 1);
      EXPECT_EQ(memory->getInputs()[0].size(), 1);
      EXPECT_EQ(memory->getOutputs()[0].size(), 8);
      EXPECT_EQ(memory->memoryDepth(), 2);
      EXPECT_FALSE(memory->memoryImageSnapshot());
    } else {
      EXPECT_TRUE(comp->getInputs().empty());
      EXPECT_TRUE(comp->getOutputs().empty());
    }
  }
}

TEST_F(FactoryTest, CreatedComponentsAreDistinctInstances)
{
  auto a = registry.create("AndGate");
  auto b = registry.create("AndGate");
  EXPECT_NE(a.get(), b.get());
}

TEST_F(FactoryTest, DuplicateRegistrationThrows)
{
  EXPECT_THROW(registerAllComponents(registry), std::logic_error);
}

TEST_F(FactoryTest, RegisteredComponentsHaveCatalogMetadata)
{
  for (const auto& typeName : registry.availableTypes()) {
    const auto metadata = registry.metadata(typeName);
    EXPECT_FALSE(metadata.displayName.empty()) << typeName;
    EXPECT_FALSE(metadata.description.empty()) << typeName;
  }
}

TEST_F(FactoryTest, RegistryMetadataComesFromComponentObject)
{
  for (const auto& typeName : registry.availableTypes()) {
    const auto component        = registry.create(typeName);
    const auto registryMetadata = registry.metadata(typeName);
    const auto objectMetadata   = component->metadata();

    EXPECT_EQ(registryMetadata.displayName, objectMetadata.displayName) << typeName;
    EXPECT_EQ(registryMetadata.description, objectMetadata.description) << typeName;
    EXPECT_EQ(registryMetadata.category, objectMetadata.category) << typeName;
    EXPECT_EQ(registryMetadata.portRole, objectMetadata.portRole) << typeName;
  }
}

TEST_F(FactoryTest, OnlyBoundaryIoComponentsDeclarePortRoles)
{
  EXPECT_EQ(registry.metadata(DummyInputComponent::Type).portRole, PortRole::Input);
  EXPECT_EQ(registry.metadata(DummyBusInputComponent::Type).portRole, PortRole::Input);
  EXPECT_EQ(registry.metadata(DummyOutputComponent::Type).portRole, PortRole::Output);
  EXPECT_EQ(registry.metadata(DummyBusOutputComponent::Type).portRole, PortRole::Output);
  EXPECT_EQ(registry.metadata(ConstantComponent::Type).portRole, PortRole::None);
}

TEST_F(FactoryTest, CatalogContainsOnlyTheUnifiedMemoryComponent)
{
  EXPECT_TRUE(registry.hasType("Memory"));
  EXPECT_FALSE(registry.hasType("RAM"));
  EXPECT_FALSE(registry.hasType("ROM"));
}

TEST_F(FactoryTest, DescriptionsExposeDefinitionsWithoutConfiguringBuses)
{
  for (const auto& type : registry.availableTypes()) {
    SCOPED_TRACE(type);
    const auto component   = registry.create(type);
    const auto inputs      = component->inputBuses();
    const auto outputs     = component->outputBuses();
    const auto description = component->describe();
    EXPECT_EQ(description.metadata.displayName,
              registry.describeConfiguration(type).metadata.displayName);
    if (type != "Subcircuit") {
      EXPECT_FALSE(description.ports.empty());
    }
    std::set<std::pair<PortDirection, unsigned int>> slots;
    for (const auto& port : description.ports) {
      EXPECT_FALSE(port.name.empty());
      EXPECT_FALSE(port.description.empty());
      EXPECT_GT(port.width, 0u);
      EXPECT_TRUE(slots.emplace(port.direction, port.index).second);
    }
    for (const auto& definition : description.properties) {
      EXPECT_FALSE(definition.key.empty());
      EXPECT_FALSE(definition.label.empty());
      EXPECT_FALSE(definition.description.empty());
      const auto value = component->getProperty(definition.key);
      ASSERT_TRUE(value);
      EXPECT_EQ(definition.type, propertyType(*value));
      if (definition.access == PropertyAccess::Derived) {
        EXPECT_FALSE(definition.defaultValue);
        EXPECT_FALSE(component->getProperties().contains(definition.key));
      } else {
        ASSERT_TRUE(definition.defaultValue);
        EXPECT_EQ(definition.defaultValue, value);
        if (!definition.acceptedValues.empty()) {
          EXPECT_TRUE(std::ranges::contains(definition.acceptedValues, *value));
        }
      }
    }
    EXPECT_EQ(component->inputBuses(), inputs);
    EXPECT_EQ(component->outputBuses(), outputs);
  }
}

TEST_F(FactoryTest, DefinitionsKeepDefaultsAfterInstanceEdits)
{
  const auto gate     = registry.create("NotGate");
  const auto defaults = gate->propertyDefinitions();
  gate->setProperty("delay", 19);
  gate->setProperty("size", 7);
  const auto edited = gate->propertyDefinitions();
  ASSERT_EQ(defaults.size(), edited.size());
  for (std::size_t i = 0; i < defaults.size(); ++i)
    EXPECT_EQ(defaults[i].defaultValue, edited[i].defaultValue);
  const auto delay = std::ranges::find(edited, "delay", &PropertyDefinition::key);
  ASSERT_NE(delay, edited.end());
  EXPECT_EQ(gate->getProperty("delay"), PropertyValue{19});
}

namespace {
void attachDescribedBuses(Component& component)
{
  std::vector<Bus> inputs, outputs;
  for (const auto& port : component.describe().ports) {
    auto& buses = port.direction == PortDirection::Input ? inputs : outputs;
    ASSERT_EQ(port.index, buses.size());
    buses.emplace_back(static_cast<unsigned short>(port.width));
  }
  component.setInputs(inputs);
  component.setOutputs(outputs);
}

void expectDescribedBuses(const Component& component)
{
  const auto ports = component.portDescriptions();
  EXPECT_EQ(ports.size(), component.inputBuses().size() + component.outputBuses().size());
  for (const auto& port : ports) {
    const auto& buses = port.direction == PortDirection::Input ? component.inputBuses()
                                                               : component.outputBuses();
    ASSERT_LT(port.index, buses.size());
    EXPECT_EQ(port.width, buses[port.index].size());
  }
}
}  // namespace

TEST_F(FactoryTest, ConfiguredPortsFollowExistingBusTopology)
{
  const std::vector<std::pair<std::string, std::vector<PropertyMap>>> configurations{
      {"AndGate", {{{"size", 4}}, {{"bitwise", true}}, {{"size", 1}}}},
      {"NotGate", {{{"size", 3}}}},
      {"Register",
       {{{"size", 5}},
        {{"outputType", std::string("Serial")}},
        {{"inputType", std::string("Serial")}}}},
      {"Multiplexer",
       {{{"selectionSize", 2}},
        {{"busSize", 4}},
        {{"selectionTable", std::string("0,1,1,0")}}}},
      {"Demultiplexer", {{{"selectionSize", 2}}, {{"busSize", 4}}}},
      {"Decoder", {{{"selectionSize", 3}}}},
      {"WireSplitter", {{{"size", 8}}, {{"slices", std::string("0:4,4:4")}}}},
      {"WireMerger", {{{"size", 8}}, {{"widths", std::string("4,4")}}}},
      {"Memory",
       {{{"type", std::string("RAM")}},
        {{"readPorts", 2}},
        {{"dataWidth", 10}, {"addressWidth", 5}, {"depthOverride", 3}}}}};
  for (const auto& [type, edits] : configurations) {
    SCOPED_TRACE(type);
    const auto component = registry.create(type);
    attachDescribedBuses(*component);
    expectDescribedBuses(*component);
    for (const auto& edit : edits) {
      component->setProperties(edit);
      expectDescribedBuses(*component);
      // Fresh, unwired instances must describe the same shape using only configuration.
      const auto unwired = registry.create(type);
      unwired->setProperties(component->getProperties());
      const auto logical    = unwired->describe().ports;
      const auto configured = component->describe().ports;
      ASSERT_EQ(logical.size(), configured.size());
      for (std::size_t i = 0; i < logical.size(); ++i) {
        EXPECT_EQ(logical[i].direction, configured[i].direction);
        EXPECT_EQ(logical[i].index, configured[i].index);
        EXPECT_EQ(logical[i].width, configured[i].width);
      }
    }
  }
}

TEST_F(FactoryTest, DerivedMemoryPropertiesAreReadableButNeverStoredOrWritable)
{
  const auto memory = registry.create("Memory");
  const auto stored = memory->getProperties();
  EXPECT_EQ(memory->getPropertyValue<int>("effectiveDepth"), 2);
  EXPECT_EQ(memory->getPropertyValue<int>("capacity"), 16);
  EXPECT_THROW(memory->setProperty("capacity", std::string("other")),
               std::invalid_argument);
  EXPECT_THROW(memory->setProperties({{"effectiveDepth", std::string("other")}}),
               std::invalid_argument);
  EXPECT_EQ(memory->getProperties(), stored);
  const auto serialized =
      nlohmann::json::parse(Circuit(Component_set{memory}, false).serialize());
  const auto& properties = serialized.at("components").at(0).at("properties");
  EXPECT_FALSE(properties.contains("effectiveDepth"));
  EXPECT_FALSE(properties.contains("capacity"));
  EXPECT_FALSE(properties.contains("initializationStatus"));
}

TEST_F(FactoryTest, SubcircuitDescriptionsFollowSelectedDefinitionsAndImportedPorts)
{
  class NamedResolver : public CircuitResolver {
  public:
    SubcircuitDefinition resolve(std::string_view slug) const override
    {
      return {Circuit{},
              {{slug == "first" ? "address" : "command", Bus(3)}},
              {{"ready", Bus(1)}}};
    }
  } resolver;
  SubcircuitComponent component(&resolver);
  EXPECT_TRUE(component.describe().ports.empty());
  component.setProperty("slug", std::string("first"));
  auto ports = component.describe().ports;
  ASSERT_EQ(ports.size(), 2u);
  EXPECT_EQ(ports[0].name, "address");
  EXPECT_EQ(ports[0].width, 3u);
  EXPECT_EQ(ports[1].name, "ready");
  EXPECT_EQ(ports[1].direction, PortDirection::Output);
  component.setProperty("slug", std::string("second"));
  EXPECT_EQ(component.describe().ports[0].name, "command");
  component.setProperty("slug", std::string{});
  EXPECT_TRUE(component.describe().ports.empty());

  const auto imported = SubcircuitComponent::imported("module", {"operand"}, {Bus(4)},
                                                      {"result"}, {Bus(8)});
  ports               = imported->describe().ports;
  ASSERT_EQ(ports.size(), 2u);
  EXPECT_EQ(ports[0].name, "operand");
  EXPECT_EQ(ports[1].name, "result");
  EXPECT_EQ(ports[1].width, 8u);
}

TEST_F(FactoryTest, TypeSchemasIncludePropertiesAbsentFromDefaultInstances)
{
  const auto schema   = registry.describeType("Memory");
  const auto defaults = registry.describeConfiguration("Memory");
  const auto ram =
      registry.describeConfiguration("Memory", {{"type", std::string("RAM")},
                                                {"initialization", std::string("Zero")},
                                                {"readPorts", 2},
                                                {"dataWidth", 4},
                                                {"addressWidth", 3}});
  for (const auto& field : schema.properties) {
    EXPECT_FALSE(field.description.empty());
    if (field.defaultValue) {
      EXPECT_EQ(propertyType(*field.defaultValue), field.type);
    }
  }
  EXPECT_NE(
      std::ranges::find(schema.properties, "initialization", &PropertyDefinition::key),
      schema.properties.end());
  EXPECT_EQ(
      std::ranges::find(defaults.properties, "initialization", &PropertyDefinition::key),
      defaults.properties.end());
  EXPECT_NE(std::ranges::find(ram.properties, "initialization", &PropertyDefinition::key),
            ram.properties.end());
  EXPECT_EQ(std::ranges::find(ram.properties, "binaryContents", &PropertyDefinition::key),
            ram.properties.end());
  EXPECT_EQ(std::ranges::find(ram.properties, "padding", &PropertyDefinition::key),
            ram.properties.end());
  EXPECT_GT(ram.ports.size(), defaults.ports.size());
  EXPECT_EQ(ram.values.at("effectiveDepth"), PropertyValue{8});
  EXPECT_EQ(ram.values.at("capacity"), PropertyValue{32});
  const auto binaryRam = registry.describeConfiguration(
      "Memory",
      {{"type", std::string("RAM")}, {"initialization", std::string("Binary document")}});
  for (const auto key : {"binaryContents", "padding"}) {
    const auto declaration =
        std::ranges::find(schema.properties, key, &PropertyDefinition::key);
    ASSERT_NE(declaration, schema.properties.end());
    EXPECT_FALSE(declaration->availability.empty());
    EXPECT_NE(std::ranges::find(binaryRam.properties, key, &PropertyDefinition::key),
              binaryRam.properties.end());
  }
}

TEST_F(FactoryTest, FiniteConfigurationChoicesAgreeWithWiredTopologyAndTypeSchemas)
{
  for (const auto& type : registry.availableTypes()) {
    SCOPED_TRACE(type);
    const auto               schema = registry.describeType(type);
    std::vector<PropertyMap> configurations(1);
    for (const auto& field : schema.properties) {
      if (field.access != PropertyAccess::Editable)
        continue;
      auto choices = field.acceptedValues;
      if (field.type == PropertyType::Boolean)
        choices = {false, true};
      if (choices.empty())
        continue;
      std::vector<PropertyMap> expanded;
      for (const auto& configuration : configurations)
        for (const auto& choice : choices) {
          auto next = configuration;
          next.emplace(field.key, choice);
          expanded.push_back(std::move(next));
        }
      configurations = std::move(expanded);
    }
    for (const auto& configuration : configurations) {
      const auto component = registry.create(type);
      attachDescribedBuses(*component);
      component->setProperties(configuration);
      const auto ports    = component->portDescriptions();
      const auto resolved = registry.describeConfiguration(type, configuration);
      ASSERT_EQ(ports.size(),
                component->inputBuses().size() + component->outputBuses().size());
      ASSERT_EQ(ports.size(), resolved.ports.size());
      for (std::size_t i = 0; i < ports.size(); ++i) {
        const auto& buses = ports[i].direction == PortDirection::Input
                                ? component->inputBuses()
                                : component->outputBuses();
        ASSERT_LT(ports[i].index, buses.size());
        EXPECT_EQ(ports[i].width, buses[ports[i].index].size());
        EXPECT_EQ(ports[i].width, resolved.ports[i].width);
        EXPECT_EQ(ports[i].index, resolved.ports[i].index);
        EXPECT_FALSE(ports[i].description.empty());
      }
      for (const auto& active : component->propertyDefinitions()) {
        const auto declared =
            std::ranges::find(schema.properties, active.key, &PropertyDefinition::key);
        ASSERT_NE(declared, schema.properties.end());
        EXPECT_EQ(active.type, declared->type);
        EXPECT_EQ(active.defaultValue, declared->defaultValue);
        const auto value = component->getProperty(active.key);
        ASSERT_TRUE(value);
        EXPECT_EQ(propertyType(*value), active.type);
      }
    }
  }
}

TEST_F(FactoryTest, DeclaredNumericBoundsAndChoicesAgreeWithPropertyValidation)
{
  for (const auto& type : registry.availableTypes()) {
    SCOPED_TRACE(type);
    for (const auto& field : registry.describeType(type).properties) {
      if (field.access != PropertyAccess::Editable)
        continue;
      SCOPED_TRACE(field.key);
      std::vector<PropertyValue> invalid;
      if (field.type == PropertyType::Integer) {
        if (field.constraints.minimum
            && *field.constraints.minimum > std::numeric_limits<int>::min())
          invalid.emplace_back(*field.constraints.minimum - 1);
        if (field.constraints.maximum
            && *field.constraints.maximum < std::numeric_limits<int>::max())
          invalid.emplace_back(*field.constraints.maximum + 1);
      } else if (field.type == PropertyType::String && !field.acceptedValues.empty()) {
        invalid.emplace_back(std::string("__invalid_choice__"));
      }
      for (const auto& value : invalid) {
        const auto component = registry.create(type);
        const auto before    = component->getProperties();
        const auto inputs    = component->inputBuses();
        const auto outputs   = component->outputBuses();
        if (field.constraints.normalization == PropertyNormalization::Clamp) {
          const auto expected = field.normalize(value);
          EXPECT_NO_THROW(component->setProperty(field.key, value));
          EXPECT_EQ(component->getProperty(field.key), expected);
        } else {
          EXPECT_THROW(component->setProperty(field.key, value), std::invalid_argument);
          EXPECT_EQ(component->getProperties(), before);
          EXPECT_EQ(component->inputBuses(), inputs);
          EXPECT_EQ(component->outputBuses(), outputs);
        }
      }
    }
  }
}

TEST(ComponentMetadataTest, PropertyTypesDescribeTheirValuesIndependentlyOfStorageOrder)
{
  EXPECT_EQ(propertyType(PropertyValue{3}), PropertyType::Integer);
  EXPECT_EQ(propertyType(PropertyValue{true}), PropertyType::Boolean);
  EXPECT_EQ(propertyType(PropertyValue{std::string("value")}), PropertyType::String);
  EXPECT_EQ(propertyType(PropertyValue{BusValue{State::LOW}}), PropertyType::BusValue);
}

TEST_F(FactoryTest, ConstructorInitialValuesRemainSeparateFromTypeDeclarations)
{
  const std::vector<Component_ptr> components{
      std::make_shared<DummyBusInputComponent>(Bus(7), "request"),
      std::make_shared<DummyBusOutputComponent>(Bus(5), "response"),
      std::make_shared<ConstantComponent>(Bus(4), BusValue(4, State::HIGH))};
  for (const auto& component : components) {
    SCOPED_TRACE(component->typeName());
    const auto schema = registry.describeType(component->typeName());
    for (const auto& field : component->declaredProperties()) {
      const auto declared =
          std::ranges::find(schema.properties, field.key, &PropertyDefinition::key);
      ASSERT_NE(declared, schema.properties.end());
      EXPECT_EQ(field.defaultValue, declared->defaultValue);
      EXPECT_EQ(field.type, declared->type);
      EXPECT_EQ(field.constraints.minimum, declared->constraints.minimum);
      EXPECT_EQ(field.constraints.maximum, declared->constraints.maximum);
    }
    EXPECT_NE(component->getProperty("size"), PropertyValue{1});
  }
}
