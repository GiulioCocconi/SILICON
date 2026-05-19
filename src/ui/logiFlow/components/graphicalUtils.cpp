#include <ui/logiFlow/components/graphicalUtils.hpp>

#include <ui/common/theme.hpp>

GraphicalWireSplitter::GraphicalWireSplitter(QGraphicsItem* parent)
  : GraphicalLogicComponent(std::make_shared<WireSplitter>(Bus(), (std::vector<Bus>){}),
                            nullptr, parent)
{
  this->associatedComponent->setPropertyCallback("size",
                                                 [this](const PropertyValue& value) {
                                                   int newSize = std::get<int>(value);
                                                   return this->setSize(newSize);
                                                 });
};

int GraphicalWireSplitter::setSize(const int newSize)
{
  if (newSize <= 1)
    return static_cast<int>(this->size);

  this->size = newSize;

  const auto logicalWireSplitter = dynamic_cast<WireSplitter*>(associatedComponent.get());
  logicalWireSplitter->setSize(newSize);

  QPainterPath path{};
  path.moveTo(0, 0);

  std::vector<std::pair<std::string, QPoint>> outputPorts;
  outputPorts.reserve(newSize);

  for (int i = 1; i <= newSize; i++) {
    QPoint portLoc(10, -20 * i + 10);
    path.lineTo(portLoc);

    outputPorts.emplace_back(std::format("b[{}]", i - 1), portLoc + QPoint(20, 0));
  }

  auto shape = new QGraphicsPathItem(path, this);
  shape->setPen(QPen(ThemeEngine::getColor("SILICON_INK"), 3));

  this->setItemShape(shape);
  this->setPorts({std::pair<std::string, QPoint>{"b", QPoint(-20, 0)}}, outputPorts);

  return newSize;
}

GraphicalWireMerger::GraphicalWireMerger(QGraphicsItem* parent)
  : GraphicalLogicComponent(std::make_shared<WireMerger>((std::vector<Bus>){}, Bus()),
                            nullptr, parent)
{
  this->associatedComponent->setPropertyCallback(
      "size", [this](const PropertyValue& value) {
        const int newSize = std::get<int>(value);
        return this->setSize(newSize);
      });
};

int GraphicalWireMerger::setSize(const int newSize)
{
  if (newSize <= 1)
    return this->size;
  this->size = newSize;

  const auto logicalWireMerger = dynamic_cast<WireMerger*>(associatedComponent.get());
  logicalWireMerger->setSize(newSize);

  QPainterPath path{};
  path.moveTo(0, 0);

  std::vector<std::pair<std::string, QPoint>> inputPorts;
  inputPorts.reserve(newSize);

  for (int i = 1; i <= newSize; i++) {
    QPoint portLoc(-10, -20 * i + 10);
    path.lineTo(portLoc);

    inputPorts.emplace_back(std::format("b[{}]", i - 1), portLoc + QPoint(-20, 0));
  }

  auto shape = new QGraphicsPathItem(path, this);
  shape->setPen(QPen(ThemeEngine::getColor("SILICON_INK"), 3));

  this->setItemShape(shape);
  this->setPorts(inputPorts, {std::pair<std::string, QPoint>{"b", QPoint(20, 0)}});

  return newSize;
}
