#include <ui/circuit/components/graphicalUtils.hpp>

#include <ui/shell/theme.hpp>


namespace SILICON::ui {
namespace {
  class ThemedPathItem : public QGraphicsPathItem {
  public:
    using QGraphicsPathItem::QGraphicsPathItem;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override
    {
      auto themedPen = pen();
      themedPen.setColor(ThemeEngine::getColor(theme::ColorToken::Ink));
      setPen(themedPen);
      QGraphicsPathItem::paint(painter, option, widget);
    }
  };
}  // namespace
using namespace SILICON::core;
using namespace SILICON::extra;

GraphicalWireSplitter::GraphicalWireSplitter(QGraphicsItem* parent)
  : GraphicalLogicComponent(std::make_shared<WireSplitter>(Bus(), std::vector<Bus>{}),
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
  this->associatedComponent->setPropertyCallback(
      "slices", [this](const PropertyValue& value) {
        const auto logical = dynamic_cast<WireSplitter*>(associatedComponent.get());
        const auto result  = logical->setSlices(std::get<std::string>(value));
        setSize(logical->getPropertyValue<int>("size").value_or(2));
        return result;
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
  if (newSize < 1)
    return static_cast<int>(this->size);

  const auto logicalWireSplitter = dynamic_cast<WireSplitter*>(associatedComponent.get());
  logicalWireSplitter->setSize(newSize);
  this->size = newSize;

  QPainterPath path{};
  path.moveTo(0, 0);

  std::vector<PortPair> outputPorts;
  const auto            slices = logicalWireSplitter->outputSlices();
  outputPorts.reserve(slices.size());

  for (std::size_t i = 0; i < slices.size(); ++i) {
    QPoint portLoc(10, -20 * static_cast<int>(i) - 10);
    path.lineTo(portLoc);

    const auto [offset, width] = slices[i];
    const auto label           = width == 1
                                     ? QString("b[%1]").arg(offset)
                                     : QString("b[%1:%2]").arg(offset + width - 1).arg(offset);
    outputPorts.emplace_back(label, portLoc + QPoint(20, 0));
  }

  auto shape = new ThemedPathItem(path, this);
  shape->setPen(QPen(ThemeEngine::getColor(theme::ColorToken::Ink), 3));

  this->setItemShape(shape);
  this->setPorts({PortPair{"b", QPoint(-20, 0)}}, outputPorts);

  return newSize;
}

GraphicalWireMerger::GraphicalWireMerger(QGraphicsItem* parent)
  : GraphicalLogicComponent(std::make_shared<WireMerger>(std::vector<Bus>{}, Bus()),
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
  this->associatedComponent->setPropertyCallback(
      "widths", [this](const PropertyValue& value) {
        const auto logical = dynamic_cast<WireMerger*>(associatedComponent.get());
        const auto result  = logical->setWidths(std::get<std::string>(value));
        setSize(logical->getPropertyValue<int>("size").value_or(2));
        return result;
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
  if (newSize < 1)
    return this->size;
  const auto logicalWireMerger = dynamic_cast<WireMerger*>(associatedComponent.get());
  logicalWireMerger->setSize(newSize);
  this->size = newSize;

  QPainterPath path{};
  path.moveTo(0, 0);

  std::vector<PortPair> inputPorts;
  const auto&           inputs = logicalWireMerger->inputBuses();
  inputPorts.reserve(inputs.size());

  unsigned int offset = 0;
  for (std::size_t i = 0; i < inputs.size(); ++i) {
    QPoint portLoc(-10, -20 * static_cast<int>(i) - 10);
    path.lineTo(portLoc);

    const auto width = inputs[i].size();
    const auto label = width == 1
                           ? QString("b[%1]").arg(offset)
                           : QString("b[%1:%2]").arg(offset + width - 1).arg(offset);
    inputPorts.emplace_back(label, portLoc + QPoint(-20, 0));
    offset += width;
  }

  auto shape = new ThemedPathItem(path, this);
  shape->setPen(QPen(ThemeEngine::getColor(theme::ColorToken::Ink), 3));

  this->setItemShape(shape);
  this->setPorts(inputPorts, {PortPair{"b", QPoint(20, 0)}});

  return newSize;
}

}  // namespace SILICON::ui
