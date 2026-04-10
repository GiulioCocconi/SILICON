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

#include <QGraphicsItem>
#include <QGraphicsSvgItem>
#include <QPainter>

#include <QHBoxLayout>
#include <QLabel>

#include <core/component.hpp>
#include <core/wire.hpp>

#include <string_view>

#include <ui/common/enums.hpp>
#include <ui/common/graphicalComponent.hpp>
#include <ui/logiFlow/components/graphicalLogicComponent.hpp>

class GraphicalInput : public GraphicalLogicComponent {
  Q_OBJECT
public:
  static constexpr std::string_view ComponentType = "Input";

  explicit GraphicalInput(QGraphicsItem* parent = nullptr);
  int type() const override { return SiliconTypes::SINGLE_INPUT; }

  State getState();

  void toggle();

  void setState(State state);

  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
             QWidget* widget) override;

  void showPropertiesDialog() override;

  const QFont font = QFont("NovaMono", 12);

private slots:
  void propertiesDialogAccepted() override;

private:
  State skinState = State::LOW;

  QLineEdit* nameInput = new QLineEdit();

  const static QString& getOnShapePath()
  {
    static QString ON_SHAPE_PATH = ":/other_components/input_on.svg";
    return ON_SHAPE_PATH;
  }

  const static QString& getOffShapePath()
  {
    static QString OFF_SHAPE_PATH = ":/other_components/input_off.svg";
    return OFF_SHAPE_PATH;
  }

  QRectF boundingRect() const override;
};

class DummyInputComponent : public Component {
public:
  static constexpr std::string_view Type = "DummyInputComponent";

  DummyInputComponent(Bus bus, std::string name) : Component({}, {bus})
  {
    defineProperty("name", std::move(name));
  }
  void setState(int value) { this->outputs[0].setCurrentValue(value, weak_from_this()); };
  std::string_view typeName() const override { return Type; }
};

class GraphicalOutputSingle : public GraphicalLogicComponent {
  Q_OBJECT
public:
  static constexpr std::string_view ComponentType = "Output";

  explicit GraphicalOutputSingle(QGraphicsItem* parent = nullptr);
  int type() const override { return SiliconTypes::SINGLE_OUTPUT; }

  void setState(State state);

private:
  const static QString& getOnShapePath()
  {
    static QString ON_SHAPE_PATH = ":/other_components/output_on.svg";
    return ON_SHAPE_PATH;
  }

  const static QString& getOffShapePath()
  {
    static QString OFF_SHAPE_PATH = ":/other_components/output_off.svg";
    return OFF_SHAPE_PATH;
  }
};

class DummyOutputComponent : public Component {
public:
  static constexpr std::string_view Type = "DummyOutputComponent";

  DummyOutputComponent(Bus bus, std::string name);
  void             setSkin(GraphicalOutputSingle* skin);
  std::string_view typeName() const override { return Type; }

private:
  GraphicalOutputSingle* skin = nullptr;
};
