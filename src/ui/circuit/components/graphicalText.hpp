/*
  Copyright (c) 2026. Giulio Cocconi

  SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <ui/circuit/diagram/graphicalComponent.hpp>

namespace SILICON::ui {

/** Text annotation supporting *bold*, _italics_, and $LaTeX math$. */
class GraphicalText : public GraphicalComponent {
public:
  static constexpr std::string_view COMPONENT_TYPE = "Text";

  explicit GraphicalText(QGraphicsItem* parent = nullptr);

  [[nodiscard]] int         type() const override { return TEXT; }
  [[nodiscard]] std::string getTypeName() const override
  {
    return std::string(COMPONENT_TYPE);
  }
  [[nodiscard]] const PropertyMap& getProperties() const override { return properties; }
  [[nodiscard]] std::optional<std::reference_wrapper<const StringPropertyOptions>>
                     getStringPropertyOptions(std::string_view key) const override;
  [[nodiscard]] bool isMultilineProperty(std::string_view key) const override
  {
    return key == "text";
  }
  [[nodiscard]] bool participatesInCollisions() const override { return false; }

  [[nodiscard]] std::span<const ResizeHandle> resizeHandles() const override
  {
    return AllResizeHandles;
  }
  [[nodiscard]] PropertyMap resizedProperties(QSizeF size, ResizeHandle handle,
                                              const PropertyMap& initial) override;

  void applyProperty(std::string_view key, const PropertyValue& value) override;
  [[nodiscard]] nlohmann::ordered_json serialize() const override;
  void loadSerializedState(const nlohmann::json& json) override;

private:
  void        setProperties(PropertyMap next);
  PropertyMap properties{{"text", std::string("Text")},
                         {"size", 12},
                         {"font", std::string("Quicksand")},
                         {"wrapWidth", 0}};
};

}  // namespace SILICON::ui
