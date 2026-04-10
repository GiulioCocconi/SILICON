#include "graphicalUtils.hpp"

#include <stdexcept>

GraphicalWireSplitter::GraphicalWireSplitter(QGraphicsItem* parent)
  : GraphicalLogicComponent(std::make_shared<WireSplitter>(Bus(), (std::vector<Bus>){}),
                            nullptr, parent),
    size(2)
{
  this->associatedComponent->setPropertyCallback(
      "size", [this](const PropertyValue& value) {
        int newSize = std::get<int>(value);
        if (newSize <= 1)
          throw std::invalid_argument("WireSplitter size must be greater than 1");
        this->setSize(newSize);
        return value;
      });
  this->associatedComponent->setProperty("size", 2);
};

void GraphicalWireSplitter::setSize(const int newSize)
{
  if (newSize <= 1)
    throw std::invalid_argument("WireSplitter size must be greater than 1");
  this->size = newSize;

  this->associatedComponent->setInput(0, Bus(newSize));
  std::vector<Bus> outputs(newSize, Bus(1));
  this->associatedComponent->setOutputs(outputs);

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
  shape->setPen(QPen(Qt::black, 3));

  this->setItemShape(shape);
  this->setPorts({std::pair<std::string, QPoint>{"b", QPoint(-20, 0)}}, outputPorts);
}

GraphicalWireMerger::GraphicalWireMerger(QGraphicsItem* parent)
  : GraphicalLogicComponent(std::make_shared<WireMerger>((std::vector<Bus>){}, Bus()),
                            nullptr, parent),
    size(2)
{
  this->associatedComponent->setPropertyCallback(
      "size", [this](const PropertyValue& value) {
        int newSize = std::get<int>(value);
        if (newSize <= 1)
          throw std::invalid_argument("WireMerger size must be greater than 1");
        this->setSize(newSize);
        return value;
      });
  this->associatedComponent->setProperty("size", 2);
};

void GraphicalWireMerger::setSize(const int newSize)
{
  if (newSize <= 1)
    throw std::invalid_argument("WireMerger size must be greater than 1");
  this->size = newSize;

  std::vector<Bus> inputs(newSize, Bus());
  this->associatedComponent->setInputs(inputs);
  this->associatedComponent->setOutput(0, Bus(newSize));

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
  shape->setPen(QPen(Qt::black, 3));

  this->setItemShape(shape);
  this->setPorts(inputPorts, {std::pair<std::string, QPoint>{"b", QPoint(20, 0)}});
}