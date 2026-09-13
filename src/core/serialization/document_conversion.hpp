/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#pragma once

#include <functional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include <core/circuit.hpp>
#include <core/projectDocument.hpp>

namespace SILICON::core {
class ComponentRegistry;
class CircuitResolver;
}  // namespace SILICON::core

namespace SILICON::conversion {

struct VerilogSource {
  std::string contents;
};

struct ConversionChoice {
  std::string              id;
  std::string              label;
  std::vector<std::string> dependencies;
};

using SemanticPayload = std::variant<VerilogSource, SILICON::core::Circuit>;

struct SemanticDocument {
  std::string     path;
  SemanticPayload payload;
};

struct SemanticConversionResult {
  std::vector<SemanticDocument> documents;
  std::string                   activatePath;
};

struct PreparedSemanticConversion {
  std::vector<ConversionChoice>                                         choices;
  std::function<SemanticConversionResult(std::span<const std::string>)> execute;
};

[[nodiscard]] PreparedSemanticConversion
prepareDocumentConversion(const SILICON::project::Document&           source,
                          SILICON::project::DocumentType              target,
                          std::span<const SILICON::project::Document> projectDocuments,
                          const SILICON::core::ComponentRegistry&     registry,
                          const SILICON::core::CircuitResolver&       resolver);

}  // namespace SILICON::conversion
