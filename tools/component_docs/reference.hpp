/*
  Copyright (c) 2026. Giulio Cocconi
  SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <core/component.hpp>
#include <core/serialization/component_registry.hpp>
#include <ui/serialization/gui_component_factory.hpp>

#include <QSize>

#include <filesystem>
#include <string>
#include <vector>

class QApplication;

namespace SILICON::docs {

struct Reference {
  std::string                           type{};
  std::string                           guiType{};
  core::ComponentMetadata               metadata{};
  std::vector<core::PropertyDefinition> properties{};
  std::vector<core::PortDescription>    ports{};
  std::vector<std::string>              portLabels{};
  QSize                                 previewSize{};
};

std::string            categorySlug(core::ComponentCategory category);
std::string            typeSlug(std::string_view type);
std::string            componentPage(const Reference& reference);
std::vector<Reference> collectReferences(const core::ComponentRegistry& registry,
                                         const ui::GUIComponentFactory& factory);
void                   generateReference(const std::filesystem::path&   output,
                                         const core::ComponentRegistry& registry,
                                         const ui::GUIComponentFactory& factory, QApplication& app);

}  // namespace SILICON::docs
