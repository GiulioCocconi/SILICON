/*
  Copyright (c) 2026. Giulio Cocconi

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <QGraphicsItem>
#include <QGraphicsSvgItem>
#include <QPainter>
#include <QString>

#include <core/component.hpp>
#include <core/io.hpp>
#include <core/wire.hpp>

#include <string_view>

#include <ui/common/enums.hpp>
#include <ui/common/graphicalComponent.hpp>
#include <ui/logiFlow/components/graphicalLogicComponent.hpp>


namespace SILICON {
namespace ui {
using namespace SILICON::core;

/**
 * @class GraphicalConstant
 * @brief Displays a fixed-width value produced by an imported HDL netlist.
 */
class GraphicalConstant : public GraphicalLogicComponent {
  Q_OBJECT
public:
  explicit GraphicalConstant(QGraphicsItem* parent = nullptr);

  int type() const override { return SiliconTypes::CONSTANT; }

  void setComponent(const Component_ptr& component) override;

private:
  void setupCallbacks();
  void updateLayout();
  void updateLayout(std::string_view value);
};

/**
 * @class GraphicalIO
 * @brief Abstract base class for all graphical Input and Output components.
 */
class GraphicalIO : public GraphicalLogicComponent {
  Q_OBJECT
protected:
  /**
   * @brief Constructs a GraphicalIO item.
   * @param category The category of the item (Input or Output).
   * @param component The underlying core logic component.
   * @param shape The visual QGraphicsItem representing the component.
   * @param parent Optional parent QGraphicsItem.
   * @param scanShape Whether to automatically scan the shape for port locations.
   */
  GraphicalIO(ItemCategory category, const Component_ptr& component, QGraphicsItem* shape,
              QGraphicsItem* parent = nullptr, bool scanShape = false);

  /**
   * @brief Safely retrieves the user-defined name of this component.
   * @return A QString containing the name, or an empty string if none is set.
   */
  [[nodiscard]] QString getComponentName() const;

  /** @brief Installs the callback that moves the IO port when orientation changes. */
  void installPortOrientationCallback();

  /** @brief Rebuilds the single IO port based on the current orientation property. */
  void updatePortOrientation();

  /** @brief Rebuilds the single IO port using an explicit orientation value. */
  void updatePortOrientation(std::string_view orientation);

  /** @brief Returns true when the IO port is currently placed above the shape. */
  [[nodiscard]] bool isPortOrientationUp() const;

  /** @brief Returns the label rectangle for the current orientation. */
  [[nodiscard]] QRectF componentNameRect(const QString& name) const;

public:
  /**
   * @brief Shared, highly-optimized UI font for all IO components.
   * Using an inline static font prevents allocating a QFont object for every component.
   */
  static inline const QFont UI_FONT{"NovaMono", 12};

  /** @brief Returns the local port position for a proposed cardinal side. */
  [[nodiscard]] QPoint portPositionFor(PortSide side) const;

  /** @brief Returns local wire-obstacle bounds for a proposed port side. */
  [[nodiscard]] QRectF collisionRectForPortSide(PortSide side) const;

  /** @brief Persists and applies a cardinal port orientation. */
  void setPortOrientation(PortSide side);

  /**
   * @brief Called when the user clicks on the component during a live simulation.
   */
  virtual void handleSimulationClick() {}

  /**
   * @brief Applies the default starting value configured in the component's properties.
   */
  virtual void applyStartValue() = 0;

  /**
   * @brief Resets the visual state to its initial simulation state.
   */
  virtual void resetSimulationState() = 0;

  void setComponent(const Component_ptr& component) override;

signals:
  /**
   * @brief Emitted when an input component changes its state.
   * @param targetBus The bus that is being driven by this input.
   * @param value The new numerical value of the input.
   * @param source A weak pointer to the logic component that triggered the change.
   */
  void inputToggled(Bus targetBus, unsigned int value, Component_weakPtr source);
};

/**
 * @class GraphicalInput
 * @brief Represents a single-bit user-toggleable input switch in the UI.
 */
class GraphicalInput : public GraphicalIO {
  Q_OBJECT
public:
  static constexpr std::string_view ComponentType = "Input";

  explicit GraphicalInput(QGraphicsItem* parent = nullptr);

  /** @return The internal silicon type identifier for a single input. */
  int type() const override { return SiliconTypes::SINGLE_INPUT; }

  /** @return The standard string identifier for this component type. */
  [[nodiscard]] std::string getTypeName() const override
  {
    return std::string(ComponentType);
  }

