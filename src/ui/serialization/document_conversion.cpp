/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#include "document_conversion.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include <core/circuit.hpp>
#include <core/serialization/component_registry.hpp>
#include <core/serialization/verilog.hpp>
#include <core/serialization/yosys/netlist.hpp>
#include <core/serialization/yosys/yosys_tool.hpp>
#include <core/subcircuitDefinition.hpp>
#include <ui/common/diagramScene/diagramScene.hpp>
#include <ui/logiFlow/components/subcircuit/metadata.hpp>
#include <ui/logiFlow/components/subcircuit/utils.hpp>
#include <ui/serialization/gui_component_factory.hpp>

namespace SILICON::ui {
namespace {

  using SILICON::core::Circuit;
  using SILICON::project::Document;
  using SILICON::project::DocumentType;

  [[nodiscard]] ConversionResult
  subcircuitToVerilog(const Document& source, const std::span<const std::string> selected)
  {
    if (!selected.empty())
      throw std::invalid_argument(
          "Circuit-to-Verilog conversion does not accept selections");

    const auto slug = SILICON::project::documentSlugForPath(source.getPath());
    if (source.getType() != DocumentType::Circuit || !slug)
      throw std::invalid_argument("Only circuits can be converted to Verilog");

    auto circuit =
        Circuit::deserialize(SILICON::core::extractCoreCircuitJson(source.getContents()),
                             SILICON::core::ComponentRegistry::instance());
    circuit.setName(*slug);

    const auto path =
        SILICON::project::documentPathForSlug(DocumentType::Verilog, *slug);
    std::vector<Document> documents;
    documents.emplace_back(path, SILICON::verilog::write(circuit));
    return {.documents = std::move(documents), .activatePath = path};
  }

  [[nodiscard]] PreparedDocumentConversion
  prepareSubcircuitToVerilog(const Document& source, std::span<const Document>)
  {
    return {
        .choices = {},
        .execute =
            [source](const std::span<const std::string> selected) {
              return subcircuitToVerilog(source, selected);
            },
    };
  }

  [[nodiscard]] Document graphicalDocument(const std::string& module,
                                           const std::string& designJson)
  {
    auto circuit =
        std::make_shared<Circuit>(SILICON::yosys::deserialize(designJson, module));
    DiagramScene scene;
    scene.setSubcircuitDocumentMode(true);
    scene.loadCircuit(std::move(circuit), GUIComponentFactory::instance(), false);

    auto sceneJson                  = scene.serialize();
    auto completed                  = nlohmann::ordered_json::parse(sceneJson);
    completed["graphicalComponent"] = graphicalSubcircuitMetadataToJson(
        synchronizeGraphicalSubcircuitMetadata(sceneJson, GraphicalSubcircuitMetadata{}));
    return preparedSubcircuitDocument(
        SILICON::project::documentPathForSlug(DocumentType::Circuit, module),
        completed.dump(2));
  }

  [[nodiscard]] PreparedDocumentConversion
  prepareVerilogToSubcircuits(const Document&                 source,
                              const std::span<const Document> projectDocuments)
  {
    if (source.getType() != DocumentType::Verilog) {
      throw std::invalid_argument(
          "Only Verilog code files can be converted to circuits");
    }

    std::vector<SILICON::verilog::SourceFile> sources;
    for (const auto& document : projectDocuments) {
      if (document.getType() == DocumentType::Verilog) {
        sources.push_back(
            {.path = document.getPath(), .contents = document.getContents()});
      }
    }

    auto designJson = SILICON::yosys::elaborateHierarchy(
        SILICON::verilog::read(sources, source.getPath()));
    auto modules = SILICON::yosys::moduleDependencyGraph(designJson);
    if (modules.modules().empty())
      throw std::runtime_error("Verilog source must declare at least one module");

    const auto allModules = modules.modules();
    static_cast<void>(modules.dependencyOrder(allModules));

    std::vector<ConversionChoice> choices;
    choices.reserve(allModules.size());
    for (const auto& module : allModules) {
      choices.push_back({.id           = module,
                         .label        = module,
                         .dependencies = modules.dependenciesOf(module)});
    }

    return {
        .choices = std::move(choices),
        .execute =
            [designJson = std::move(designJson),
             modules = std::move(modules)](const std::span<const std::string> selected) {
              if (selected.empty())
                throw std::invalid_argument("At least one module must be selected");

              const std::vector<std::string> roots(selected.begin(), selected.end());
              const auto                     exportOrder = modules.dependencyOrder(roots);
              for (const auto& module : exportOrder) {
                if (!SILICON::project::isValidDocumentSlug(module)) {
                  throw std::runtime_error(std::format(
                      "Verilog module '{}' cannot be used as a project circuit name",
                      module));
                }
              }

              std::vector<Document> generated;
              generated.reserve(exportOrder.size());
              for (const auto& module : exportOrder)
                generated.push_back(graphicalDocument(module, designJson));

              return ConversionResult{
                  .documents    = std::move(generated),
                  .activatePath = SILICON::project::documentPathForSlug(
                      DocumentType::Circuit, roots.front()),
              };
            },
    };
  }

#ifdef __EMSCRIPTEN__
  constexpr bool             YosysAvailable = false;
  constexpr std::string_view YosysUnavailableReason =
      "Conversion requires Yosys and is unavailable in the web build";
#else
  constexpr bool             YosysAvailable = true;
  constexpr std::string_view YosysUnavailableReason{};
#endif

  constexpr std::array Converters{
      DocumentConverter{
          .source            = DocumentType::Circuit,
          .target            = DocumentType::Verilog,
          .available         = YosysAvailable,
          .unavailableReason = YosysUnavailableReason,
          .prepare           = prepareSubcircuitToVerilog,
      },
      DocumentConverter{
          .source            = DocumentType::Verilog,
          .target            = DocumentType::Circuit,
          .available         = YosysAvailable,
          .unavailableReason = YosysUnavailableReason,
          .prepare           = prepareVerilogToSubcircuits,
      },
  };

}  // namespace

const DocumentConverter* documentConverterFor(const DocumentType source,
                                              const DocumentType target)
{
  const auto found = std::ranges::find_if(Converters, [source, target](const auto& converter) {
    return converter.source == source && converter.target == target;
  });
  return found == Converters.end() ? nullptr : &*found;
}

std::vector<const DocumentConverter*> documentConvertersFor(const DocumentType source)
{
  std::vector<const DocumentConverter*> result;
  for (const auto& converter : Converters) {
    if (converter.source == source)
      result.push_back(&converter);
  }
  return result;
}

PreparedDocumentConversion
prepareDocumentConversion(const Document&                 source,
                          const DocumentType               target,
                          const std::span<const Document> projectDocuments)
{
  const auto* converter = documentConverterFor(source.getType(), target);
  if (!converter)
    throw std::invalid_argument("The selected document has no registered conversion");
  if (!converter->available)
    throw std::runtime_error(std::string(converter->unavailableReason));
  return converter->prepare(source, projectDocuments);
}

}  // namespace SILICON::ui
