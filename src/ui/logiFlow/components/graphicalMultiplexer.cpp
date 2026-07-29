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

#include "graphicalMultiplexer.hpp"

#include <algorithm>
#include <stdexcept>
#include <variant>

#include <QGraphicsRectItem>
#include <QGraphicsSvgItem>
#include <QPainter>

#include <ui/common/theme.hpp>


namespace SILICON {
namespace ui {
using namespace SILICON::core;
using namespace SILICON::extra;

namespace {
std::shared_ptr<Multiplexer> makeMultiplexer()
{
  return std::make_shared<Multiplexer>(Bus(2), Bus(1), Wire_ptr{});
}

std::shared_ptr<Demultiplexer> makeDemultiplexer()
{
  return std::make_shared<Demultiplexer>(Bus(1), Bus(1), Bus(2));
}

std::shared_ptr<Decoder> makeDecoder()
{
  return std::make_shared<Decoder>(Bus(1), Bus(1), Bus(2));
}

constexpr unsigned int dataInputIndex =
    static_cast<unsigned int>(Multiplexer::Inputs::Data);
constexpr unsigned int selectionInputIndex =
    static_cast<unsigned int>(Multiplexer::Inputs::Selection);

int getMuxWidth(const int numberOfInputs)
{
  return std::max(150, 40 + (numberOfInputs - 1) * 40);
}

class LabeledMuxRectItem : public QGraphicsRectItem {
public:
  explicit LabeledMuxRectItem(const QRectF& rect, QString label)
    : QGraphicsRectItem(rect), label(std::move(label))
  {
  }

  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
             QWidget* widget) override
  {
    Q_UNUSED(option);
    Q_UNUSED(widget);

    painter->save();
    painter->setPen(QPen(ThemeEngine::getColor("SILICON_INK"), 3));
    painter->setBrush(ThemeEngine::getColor("SILICON_INTERNAL"));
    painter->drawRect(rect());

    painter->setPen(QPen(ThemeEngine::getColor("SILICON_INK")));
    painter->setFont(QFont("NovaMono", 9));
    painter->drawText(rect().adjusted(6, 6, -6, -6), label,
                      QTextOption(Qt::AlignCenter));
    painter->restore();
  }

private:
  QString label;
};

QGraphicsItem* busShape(const int busSize, const int selectionSize, const bool demux)
{
  const int dataBusCount = 1 << selectionSize;
  const int width = getMuxWidth(dataBusCount);
  constexpr int height   = 90;
  const QString label =
      demux ? QString("%1 demux 1 to %2").arg(busSize).arg(dataBusCount)
            : QString("%1 mux %2 to 1").arg(busSize).arg(dataBusCount);
  return new LabeledMuxRectItem(QRectF(0, 0, width, height), label);
}

std::vector<PortPair> northBusPorts(const int count, const QString& prefix,
                                    const int width)
{
  std::vector<PortPair> ports;
  ports.reserve(static_cast<std::size_t>(count));

  const int startX = (width - (count - 1) * 40) / 2;
  for (int index = 0; index < count; ++index)
    ports.emplace_back(PortPair{QString("%1[%2]").arg(prefix).arg(index),
                                DiagramScene::snapToGrid(QPoint(startX + index * 40, -20))});

  return ports;
}
}  // namespace

// --- Graphical Multiplexer -------------------------------------------------------------

GraphicalMultiplexer::GraphicalMultiplexer(QGraphicsItem* parent)
  : GraphicalLogicComponent(makeMultiplexer(),
                            new QGraphicsSvgItem(":/other_components/MUX.svg"), parent,
                            true)
{
  printPortNames = true;
  setupCallbacks();
  updateLayout(1, 1);
}

Multiplexer* GraphicalMultiplexer::getComponentAsMultiplexer() const
{
  auto* multiplexer = dynamic_cast<Multiplexer*>(associatedComponent.get());
  if (!multiplexer)
    throw std::logic_error("GraphicalMultiplexer requires a Multiplexer component");
  return multiplexer;
}

void GraphicalMultiplexer::setupCallbacks()
{
  if (!associatedComponent)
    return;

  associatedComponent->setPropertyCallback("selectionSize", [this](const PropertyValue& value) {
    return applySelectionSize(std::get<int>(value));
  });

  associatedComponent->setPropertyCallback("busSize", [this](const PropertyValue& value) {
    return applyBusSize(std::get<int>(value));
  });
}

void GraphicalMultiplexer::updateLayout(const int selectionSize, const int busSize)
{
  const int dataBusCount = 1 << selectionSize;

  if (busSize > 1) {
    setItemShape(busShape(busSize, selectionSize, false));
    const int width = getMuxWidth(dataBusCount);

    auto inputs = northBusPorts(dataBusCount, "d", width);
    inputs.emplace_back("sel", QPoint(-20, 50));
    setPorts(inputs, {PortPair{"out", DiagramScene::snapToGrid(QPoint(width / 2, 110))}});
    return;
  }

  setItemShape(new QGraphicsSvgItem(":/other_components/MUX.svg"));
  if (selectionSize == 1) {
    setPorts({PortPair{"d[0]", QPoint(-20, 30)}, PortPair{"d[1]", QPoint(-20, 70)},
              PortPair{"sel", QPoint(60, -10)}},
             {PortPair{"out", QPoint(120, 50)}});
  } else {
    setPorts({PortPair{"d", QPoint(-20, 50)}, PortPair{"sel", QPoint(60, -10)}},
             {PortPair{"o", QPoint(120, 50)}});
  }
}

int GraphicalMultiplexer::applySelectionSize(const int selectionSize)
{
  const int appliedSelectionSize = getComponentAsMultiplexer()->setSelectionSize(selectionSize);
  const int appliedBusSize = associatedComponent->getPropertyValue<int>("busSize").value_or(1);
  updateLayout(appliedSelectionSize, appliedBusSize);
  return appliedSelectionSize;
}

int GraphicalMultiplexer::applyBusSize(const int busSize)
{
  const int appliedBusSize = getComponentAsMultiplexer()->setBusSize(busSize);
  const int appliedSelectionSize = associatedComponent->getPropertyValue<int>("selectionSize").value_or(1);
  updateLayout(appliedSelectionSize, appliedBusSize);
  return appliedBusSize;
}

bool GraphicalMultiplexer::splitDataInputs() const
{
  if (!associatedComponent)
    return false;

  const auto inputs  = associatedComponent->getInputs();
  const auto outputs = associatedComponent->getOutputs();

  return inputs.size() == 2 && outputs.size() == 1 && inputs[dataInputIndex].size() == 2
         && inputs[selectionInputIndex].size() == 1 && outputs[0].size() == 1;
}

bool GraphicalMultiplexer::acceptsInputPortCount(
    const size_t portCount, const std::vector<Bus>& componentInputs) const
{
  if (splitDataInputs())
    return componentInputs.size() == 2 && portCount == 3;

  return GraphicalLogicComponent::acceptsInputPortCount(portCount, componentInputs);
}

unsigned int GraphicalMultiplexer::inputPortSize(
    const size_t portIndex, const std::vector<Bus>& componentInputs) const
{
  if (!splitDataInputs())
    return GraphicalLogicComponent::inputPortSize(portIndex, componentInputs);

  if (portIndex == 0 || portIndex == 1)
    return 1;
  if (portIndex == 2 && componentInputs.size() > 1)
    return static_cast<unsigned int>(componentInputs[1].size());

  return 1;
}

void GraphicalMultiplexer::assignInputPortBus(const unsigned int portIndex,
                                              const Bus&         bus) const
{
  if (!associatedComponent)
    return;

  if (!splitDataInputs()) {
    GraphicalLogicComponent::assignInputPortBus(portIndex, bus);
    return;
  }

  if (portIndex == 2) {
    associatedComponent->setInput(selectionInputIndex, bus, true);
    return;
  }

  if (portIndex > 1)
    throw std::out_of_range("Multiplexer input port index out of range");

  auto inputs = associatedComponent->getInputs();
  if (inputs.size() < 2)
    inputs.resize(2);
  if (inputs[dataInputIndex].size() != 2)
    inputs[dataInputIndex].setSize(2);

  if (bus.size() != 1)
    throw std::runtime_error("2:1 multiplexer data inputs must be single wires");

  inputs[dataInputIndex][portIndex] = bus[0];
  associatedComponent->setInput(dataInputIndex, inputs[dataInputIndex], true);
}

void GraphicalMultiplexer::setComponent(const Component_ptr& component)
{
  GraphicalLogicComponent::setComponent(component);
  if (!component)
    return;

  setupCallbacks();
  const int appliedSelSize = getComponentAsMultiplexer()->setSelectionSize(component->getPropertyValue<int>("selectionSize").value_or(1));
  const int appliedBusSize = getComponentAsMultiplexer()->setBusSize(component->getPropertyValue<int>("busSize").value_or(1));
  updateLayout(appliedSelSize, appliedBusSize);
}


// --- Graphical Demultiplexer -----------------------------------------------------------

GraphicalDemultiplexer::GraphicalDemultiplexer(QGraphicsItem* parent)
  : GraphicalLogicComponent(makeDemultiplexer(),
                            new QGraphicsSvgItem(":/other_components/DEMUX.svg"),
                            parent, true)
{
  printPortNames = true;
  setupCallbacks();
  updateLayout(1, 1);
}

Demultiplexer* GraphicalDemultiplexer::getComponentAsDemultiplexer() const
{
  auto* demultiplexer = dynamic_cast<Demultiplexer*>(associatedComponent.get());
  if (!demultiplexer)
    throw std::logic_error("GraphicalDemultiplexer requires a Demultiplexer component");
  return demultiplexer;
}

void GraphicalDemultiplexer::setupCallbacks()
{
  if (!associatedComponent)
    return;

  associatedComponent->setPropertyCallback("selectionSize", [this](const PropertyValue& value) {
    return applySelectionSize(std::get<int>(value));
  });

  associatedComponent->setPropertyCallback("busSize", [this](const PropertyValue& value) {
    return applyBusSize(std::get<int>(value));
  });
}

void GraphicalDemultiplexer::updateLayout(const int selectionSize, const int busSize)
{
  const int outputBusCount = 1 << selectionSize;

  if (busSize > 1) {
    setItemShape(busShape(busSize, selectionSize, true));
    const int width = getMuxWidth(outputBusCount);

    setPorts({PortPair{"data", QPoint(width / 2, 110)}, PortPair{"sel", QPoint(-20, 45)}},
             northBusPorts(outputBusCount, "o", width));
    return;
  }

  setItemShape(new QGraphicsSvgItem(":/other_components/DEMUX.svg"));
  setPorts({PortPair{"d", QPoint(-20, 50)}, PortPair{"sel", QPoint(60, -10)}},
           {PortPair{"out", QPoint(120, 50)}});
}

int GraphicalDemultiplexer::applySelectionSize(const int selectionSize)
{
  const int appliedSelectionSize = getComponentAsDemultiplexer()->setSelectionSize(selectionSize);
  const int appliedBusSize = associatedComponent->getPropertyValue<int>("busSize").value_or(1);
  updateLayout(appliedSelectionSize, appliedBusSize);
  return appliedSelectionSize;
}

int GraphicalDemultiplexer::applyBusSize(const int busSize)
{
  const int appliedBusSize = getComponentAsDemultiplexer()->setBusSize(busSize);
  const int appliedSelectionSize = associatedComponent->getPropertyValue<int>("selectionSize").value_or(1);
  updateLayout(appliedSelectionSize, appliedBusSize);
  return appliedBusSize;
}

void GraphicalDemultiplexer::setComponent(const Component_ptr& component)
{
  GraphicalLogicComponent::setComponent(component);
  if (!component)
    return;

  setupCallbacks();
  const int appliedSelSize = getComponentAsDemultiplexer()->setSelectionSize(component->getPropertyValue<int>("selectionSize").value_or(1));
  const int appliedBusSize = getComponentAsDemultiplexer()->setBusSize(component->getPropertyValue<int>("busSize").value_or(1));
  updateLayout(appliedSelSize, appliedBusSize);
}

// --- Graphical Decoder -----------------------------------------------------------------

GraphicalDecoder::GraphicalDecoder(QGraphicsItem* parent)
  : GraphicalLogicComponent(makeDecoder(),
                            new QGraphicsSvgItem(":/other_components/DECODER.svg"),
                            parent, true)
{
  printPortNames = true;
  setupCallbacks();
  applySelectionSize(1);
}

Decoder* GraphicalDecoder::getComponentAsDecoder() const
{
  auto* decoder = dynamic_cast<Decoder*>(associatedComponent.get());
  if (!decoder)
    throw std::logic_error("GraphicalDecoder requires a Decoder component");
  return decoder;
}

void GraphicalDecoder::setupCallbacks()
{
  if (!associatedComponent)
    return;

  associatedComponent->setPropertyCallback("selectionSize", [this](const PropertyValue& value) {
    return applySelectionSize(std::get<int>(value));
  });
}

int GraphicalDecoder::applySelectionSize(const int selectionSize)
{
  const int appliedSelectionSize = getComponentAsDecoder()->setSelectionSize(selectionSize);
  
  setPorts({PortPair{"en", QPoint(40, -10)}, PortPair{"sel", QPoint(60, -10)}},
           {PortPair{"out", QPoint(120, 50)}});

  return appliedSelectionSize;
}

void GraphicalDecoder::setComponent(const Component_ptr& component)
{
  GraphicalLogicComponent::setComponent(component);
  if (!component)
    return;

  setupCallbacks();
  applySelectionSize(component->getPropertyValue<int>("selectionSize").value_or(1));
}

}  // namespace ui
}  // namespace SILICON
