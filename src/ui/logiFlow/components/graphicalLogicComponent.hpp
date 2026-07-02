/*
 Copyright (c) 2025. Giulio Cocconi

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

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <QGraphicsItem>
#include <QPainter>
#include <QPoint>
#include <QRect>

#include <core/component.hpp>
#include <core/wire.hpp>

#include <ui/common/diagramScene/diagramScene.hpp>
#include <ui/common/graphicalComponent.hpp>

/**
 * @class GraphicalLogicComponent
 * @brief Base class for logic-gate graphical components.
 *
 * GraphicalLogicComponent extends GraphicalComponent to add
 * logic-level functionality. It associates a logical Component
 * with its visual representation, enabling wire connection
 * and simulation integration.
 *
 * The component's input/output buses are synchronized with
 * its graphical ports for circuit connectivity.
 *
 * @see GraphicalComponent
 * @see Component
 */
class GraphicalLogicComponent : public GraphicalComponent {
  Q_OBJECT
protected:
  /** @brief The logical component this graphical component represents */
  Component_ptr associatedComponent;

  /** @brief Listener id for associated component I/O changes */
  uint64_t componentIOListenerId = 0;

  /** @brief Whether the component allows property editing */
  bool isEditable = false;

  /** @brief Installs the listener that keeps graphical ports synced to bus sizes */
  void refreshComponentIOListener();

  /** @brief Copies associated component bus sizes into graphical ports */
  virtual void updatePortSizes();

  /** @brief Checks whether a visible input port count is valid for this component */
  [[nodiscard]] virtual bool
  acceptsInputPortCount(size_t portCount, const std::vector<Bus>& componentInputs) const;

  /** @brief Returns the visible size for an input port */
  [[nodiscard]] virtual unsigned int
  inputPortSize(size_t portIndex, const std::vector<Bus>& componentInputs) const;

protected:
  explicit GraphicalLogicComponent(ItemCategory category, const Component_ptr& component,
                                   QGraphicsItem* shape, QGraphicsItem* parent,
                                   bool scanShape = false);

public:
  /**
   * @brief Constructs a logic component.
   *
   * @param component The logical component
   * @param shape The visual shape item
   * @param parent Optional parent graphics item
   * @param scanShape Enable alpha scanning for port placement
   */
  GraphicalLogicComponent(const Component_ptr& component, QGraphicsItem* shape,
                          QGraphicsItem* parent, bool scanShape = false);

  ~GraphicalLogicComponent() override;

  /**
   * @brief Sets ports and validates against component I/O.
   *
   * @param busToPortInputs Vector of (name, position) pairs for inputs
   * @param busToPortOutputs Vector of (name, position) pairs for outputs
   */
  void setPorts(const std::vector<PortPair>& busToPortInputs,
                const std::vector<PortPair>& busToPortOutputs) override;

  /**
   * @brief Assigns a bus connected to a visible input port.
   * @param portIndex Visible input port index
   * @param bus Connected wire bus
   */
  virtual void assignInputPortBus(unsigned int portIndex, const Bus& bus) const;

  /**
   * @brief Gets the associated logical component.
   * @return Pointer to the component
   */
  [[nodiscard]] Component_ptr getComponent() const { return associatedComponent; }

  /**
   * @brief Applies a component property while notifying Qt about possible geometry
   * changes.
   * @param key The property name
   * @param value The new property value
   */
  void applyProperty(std::string_view key, const PropertyValue& value);

  /**
   * @brief Sets the associated logical component.
   * @param component The component to associate
   */
  virtual void setComponent(const Component_ptr& component);

  [[nodiscard]] std::string getTypeName() const override
  {
    if (associatedComponent) {
      return std::string(associatedComponent->typeName());
    }
    return "Unknown";
  }
};
