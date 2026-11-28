/*
  Copyright (c) 2026. Giulio Cocconi
  SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <reference.hpp>

#include <core/register.hpp>
#include <core/serialization/component_registration.hpp>
#include <ui/circuit/components/graphicalRegister.hpp>
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

TEST_F(ComponentDocumentation, CoversCatalogAndFullSchemas)
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

TEST_F(ComponentDocumentation, CapturesEveryRegisterModeAndItsActualPins)
{
  const auto references = docs::collectReferences(registry, factory);
  const auto reg = std::ranges::find(references, "Register", &docs::Reference::type);
  ASSERT_NE(reg, references.end());
  ASSERT_EQ(reg->configurations.size(), 3);
  EXPECT_EQ(reg->defaultProperties.at("inputType"),
            core::PropertyValue(std::string("Parallel")));
  EXPECT_EQ(reg->defaultProperties.at("outputType"),
            core::PropertyValue(std::string("Parallel")));
  EXPECT_EQ(reg->defaultProperties.at("size"), core::PropertyValue(2));
  EXPECT_EQ(reg->ports.front().name, "d");
  EXPECT_EQ(reg->ports.back().name, "q");
  EXPECT_EQ(reg->ports.size(), 5);
  for (const auto& configuration : reg->configurations) {
    SCOPED_TRACE(configuration.id);
    const bool  serialInput  = configuration.id != "piso";
    const bool  serialOutput = configuration.id != "sipo";
    const auto& values       = configuration.effectiveProperties;
    EXPECT_EQ(values.at("inputType"),
              core::PropertyValue(std::string(serialInput ? "Serial" : "Parallel")));
    EXPECT_EQ(values.at("outputType"),
              core::PropertyValue(std::string(serialOutput ? "Serial" : "Parallel")));
    EXPECT_EQ(values.at("size"), core::PropertyValue(2));
    ASSERT_EQ(configuration.ports.size(), configuration.id == "piso" ? 6 : 5);
    EXPECT_EQ(configuration.ports.front().name, serialInput ? "si" : "d");
    EXPECT_EQ(configuration.ports.front().width, serialInput ? 1 : 2);
    EXPECT_EQ(configuration.ports.back().name, serialOutput ? "so" : "q");
    EXPECT_EQ(configuration.ports.back().width, serialOutput ? 1 : 2);
    const auto load =
        std::ranges::find(configuration.ports, "ld", &core::PortDescription::name);
    EXPECT_EQ(load != configuration.ports.end(), configuration.id == "piso");
    ASSERT_EQ(configuration.portLabels.size(), configuration.ports.size());
    for (std::size_t i = 0; i < configuration.ports.size(); ++i)
      EXPECT_EQ(configuration.portLabels[i], configuration.ports[i].name);
  }
  const auto page = docs::componentPage(*reg);
  EXPECT_NE(page.find("| <code v-pre>ld</code> | Input | 1 |"), std::string::npos);
  EXPECT_NE(page.find("| <code v-pre>si</code> | Input | 1 |"), std::string::npos);
  EXPECT_NE(page.find("| <code v-pre>so</code> | Output | 1 |"), std::string::npos);
}

TEST_F(ComponentDocumentation, CoversMemoryGateAndRoutingLayouts)
{
  const auto references = docs::collectReferences(registry, factory);
  const auto find       = [&](std::string_view type) -> const docs::Reference& {
    const auto found = std::ranges::find(references, type, &docs::Reference::type);
    if (found == references.end())
      throw std::runtime_error("Missing component: " + std::string(type));
    return *found;
  };
  const auto& memory = find("Memory");
  ASSERT_EQ(memory.configurations.size(), 3);
  for (const auto& configuration : memory.configurations) {
    SCOPED_TRACE(configuration.id);
    const bool ram   = configuration.id.starts_with("ram");
    const int  lanes = configuration.id == "ram" ? 1 : 2;
    EXPECT_EQ(configuration.effectiveProperties.at("readPorts"),
              core::PropertyValue(lanes));
    EXPECT_EQ(configuration.effectiveProperties.at("addressWidth"),
              core::PropertyValue(1));
    EXPECT_EQ(configuration.effectiveProperties.at("dataWidth"), core::PropertyValue(8));
    EXPECT_EQ(configuration.ports.size(), lanes * 2 + (ram ? 4 : 0));
    const auto clock =
        std::ranges::find(configuration.ports, "clk", &core::PortDescription::name);
    EXPECT_EQ(clock != configuration.ports.end(), ram);
    if (ram) {
      const auto mask =
          std::ranges::find(configuration.ports, "WE", &core::PortDescription::name);
      ASSERT_NE(mask, configuration.ports.end());
      EXPECT_EQ(mask->width, 8);
    }
  }
  for (const auto* type : {"AndGate", "OrGate", "NandGate", "NorGate", "XorGate"}) {
    const auto& gate = find(type);
    ASSERT_EQ(gate.configurations.size(), 2);
    const auto& reduction = gate.configurations[0];
    ASSERT_EQ(reduction.ports.size(), 2);
    EXPECT_EQ(reduction.ports.front().width, 4);
    EXPECT_EQ(reduction.ports.back().width, 1);
    const auto& bitwise = gate.configurations[1];
    ASSERT_EQ(bitwise.ports.size(), gate.ports.size());
    for (const auto& port : bitwise.ports)
      EXPECT_EQ(port.width, 4);
  }
  const auto& mux = find("Multiplexer");
  ASSERT_EQ(mux.configurations.size(), 3);
  for (const auto& configuration : mux.configurations) {
    ASSERT_EQ(configuration.ports.size(), 4);
    EXPECT_EQ(configuration.ports[2].name, "s");
    EXPECT_EQ(configuration.ports[2].width, configuration.id == "bus" ? 1 : 2);
    EXPECT_EQ(configuration.ports.back().width, configuration.id == "remapped" ? 1 : 4);
  }
  const auto& demux = find("Demultiplexer");
  ASSERT_EQ(demux.configurations.size(), 1);
  ASSERT_EQ(demux.configurations[0].ports.size(), 4);
  EXPECT_EQ(demux.configurations[0].ports[2].name, "o0");
  EXPECT_EQ(demux.configurations[0].ports[3].name, "o1");
  EXPECT_EQ(demux.configurations[0].ports[3].width, 4);
  const auto& splitter = find("WireSplitter");
  ASSERT_EQ(splitter.configurations.size(), 2);
  ASSERT_EQ(splitter.configurations[0].ports.size(), 3);
  EXPECT_EQ(splitter.configurations[0].ports[1].name, "b[1:0]");
  EXPECT_EQ(splitter.configurations[0].ports[2].name, "b[3:2]");
  EXPECT_EQ(splitter.configurations[1].ports[1].name, "b[2:0]");
  EXPECT_EQ(splitter.configurations[1].ports[2].name, "b[3:1]");
  const auto& merger = find("WireMerger");
  ASSERT_EQ(merger.configurations.size(), 1);
  ASSERT_EQ(merger.configurations[0].ports.size(), 3);
  EXPECT_EQ(merger.configurations[0].ports[0].width, 1);
  EXPECT_EQ(merger.configurations[0].ports[1].width, 3);
  EXPECT_EQ(merger.configurations[0].ports[2].width, 4);
  const auto& extender = find("Extender");
  ASSERT_EQ(extender.configurations.size(), 1);
  EXPECT_EQ(extender.configurations[0].ports.front().width, 8);
  EXPECT_EQ(extender.configurations[0].ports.back().width, 4);
  for (const auto* type : {"Shifter", "Comparator", "Multiplier", "Divider"}) {
    const auto& entry = find(type);
    EXPECT_TRUE(entry.configurations.empty());
    const auto page = docs::componentPage(entry);
    EXPECT_NE(page.find("Options sharing a pin layout"), std::string::npos);
    EXPECT_NE(page.find("[<code v-pre>signed</code>](#property-signed)"),
              std::string::npos);
  }
}

TEST_F(ComponentDocumentation, LinksGalleryCaptionsToEveryPinTable)
{
  for (const auto& reference : docs::collectReferences(registry, factory)) {
    SCOPED_TRACE(reference.type);
    const auto page = docs::componentPage(reference);
    EXPECT_NE(page.find("class=\"configuration-gallery\" data-count=\""
                        + std::to_string(1 + reference.configurations.size()) + "\""),
              std::string::npos);
    EXPECT_NE(page.find("href=\"#pins-default\""), std::string::npos);
    EXPECT_NE(page.find("{#pins-default}"), std::string::npos);
    EXPECT_EQ(page.find("pins belong to the default"), std::string::npos);
    for (const auto& configuration : reference.configurations) {
      const auto id = docs::typeSlug(configuration.id);
      EXPECT_NE(page.find("href=\"#pins-configuration-" + id + "\""), std::string::npos);
      EXPECT_NE(page.find("{#pins-configuration-" + id + "}"), std::string::npos);
      for (const auto& key : reference.metadata.configurationProperties)
        EXPECT_TRUE(configuration.effectiveProperties.contains(key)) << key;
    }
  }
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
  auto configured              = entry;
  configured.defaultProperties = {{"key", std::string("<default> {{ value }}")}};
  docs::Configuration variant;
  variant.id                  = "a/b";
  variant.title               = "<Mode> {{ value }}";
  variant.description         = "a < b & c";
  variant.effectiveProperties = {{"key", std::string("<variant> {{ value }}")}};
  variant.ports = {{"p|q", core::PortDirection::Input, 0, 1, "<description>"}};
  configured.configurations.push_back(variant);
  const auto page = docs::componentPage(configured);
  EXPECT_NE(page.find("&lt;default&gt;"), std::string::npos);
  EXPECT_NE(page.find("&lt;variant&gt;"), std::string::npos);
  EXPECT_NE(page.find("&lt;Mode&gt;"), std::string::npos);
  EXPECT_NE(page.find("{#pins-configuration-a-2fb}"), std::string::npos);
  EXPECT_NE(page.find("p&#124;q"), std::string::npos);
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
  std::size_t expectedPngCount = 0;
  for (const auto& reference : docs::collectReferences(registry, factory))
    expectedPngCount += (1 + reference.configurations.size()) * 2;
  EXPECT_EQ(pngCount, expectedPngCount);
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

TEST_F(ComponentDocumentation, ValidatesConfiguredPinsBeforeReplacingOutput)
{
  class BadRegister : public core::Register {
  public:
    using core::Register::Register;
    std::vector<core::PortDescription> portDescriptions() const override
    {
      auto result = core::Register::portDescriptions();
      if (getPropertyValue<std::string>("outputType") == "Serial")
        result.back().index =
            1;  // The default is valid; only the PISO variant is broken.
      return result;
    }
  };
  auto badRegistry = core::ComponentRegistry::empty();
  badRegistry.registerType("Register", [] { return std::make_shared<BadRegister>(); });
  auto badFactory = ui::GUIComponentFactory::empty();
  badFactory.registerType("BadRegister",
                          [](QGraphicsItem* parent) {
                            auto graphical =
                                std::make_unique<ui::GraphicalRegister>(parent);
                            graphical->setComponent(std::make_shared<BadRegister>(
                                core::Bus(2), core::Wire_ptr{}, core::Wire_ptr{},
                                core::Wire_ptr{}, core::Bus(2)));
                            return graphical;
                          },
                          {.coreType = "Register"});
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const std::filesystem::path output(temporary.path().toStdString());
  std::ofstream(output / ".silicon-component-reference") << "Generated output";
  std::ofstream(output / "previous.md") << "Previous reference";
  auto& app = *qobject_cast<QApplication*>(QApplication::instance());
  EXPECT_THROW(docs::generateReference(output, badRegistry, badFactory, app),
               std::runtime_error);
  EXPECT_EQ(contents(output).at("previous.md"), "Previous reference");
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
