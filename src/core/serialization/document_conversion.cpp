/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#include "document_conversion.hpp"

#include <format>
#include <stdexcept>
#include <utility>

#include <core/circuitDocument.hpp>
#include <core/serialization/component_registry.hpp>
#include <core/serialization/verilog.hpp>
#include <core/serialization/yosys/netlist.hpp>
#include <core/serialization/yosys/yosys_tool.hpp>
#include <core/sourceFile.hpp>

namespace SILICON::conversion {
namespace {

  using SILICON::project::Document;
  using SILICON::project::DocumentType;

  struct YosysDesign {
    std::string                           json;
    SILICON::yosys::ModuleDependencyGraph modules;
  };

  PreparedSemanticConversion
  prepareCircuitToVerilog(const Document&                         source,
                          const SILICON::core::ComponentRegistry& registry,
                          const SILICON::core::CircuitResolver&   resolver)
  {
    const auto slug = SILICON::project::documentSlugForPath(source.getPath());
    if (source.getType() != DocumentType::Circuit || !slug)
      throw std::invalid_argument("Only circuits can be converted to Verilog");

    auto circuit =
        SILICON::core::parseCircuitDocument(source.getContents(), registry, &resolver)
            .circuit;
    circuit.setName(*slug);

    const auto path = SILICON::project::documentPathForSlug(DocumentType::Verilog, *slug);
    return {
        .choices = {},
        .execute =
            [path,
             circuit = std::move(circuit)](const std::span<const std::string> selected) {
              if (!selected.empty())
                throw std::invalid_argument(
                    "Circuit-to-Verilog conversion does not accept selections");
              std::vector<SemanticDocument> documents;
              documents.push_back({.path    = path,
                                   .payload = VerilogSource{
                                       .contents = SILICON::verilog::write(circuit)}});
              return SemanticConversionResult{.documents    = std::move(documents),
                                              .activatePath = path};
            },
    };
  }

  PreparedSemanticConversion
  prepareVerilogToCircuits(const Document&                 source,
                           const std::span<const Document> projectDocuments)
  {
    if (source.getType() != DocumentType::Verilog)
      throw std::invalid_argument("Only Verilog files can be converted to circuits");

    std::vector<SILICON::core::SourceFile> sources;
    for (const auto& document : projectDocuments) {
      if (document.getType() == DocumentType::Verilog)
        sources.push_back(
            {.path = document.getPath(), .contents = document.getContents()});
    }

    YosysDesign design{
        .json = SILICON::yosys::elaborateHierarchy(
            SILICON::verilog::read(sources, source.getPath())),
        .modules = {},
    };
    design.modules = SILICON::yosys::moduleDependencyGraph(design.json);
    if (design.modules.modules().empty())
      throw std::runtime_error("Verilog source must declare at least one module");

    const auto allModules = design.modules.modules();
    static_cast<void>(design.modules.dependencyOrder(allModules));

    std::vector<ConversionChoice> choices;
    choices.reserve(allModules.size());
    for (const auto& module : allModules) {
      choices.push_back({.id           = module,
                         .label        = module,
                         .dependencies = design.modules.dependenciesOf(module)});
    }

    return {
        .choices = std::move(choices),
        .execute =
            [design = std::move(design)](const std::span<const std::string> selected) {
              if (selected.empty())
                throw std::invalid_argument("At least one module must be selected");

              const std::vector<std::string> roots(selected.begin(), selected.end());
              const auto exportOrder = design.modules.dependencyOrder(roots);

              std::vector<SemanticDocument> generated;
              generated.reserve(exportOrder.size());
              for (const auto& module : exportOrder) {
                if (!SILICON::project::isValidDocumentSlug(module)) {
                  throw std::runtime_error(std::format(
                      "Verilog module '{}' cannot be used as a project circuit name",
                      module));
                }
                generated.push_back({
                    .path = SILICON::project::documentPathForSlug(DocumentType::Circuit,
                                                                  module),
                    .payload = SILICON::yosys::deserialize(design.json, module),
                });
              }

              return SemanticConversionResult{
                  .documents    = std::move(generated),
                  .activatePath = SILICON::project::documentPathForSlug(
                      DocumentType::Circuit, roots.front()),
              };
            },
    };
  }

}  // namespace

PreparedSemanticConversion
prepareDocumentConversion(const Document& source, const DocumentType target,
                          const std::span<const Document>         projectDocuments,
                          const SILICON::core::ComponentRegistry& registry,
                          const SILICON::core::CircuitResolver&   resolver)
{
  if (source.getType() == DocumentType::Circuit && target == DocumentType::Verilog)
    return prepareCircuitToVerilog(source, registry, resolver);
  if (source.getType() == DocumentType::Verilog && target == DocumentType::Circuit)
    return prepareVerilogToCircuits(source, projectDocuments);
  throw std::invalid_argument("The selected document has no registered conversion");
}

}  // namespace SILICON::conversion
