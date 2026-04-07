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

#include <core/serialization/component_registration.hpp>
#include <core/serialization/component_registry.hpp>

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

TEST_F(FactoryTest, CreatedComponentsHaveEmptyBuses)
{
  for (const auto& typeName : registry.availableTypes()) {
    auto comp = registry.create(typeName);
    EXPECT_TRUE(comp->getInputs().empty());
    EXPECT_TRUE(comp->getOutputs().empty());
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
