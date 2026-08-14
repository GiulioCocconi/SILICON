#include <ui/logiFlow/components/graphicalUtils.hpp>

#include <ui/common/theme.hpp>


namespace SILICON {
namespace ui {
using namespace SILICON::core;
using namespace SILICON::extra;

GraphicalWireSplitter::GraphicalWireSplitter(QGraphicsItem* parent)
  : GraphicalLogicComponent(std::make_shared<WireSplitter>(Bus(), (std::vector<Bus>){}),
                            nullptr, parent)
{
  setSize(associatedComponent->getPropertyValue<int>("size").value_or(2));
  installSizeCallback();
};

void GraphicalWireSplitter::installSizeCallback()
{
  this->associatedComponent->setPropertyCallback("size",
                                                 [this](const PropertyValue& value) {
                                                   int newSize = std::get<int>(value);
                                                   return this->setSize(newSize);
                                                 });
}

void GraphicalWireSplitter::setComponent(const Component_ptr& component)
{
  GraphicalLogicComponent::setComponent(component);
  setSize(component ? component->getPropertyValue<int>("size").value_or(2) : 2);
  installSizeCallback();
}

int GraphicalWireSplitter::setSize(const int newSize)
{
  if (newSize <= 1)
    return static_cast<int>(this->size);

  this->size = newSize;

  const auto logicalWireSplitter = dynamic_cast<WireSplitter*>(associatedComponent.get());
  logicalWireSplitter->setSize(newSize);

  QPainterPath path{};
  path.moveTo(0, 0);

  std::vector<PortPair> outputPorts;
  outputPorts.reserve(newSize);

  for (int i = 1; i <= newSize; i++) {
    QPoint portLoc(10, -20 * i + 10);
    path.lineTo(portLoc);

    outputPorts.emplace_back(QString("b[%1]").arg(i - 1), portLoc + QPoint(20, 0));
  }

  auto shape = new QGraphicsPathItem(path, this);
  shape->setPen(QPen(ThemeEngine::getColor("SILICON_INK"), 3));

  this->setItemShape(shape);
  this->setPorts({PortPair{"b", QPoint(-20, 0)}}, outputPorts);

  return newSize;
}

GraphicalWireMerger::GraphicalWireMerger(QGraphicsItem* parent)
  : GraphicalLogicComponent(std::make_shared<WireMerger>((std::vector<Bus>){}, Bus()),
                            nullptr, parent)
{
  setSize(associatedComponent->getPropertyValue<int>("size").value_or(2));
  installSizeCallback();
};

void GraphicalWireMerger::installSizeCallback()
{
  this->associatedComponent->setPropertyCallback(
      "size", [this](const PropertyValue& value) {
        const int newSize = std::get<int>(value);
        return this->setSize(newSize);
      });
}

void GraphicalWireMerger::setComponent(const Component_ptr& component)
{
  GraphicalLogicComponent::setComponent(component);
  setSize(component ? component->getPropertyValue<int>("size").value_or(2) : 2);
  installSizeCallback();
}

int GraphicalWireMerger::setSize(const int newSize)
{
  if (newSize <= 1)
    return this->size;
  this->size = newSize;

  const auto logicalWireMerger = dynamic_cast<WireMerger*>(associatedComponent.get());
  logicalWireMerger->setSize(newSize);

  QPainterPath path{};
  path.moveTo(0, 0);

  std::vector<PortPair> inputPorts;
  inputPorts.reserve(newSize);

  for (int i = 1; i <= newSize; i++) {
    QPoint portLoc(-10, -20 * i + 10);
    path.lineTo(portLoc);

    inputPorts.emplace_back(QString("b[%1]").arg(i - 1), portLoc + QPoint(-20, 0));
  }

  auto shape = new QGraphicsPathItem(path, this);
  shape->setPen(QPen(ThemeEngine::getColor("SILICON_INK"), 3));

  this->setItemShape(shape);
  this->setPorts(inputPorts, {PortPair{"b", QPoint(20, 0)}});

  return newSize;
}

}  // namespace ui
}  // namespace SILICON
