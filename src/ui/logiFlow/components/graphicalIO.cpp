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

#include "graphicalIO.hpp"
#include <core/simulator.hpp>
#include <stdexcept>

GraphicalInput::GraphicalInput(QGraphicsItem* parent)
  : GraphicalLogicComponent(std::make_shared<DummyInputComponent>(Bus(1), "in"),
                            new QGraphicsSvgItem(":/other_components/input_off.svg"),
                            parent)
{
  isEditable = false;

  setPorts({}, {std::pair<std::string, QPoint>{"o", QPoint(20, 60)}});
  setState(State::LOW);

  this->associatedComponent->setPropertyCallback("name",
                                                 [this](const PropertyValue& value) {
                                                   prepareGeometryChange();
                                                   return value;
                                                 });

  auto nameLayout = new QHBoxLayout();
  auto nameLabel  = new QLabel("Name:");

  nameLayout->addWidget(nameLabel);
  nameLayout->addWidget(nameInput);

  auto propertiesWidget = new QWidget();
  propertiesWidget->setLayout(nameLayout);

  propertiesDialog = new PropertiesDialog({propertiesWidget});

  connect(this->propertiesDialog, &PropertiesDialog::accepted, this,
          &GraphicalInput::propertiesDialogAccepted);

  connect(this->propertiesDialog, &PropertiesDialog::rejected, this,
          &GraphicalInput::propertiesDialogRejected);

  GraphicalInput::showPropertiesDialog();
}

void GraphicalInput::toggle()
{
  setState(!skinState);
}

void GraphicalInput::setState(State state)
{
  this->skinState = state;

  const QString shapePath =
      (skinState == State::HIGH) ? getOnShapePath() : getOffShapePath();
  setItemShape(new QGraphicsSvgItem(shapePath));

  const auto targetBus = this->getComponent()->getOutputs()[0];
  const auto value     = (state == State::HIGH) ? 1 : 0;
  emit       inputToggled(targetBus, value, getComponent()->weak_from_this());
}

void GraphicalInput::paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
                           QWidget* widget)
{
  painter->setFont(font);
  painter->drawText(QPointF(0, -1), QString::fromStdString(std::get<std::string>(
                                        *this->getComponent()->getProperty("name"))));

  GraphicalLogicComponent::paint(painter, option, widget);
}

void GraphicalInput::showPropertiesDialog()
{
  nameInput->setText(QString::fromStdString(
      std::get<std::string>(*this->getComponent()->getProperty("name"))));
  GraphicalLogicComponent::showPropertiesDialog();
}

void GraphicalInput::propertiesDialogAccepted()
{
  const auto newName = nameInput->text().toStdString();
  this->associatedComponent->setProperty("name", newName);

  prepareGeometryChange();
}

QRectF GraphicalInput::boundingRect() const
{
  const auto fontHeight = QFontMetrics(font).height();
  auto       rect       = GraphicalLogicComponent::boundingRect();
  // Add some extra space for the name
  return rect.adjusted(0, -fontHeight, 0, 0);
}

State GraphicalInput::getState()
{
  const int value = this->getComponent()->getOutputs()[0].getCurrentValue();
  return (value == 1) ? State::HIGH : State::LOW;
}

// --- Graphical Output ------------------------------------------------------------------

GraphicalOutputSingle::GraphicalOutputSingle(QGraphicsItem* parent)
  : GraphicalLogicComponent(std::make_shared<DummyOutputComponent>(Bus(1), "out"),
                            new QGraphicsSvgItem(":/other_components/output_off.svg"),
                            parent)
{
  isEditable = false;
  setPorts({std::pair<std::string, QPoint>{"in", QPoint(20, 60)}}, {});

  this->associatedComponent->setPropertyCallback(
      "name", [](const PropertyValue& value) { return value; });

  (std::static_pointer_cast<DummyOutputComponent>(associatedComponent))->setSkin(this);
}

void GraphicalOutputSingle::setState(State state)
{
  QString shapePath = (state == State::HIGH) ? getOnShapePath() : getOffShapePath();
  setItemShape(new QGraphicsSvgItem(shapePath));
}

// --- Dummy Output Component ------------------------------------------------------------

DummyOutputComponent::DummyOutputComponent(Bus bus, std::string name)
  : Component({std::move(bus)}, {})
{
  defineProperty("name", std::move(name));
}

void DummyOutputComponent::simulate(Simulator& /*sim*/)
{
  if (this->skin) {
    State s = Wire::safeGetCurrentState(this->inputs[0][0]);
    this->skin->setState(s);
  }
}

void DummyOutputComponent::setSkin(GraphicalOutputSingle* skin)
{
  if (!skin)
    throw std::invalid_argument("setSkin: skin must not be null");
  this->skin = skin;
}
