#include "graphicalUtils.hpp"

#include <stdexcept>

GraphicalWireSplitter::GraphicalWireSplitter(const unsigned size, QGraphicsItem* parent)
  : GraphicalLogicComponent(std::make_shared<WireSplitter>(Bus(), (std::vector<Bus>){}),
                            nullptr, parent)
{
  setSize(size);
};

void GraphicalWireSplitter::setSize(const unsigned int size)
{
  if (size <= 1)
    throw std::invalid_argument("WireSplitter size must be greater than 1");
  this->size = size;

  this->associatedComponent->setInput(0, Bus(size));
  this->associatedComponent->setOutputs(std::vector(size, Bus(1)));

  QPainterPath path{};
  path.moveTo(0, 0);

  std::vector<std::pair<std::string, QPoint>> outputPorts;
  outputPorts.reserve(size);

  for (unsigned int i = 1; i <= size; i++) {
    QPoint portLoc(10, -20 * i + 10);
    path.lineTo(portLoc);

    outputPorts.emplace_back(std::format("b[{}]", i - 1), portLoc + QPoint(20, 0));
  }

  auto shape = new QGraphicsPathItem(path, this);
  shape->setPen(QPen(Qt::black, 3));

  this->setItemShape(shape);
  this->setPorts({std::pair<std::string, QPoint>{"b", QPoint(-20, 0)}}, outputPorts);
}

GraphicalWireMerger::GraphicalWireMerger(const unsigned size, QGraphicsItem* parent)
  : GraphicalLogicComponent(std::make_shared<WireMerger>((std::vector<Bus>){}, Bus()),
                            nullptr, parent)
{
  setSize(size);
};

void GraphicalWireMerger::setSize(const unsigned int size)
{
  if (size <= 1)
    throw std::invalid_argument("WireMerger size must be greater than 1");
  this->size = size;

  this->associatedComponent->setInputs(std::vector(size, Bus()));
  this->associatedComponent->setOutput(0, Bus(size));

  QPainterPath path{};
  path.moveTo(0, 0);

  std::vector<std::pair<std::string, QPoint>> inputPorts;
  inputPorts.reserve(size);

  for (unsigned int i = 1; i <= size; i++) {
    QPoint portLoc(-10, -20 * i + 10);
    path.lineTo(portLoc);

    inputPorts.emplace_back(std::format("b[{}]", i - 1), portLoc + QPoint(-20, 0));
  }

  auto shape = new QGraphicsPathItem(path, this);
  shape->setPen(QPen(Qt::black, 3));

  this->setItemShape(shape);
  this->setPorts(inputPorts, {std::pair<std::string, QPoint>{"b", QPoint(20, 0)}});
}
