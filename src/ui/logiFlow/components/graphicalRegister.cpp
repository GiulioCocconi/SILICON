/*
  Copyright (C) 2026 Giulio Cocconi

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

#include "graphicalRegister.hpp"

#include <stdexcept>
#include <utility>
#include <variant>

#include <QGraphicsRectItem>
#include <QPainter>
#include <QTextOption>

#include <ui/common/theme.hpp>


namespace SILICON {
namespace ui {
using namespace SILICON::core;

namespace {

constexpr QRectF RegisterBodyRect(0.0, 0.0, 100.0, 80.0);

std::shared_ptr<Register> makeRegister()
{
  return std::make_shared<Register>(Bus(2), Wire_ptr{}, Wire_ptr{}, Wire_ptr{}, Bus(2));
}

QString registerLabel(const std::string& inputType, const std::string& outputType)
{
  const QString inputPrefix = inputType == Register::ParallelType ? "Pi" : "Si";
  const QString outputSuffix = outputType == Register::ParallelType ? "Po" : "So";
  return inputPrefix + outputSuffix + "\nRegister";
}

class RegisterShape : public QGraphicsRectItem {
public:
  explicit RegisterShape(QString label, QGraphicsItem* parent = nullptr)
    : QGraphicsRectItem(RegisterBodyRect, parent), label(std::move(label))
  {
  }

  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
             QWidget* widget) override
  {
    Q_UNUSED(option);
    Q_UNUSED(widget);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setPen(QPen(ThemeEngine::getColor("SILICON_INK"), 3));
    painter->setBrush(ThemeEngine::getColor("SILICON_INTERNAL"));
    painter->drawRect(rect());

    painter->setPen(QPen(ThemeEngine::getColor("SILICON_INK")));
    painter->setFont(QFont("Quicksand", 8, QFont::Bold));
    painter->drawText(rect().adjusted(6, 26, -6, -6), label,
                      QTextOption(Qt::AlignCenter));
    painter->restore();
  }

private:
  QString label;
};

}  // namespace

GraphicalRegister::GraphicalRegister(QGraphicsItem* parent)
  : GraphicalLogicComponent(makeRegister(),
                            new RegisterShape(registerLabel("Parallel", "Parallel")),
                            parent)
{
  isEditable     = true;
  printPortNames = true;
  setupCallbacks();
  updateLayout();
}

Register* GraphicalRegister::getComponentAsRegister() const
{
  auto* reg = dynamic_cast<Register*>(associatedComponent.get());
  if (!reg)
    throw std::logic_error("GraphicalRegister requires a Register component");
  return reg;
}

void GraphicalRegister::setupCallbacks()
{
  if (!associatedComponent)
    return;

  associatedComponent->setPropertyCallback("size", [this](const PropertyValue& value) {
    return applySize(std::get<int>(value));
  });
  associatedComponent->setPropertyCallback("inputType", [this](const PropertyValue& value) {
    return applyInputType(std::get<std::string>(value));
  });
  associatedComponent->setPropertyCallback("outputType", [this](const PropertyValue& value) {
    return applyOutputType(std::get<std::string>(value));
  });
}

void GraphicalRegister::updateLayout()
{
  const std::string inputType =
      associatedComponent->getPropertyValue<std::string>("inputType")
          .value_or(std::string(Register::ParallelType));
  const std::string outputType =
      associatedComponent->getPropertyValue<std::string>("outputType")
          .value_or(std::string(Register::ParallelType));

  updateLayout(inputType, outputType);
}

void GraphicalRegister::updateLayout(const std::string& inputType,
                                     const std::string& outputType)
{
  setItemShape(new RegisterShape(registerLabel(inputType, outputType)));

  const bool needsLoad = inputType == Register::ParallelType && outputType == Register::SerialType;

  const QString dataInput = inputType == Register::ParallelType ? "d" : "si";
  const QString dataOutput = outputType == Register::ParallelType ? "q" : "so";

  std::vector<PortPair> inputs{PortPair{dataInput, QPoint(-20, 20)},
                               PortPair{"clk", QPoint(-20, 40)},
                               PortPair{"en", QPoint(-20, 60)},
                               PortPair{"clr", needsLoad ? QPoint(80, -20) : QPoint(50, -20)}};
  if (needsLoad)
    inputs.push_back(PortPair{"ld", QPoint(50, -20)});

  setPorts(inputs, {PortPair{dataOutput, QPoint(120, 40)}});
}

int GraphicalRegister::applySize(const int size)
{
  const int appliedSize = getComponentAsRegister()->setSize(size);
  updateLayout();
  return appliedSize;
}

std::string GraphicalRegister::applyInputType(const std::string& inputType)
{
  const std::string appliedInputType = getComponentAsRegister()->setInputType(inputType);
  updateLayout(appliedInputType,
               associatedComponent->getPropertyValue<std::string>("outputType")
                   .value_or(std::string(Register::ParallelType)));
  return appliedInputType;
}

std::string GraphicalRegister::applyOutputType(const std::string& outputType)
{
  const std::string appliedOutputType =
      getComponentAsRegister()->setOutputType(outputType);
  updateLayout(associatedComponent->getPropertyValue<std::string>("inputType")
                   .value_or(std::string(Register::ParallelType)),
               appliedOutputType);
  return appliedOutputType;
}

void GraphicalRegister::setComponent(const Component_ptr& component)
{
  GraphicalLogicComponent::setComponent(component);
  if (!component)
    return;

  setupCallbacks();
  getComponentAsRegister()->setSize(component->getPropertyValue<int>("size").value_or(2));
  getComponentAsRegister()->setInputType(
      component->getPropertyValue<std::string>("inputType")
          .value_or(std::string(Register::ParallelType)));
  getComponentAsRegister()->setOutputType(
      component->getPropertyValue<std::string>("outputType")
          .value_or(std::string(Register::ParallelType)));
  updateLayout();
}

}  // namespace ui
}  // namespace SILICON