  /**
   * @brief Retrieves the current state of the input.
   * @return State::HIGH if active, State::LOW otherwise.
   */
  State getState() const;

  /**
   * @brief Toggles the visual and logical state between HIGH and LOW.
   */
  void toggle();

  /**
   * @brief Explicitly sets the input state and updates visuals/simulation logic.
   * @param state The target state to set.
   */
  void setState(State state);

  void handleSimulationClick() override;
  void applyStartValue() override;
  void resetSimulationState() override;

  /** @brief Notifies the Qt framework that the bounding rect has changed. */
  void triggerGeometryChange() { prepareGeometryChange(); }

  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
             QWidget* widget) override;

private:
  State skinState = State::LOW;

  static const QString& getOnShapePath()
  {
    static const QString path = ":/other_components/input_on.svg";
    return path;
  }

  static const QString& getOffShapePath()
  {
    static const QString path = ":/other_components/input_off.svg";
    return path;
  }

  QRectF boundingRect() const override;
};

/**
 * @class GraphicalBusInput
 * @brief Represents a multi-bit user-editable data bus input in the UI.
 */
class GraphicalBusInput : public GraphicalIO {
  Q_OBJECT
public:
  static constexpr std::string_view ComponentType = "BusInput";

  explicit GraphicalBusInput(QGraphicsItem* parent = nullptr);

  int type() const override { return SiliconTypes::BUS_INPUT; }

  [[nodiscard]] std::string getTypeName() const override
  {
    return std::string(ComponentType);
  }

  void setComponent(const Component_ptr& component) override;

  /**
   * @brief Directly sets the numerical value of the bus.
   * @param value The value to apply (will be masked to fit bus width).
   */
  void setValue(unsigned int value);

  /**
   * @brief Opens a dialog prompting the user to type in a new bus value.
   */
  void editValue();

  /**
   * @brief Synchronizes the UI visuals with the underlying logic component's state.
   */
  void refreshFromComponent();

  void handleSimulationClick() override;
  void applyStartValue() override;
  void resetSimulationState() override;
  void triggerGeometryChange() { prepareGeometryChange(); }

  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
             QWidget* widget) override;

private:
  unsigned int currentValue = 0;

  void   propagateCurrentValue();
  void   installPropertyCallbacks();
  QRectF boundingRect() const override;
};

/**
 * @class GraphicalOutputSingle
 * @brief Represents a single-bit display output in the UI (e.g., an LED indicator).
 */
class GraphicalOutputSingle : public GraphicalIO {
  Q_OBJECT
public:
  static constexpr std::string_view ComponentType = "Output";

  explicit GraphicalOutputSingle(QGraphicsItem* parent = nullptr);

  int type() const override { return SiliconTypes::SINGLE_OUTPUT; }

  [[nodiscard]] std::string getTypeName() const override
  {
    return std::string(ComponentType);
  }

  /**
   * @brief Updates the visual representation based on the wire state.
   * @param state The incoming logic state.
   */
  void setState(State state);

  void applyStartValue() override;
  void resetSimulationState() override;

  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
             QWidget* widget) override;

private:
  static const QString& getUnknownShapePath()
  {
    static const QString path = ":/other_components/output_unknown.svg";
    return path;
  }
  static const QString& getOnShapePath()
  {
    static const QString path = ":/other_components/output_on.svg";
    return path;
  }
  static const QString& getOffShapePath()
  {
    static const QString path = ":/other_components/output_off.svg";
    return path;
  }

  QRectF boundingRect() const override;
};

/**
 * @class GraphicalBusOutput
 * @brief Represents a multi-bit data bus display output in the UI.
 */
class GraphicalBusOutput : public GraphicalIO {
  Q_OBJECT
public:
  static constexpr std::string_view ComponentType = "BusOutput";

  explicit GraphicalBusOutput(QGraphicsItem* parent = nullptr);

  int type() const override { return SiliconTypes::BUS_OUTPUT; }

  [[nodiscard]] std::string getTypeName() const override
  {
    return std::string(ComponentType);
  }

  void setComponent(const Component_ptr& component) override;

  /**
   * @brief Updates the display shape based on the state of the incoming bus.
   * Handles edge cases like Error/Unknown bits and widths that are too large to display
   * as hex.
   * @param bus The incoming bus.
   */
  void setBusState(const Bus& bus);

  void refreshFromComponent();
  void applyStartValue() override;
  void resetSimulationState() override;

  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
             QWidget* widget) override;

private:
  void   installPropertyCallbacks();
  QRectF boundingRect() const override;
};

}  // namespace ui
}  // namespace SILICON
