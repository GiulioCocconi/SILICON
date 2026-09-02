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
#include <string_view>
#include <vector>

#include <core/projectDocument.hpp>

namespace SILICON::ui {

struct ConversionChoice {
  std::string              id;
  std::string              label;
  std::vector<std::string> dependencies;
};

struct ConversionResult {
  std::vector<SILICON::project::Document> documents;
  std::string                             activatePath;
};

struct PreparedDocumentConversion {
  std::vector<ConversionChoice>                                 choices;
  std::function<ConversionResult(std::span<const std::string>)> execute;
};

struct DocumentConverter {
  using Prepare = PreparedDocumentConversion (*)(
      const SILICON::project::Document&, std::span<const SILICON::project::Document>);

  SILICON::project::DocumentType source;
  SILICON::project::DocumentType target;
  bool                           available;
  std::string_view               unavailableReason;
  Prepare                        prepare;
};

[[nodiscard]] const DocumentConverter*
documentConverterFor(SILICON::project::DocumentType source,
                     SILICON::project::DocumentType target);

[[nodiscard]] std::vector<const DocumentConverter*>
documentConvertersFor(SILICON::project::DocumentType source);

[[nodiscard]] PreparedDocumentConversion
prepareDocumentConversion(const SILICON::project::Document&           source,
                          SILICON::project::DocumentType               target,
                          std::span<const SILICON::project::Document> projectDocuments);

}  // namespace SILICON::ui
