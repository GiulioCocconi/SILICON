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
#include <memory>
#include <ranges>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include <core/circuit.hpp>
#include <ui/common/diagramScene/diagramScene.hpp>
#include <ui/logiFlow/components/subcircuit/metadata.hpp>
#include <ui/logiFlow/components/subcircuit/utils.hpp>
#include <ui/serialization/gui_component_factory.hpp>

namespace SILICON::ui {
namespace {

  using SILICON::core::Circuit;
  using SILICON::project::Document;
  using SILICON::project::DocumentType;

  [[nodiscard]] Document
  materializeDocument(SILICON::conversion::SemanticDocument&& document)
  {
    if (auto* source = std::get_if<SILICON::conversion::VerilogSource>(&document.payload))
      return {std::move(document.path), std::move(source->contents)};

    auto circuit =
        std::make_shared<Circuit>(std::get<Circuit>(std::move(document.payload)));
    DiagramScene scene;
    scene.setSubcircuitDocumentMode(true);
    scene.loadCircuit(std::move(circuit), GUIComponentFactory::instance(), false);

    auto sceneJson                  = scene.serialize();
    auto completed                  = nlohmann::ordered_json::parse(sceneJson);
    completed["graphicalComponent"] = graphicalSubcircuitMetadataToJson(
        synchronizeGraphicalSubcircuitMetadata(sceneJson, GraphicalSubcircuitMetadata{}));
    return {std::move(document.path), completed.dump(2)};
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
      },
      DocumentConverter{
          .source            = DocumentType::Verilog,
          .target            = DocumentType::Circuit,
          .available         = YosysAvailable,
          .unavailableReason = YosysUnavailableReason,
      },
  };

}  // namespace

const DocumentConverter* documentConverterFor(const DocumentType source,
                                              const DocumentType target)
{
  const auto found =
      std::ranges::find_if(Converters, [source, target](const auto& converter) {
        return converter.source == source && converter.target == target;
      });
  return found == Converters.end() ? nullptr : std::to_address(found);
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
prepareDocumentConversion(const Document& source, const DocumentType target,
                          const std::span<const Document>         projectDocuments,
                          const SILICON::core::ComponentRegistry& registry,
                          const SILICON::core::CircuitResolver&   resolver)
{
  const auto* converter = documentConverterFor(source.getType(), target);
  if (!converter)
    throw std::invalid_argument("The selected document has no registered conversion");
  if (!converter->available)
    throw std::runtime_error(std::string(converter->unavailableReason));

  auto prepared = SILICON::conversion::prepareDocumentConversion(
      source, target, projectDocuments, registry, resolver);
  return {
      .choices = std::move(prepared.choices),
      .execute =
          [execute =
               std::move(prepared.execute)](const std::span<const std::string> selected) {
            auto                  semantic = execute(selected);
            std::vector<Document> documents;
            documents.reserve(semantic.documents.size());
            for (auto& document : semantic.documents)
              documents.push_back(materializeDocument(std::move(document)));
            return ConversionResult{.documents    = std::move(documents),
                                    .activatePath = std::move(semantic.activatePath)};
          },
  };
}

}  // namespace SILICON::ui
