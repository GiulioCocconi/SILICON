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

#include <core/io.hpp>
#include <core/memory.hpp>
#include <core/serialization/component_registration.hpp>
#include <core/serialization/component_registry.hpp>

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
      EXPECT_EQ(memory->getInputs().size(), typeName == RAM::Type ? 5 : 3);
      ASSERT_EQ(memory->getOutputs().size(), 1);
      EXPECT_EQ(memory->getInputs()[0].size(), 1);
      EXPECT_EQ(memory->getOutputs()[0].size(), 8);
      const auto depth = typeName == RAM::Type
                             ? std::dynamic_pointer_cast<RAM>(comp)->memoryDepth()
                             : std::dynamic_pointer_cast<ROM>(comp)->memoryDepth();
      EXPECT_EQ(depth, typeName == RAM::Type ? 1 : 0);
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
