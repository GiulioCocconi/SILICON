/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <QString>

#include <core/projectCircuitResolver.hpp>
#include <core/projectContext.hpp>
#include <core/serialization/component_registry.hpp>
#include <core/serialization/projectFile.hpp>

namespace SILICON::ui {

/** Project lifetime and persistence state shared by the editor controllers. */
struct ProjectSession {
  QString                                          currentFileName;
  std::optional<SILICON::project::ProjectMetadata> currentProjectMetadata;
  std::optional<SILICON::project::ProjectInfo>     currentProjectInfo;
  std::string                                      activeDocumentPath;

  SILICON::project::ProjectContext         projectContext;
  SILICON::project::ProjectCircuitResolver circuitResolver{
      projectContext, SILICON::core::ComponentRegistry::instance()};

  [[nodiscard]] std::optional<std::string>
  firstCircuitPath(std::string_view excludedPath = {}) const;
};

}  // namespace SILICON::ui
