/*
  Copyright (c) 2026. Giulio Cocconi
  SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <reference.hpp>

#include <core/serialization/component_registration.hpp>
#include <ui/serialization/gui_component_registration.hpp>
#include <ui/shell/theme.hpp>

#include <QApplication>
#include <QFontDatabase>
#include <QImage>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>

using namespace SILICON;

namespace {

class ComponentDocumentation : public testing::Test {
protected:
  core::ComponentRegistry registry;
  ui::GUIComponentFactory factory;

  ComponentDocumentation()
  {
    core::registerAllComponents(registry);
    ui::registerAllGUIComponents(factory);
  }
};

std::map<std::string, std::string> contents(const std::filesystem::path& directory)
{
  std::map<std::string, std::string> result;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
    if (!entry.is_regular_file())
      continue;
    std::ifstream file(entry.path(), std::ios::binary);
    result.emplace(entry.path().lexically_relative(directory).generic_string(),
                   std::string(std::istreambuf_iterator<char>(file), {}));
  }
  return result;
}

void expectSameContents(const std::map<std::string, std::string>& actual,
                        const std::map<std::string, std::string>& expected)
{
  ASSERT_EQ(actual.size(), expected.size());
  for (const auto& [path, data] : expected) {
    const auto found = actual.find(path);
    ASSERT_NE(found, actual.end()) << path;
    EXPECT_TRUE(found->second == data) << "Generated file changed: " << path;
  }
}

TEST_F(ComponentDocumentation, CoversCatalogAndFullSchemasWithDefaultPins)
{
  const auto references = docs::collectReferences(registry, factory);
  EXPECT_EQ(references.size(), factory.availableTypes().size() - 2);
  EXPECT_EQ(references.size(), registry.availableTypes().size() - 1);
  std::set<std::string> types;
  for (const auto& entry : references) {
    SCOPED_TRACE(entry.type);
    EXPECT_TRUE(types.insert(entry.type).second);
    EXPECT_FALSE(entry.metadata.description.empty());
    if (registry.hasType(entry.type)) {
      const auto schema = registry.describeType(entry.type);
      EXPECT_EQ(schema.properties.size(), entry.properties.size());
      for (std::size_t i = 0; i < schema.properties.size(); ++i)
        EXPECT_EQ(schema.properties[i].key, entry.properties[i].key);
    }
  }
  const auto memory = std::ranges::find(references, "Memory", &docs::Reference::type);
  ASSERT_NE(memory, references.end());
  const auto page = docs::componentPage(*memory);
  EXPECT_EQ(page.find("property-initializationStatus"), std::string::npos);
  EXPECT_EQ(page.find("property-capacity"), std::string::npos);
  EXPECT_NE(page.find("property-binaryContents"), std::string::npos);
  EXPECT_NE(page.find("Writable RAM; ROM always uses a binary document."),
            std::string::npos);
  EXPECT_EQ(page.find("| Access |"), std::string::npos);
  EXPECT_EQ(page.find("Default example value"), std::string::npos);
  EXPECT_EQ(page.find("Bus index"), std::string::npos);

  const auto text = std::ranges::find(references, "Text", &docs::Reference::type);
  EXPECT_EQ(text, references.end());
  const auto subcircuit =
      std::ranges::find(references, "Subcircuit", &docs::Reference::type);
  EXPECT_EQ(subcircuit, references.end());
  const auto adder = std::ranges::find(references, "HalfAdder", &docs::Reference::type);
  ASSERT_NE(adder, references.end());
  const auto adderPage = docs::componentPage(*adder);
  EXPECT_NE(adderPage.find("Editor label"), std::string::npos);
  EXPECT_NE(adderPage.find("<code v-pre>cout</code> | <code v-pre>co</code>"),
            std::string::npos);
}

TEST_F(ComponentDocumentation, EscapesMetadataAndDocumentsAllConstraintKinds)
{
  auto property =
      core::propertyDefinition("key", 3, "<script> {{ value }} | `x`\nnext",
                               {.minimum       = 1,
                                .maximum       = 8,
                                .description   = "a < b & b > c",
                                .normalization = core::PropertyNormalization::Clamp});
  property.label          = "Label [link]";
  property.acceptedValues = {1, 3, 8};
  property.availability   = "Only in mode *A*";
  auto readOnly           = property;
  readOnly.key            = "readOnlyExample";
  readOnly.access         = core::PropertyAccess::ReadOnly;
  auto derived            = property;
  derived.key             = "derivedExample";
  derived.access          = core::PropertyAccess::Derived;
  const docs::Reference entry{
      .type       = "Example",
      .metadata   = {.displayName = "<Example>",
                     .description = "{{ dangerous }} & description",
                     .category    = core::ComponentCategory::Utils},
      .properties = {property, readOnly, derived},
      .ports      = {{"a|b", core::PortDirection::Input, 0, 8, "Operand"}}};
  const auto page = docs::componentPage(entry);
  for (const auto* expected :
       {"&lt;script&gt;", "&#123;&#123;", "&#124;", "<br>next", "Clamp to bounds",
        "Minimum (inclusive) | 1", "Maximum (inclusive) | 8", "Only in mode &#42;A&#42;",
        "a &lt; b &amp; b &gt; c", "Label &#91;link&#93;"})
    EXPECT_NE(page.find(expected), std::string::npos) << expected;
  EXPECT_EQ(page.find("<script>"), std::string::npos);
  EXPECT_EQ(page.find("{{"), std::string::npos);
  EXPECT_EQ(page.find("readOnlyExample"), std::string::npos);
  EXPECT_EQ(page.find("derivedExample"), std::string::npos);
  EXPECT_NE(docs::typeSlug("a/b"), docs::typeSlug("a-b"));
  EXPECT_NE(docs::typeSlug("../"), "../");
}

TEST_F(ComponentDocumentation, GeneratesHdThemedPngsAndReplacesObsoleteOutput)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto output =
      std::filesystem::path(temporary.path().toStdString()) / "components";
  auto&      app      = *qobject_cast<QApplication*>(QApplication::instance());
  const auto original = ui::ThemeEngine::mode();
  docs::generateReference(output, registry, factory, app);
  EXPECT_EQ(ui::ThemeEngine::mode(), original);
  const auto first = contents(output);
  EXPECT_TRUE(first.contains("index.md"));
  std::size_t pngCount = 0;
  for (const auto& [path, data] : first) {
    EXPECT_FALSE(path.ends_with(".svg")) << path;
    if (!path.ends_with(".png"))
      continue;
    ++pngCount;
    const auto image = QImage::fromData(QByteArray::fromStdString(data), "PNG");
    ASSERT_FALSE(image.isNull()) << path;
    EXPECT_GE(std::max(image.width(), image.height()), 1920) << path;
    EXPECT_TRUE(image.hasAlphaChannel()) << path;
    EXPECT_EQ(image.pixelColor(0, 0).alpha(), 0) << path;
    bool painted = false;
    for (int y = 0; !painted && y < image.height(); y += 8)
      for (int x = 0; !painted && x < image.width(); x += 8)
        painted = image.pixelColor(x, y).alpha() > 0;
    EXPECT_TRUE(painted) << path;
  }
  EXPECT_EQ(pngCount, docs::collectReferences(registry, factory).size() * 2);
  EXPECT_NE(first.at("gates/AndGate-light.png"), first.at("gates/AndGate-dark.png"));
  const auto gate = docs::collectReferences(registry, factory).front();
  const auto page = docs::componentPage(gate);
  EXPECT_NE(page.find("-light.png\""), std::string::npos);
  EXPECT_NE(page.find("-dark.png\""), std::string::npos);
  EXPECT_NE(page.find("width=\"" + std::to_string(gate.previewSize.width()) + "\""),
            std::string::npos);
  EXPECT_EQ(page.find(".svg"), std::string::npos);
  std::ofstream(output / "obsolete.md") << "obsolete";
  std::ofstream(output / "obsolete.svg") << "obsolete SVG preview";
  docs::generateReference(output, registry, factory, app);
  expectSameContents(contents(output), first);

  auto missing = ui::GUIComponentFactory::empty();
  EXPECT_THROW(docs::generateReference(output, registry, missing, app),
               std::runtime_error);
  expectSameContents(contents(output), first);
}

TEST_F(ComponentDocumentation, RejectsMissingAndAmbiguousMappingsAndUnownedOutput)
{
  auto missing = ui::GUIComponentFactory::empty();
  EXPECT_THROW(docs::collectReferences(registry, missing), std::runtime_error);
  factory.registerType("Duplicate",
                       [](QGraphicsItem*) -> std::unique_ptr<ui::GraphicalComponent> {
                         throw std::logic_error(
                             "This duplicate must not be instantiated");
                       },
                       {.coreType = "AndGate"});
  EXPECT_THROW(docs::collectReferences(registry, factory), std::runtime_error);

  QTemporaryDir               temporary;
  const std::filesystem::path output(temporary.path().toStdString());
  std::ofstream(output / "manual.md") << "Preserve this file";
  auto& app = *qobject_cast<QApplication*>(QApplication::instance());
  EXPECT_THROW(docs::generateReference(output, registry, factory, app),
               std::runtime_error);
  EXPECT_EQ(contents(output).at("manual.md"), "Preserve this file");
}

}  // namespace

int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  for (const auto* font :
       {":/fonts/Quicksand.ttf", ":/fonts/Chango.ttf", ":/fonts/NovaMono.ttf"})
    if (QFontDatabase::addApplicationFont(font) < 0)
      return 1;
  QFont regular("Quicksand");
  regular.setPixelSize(14);
  QApplication::setFont(regular);
  return RUN_ALL_TESTS();
}
