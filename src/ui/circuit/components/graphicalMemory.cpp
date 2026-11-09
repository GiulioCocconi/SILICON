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

#include "graphicalMemory.hpp"

#include <memory>
#include <stdexcept>
#include <utility>

#include <QGraphicsRectItem>
#include <QPainter>
#include <QTextOption>

#include <core/projectDocument.hpp>
#include <ui/shell/theme.hpp>

namespace SILICON::ui {
using namespace SILICON::core;

namespace {

  class MemoryShape final : public QGraphicsRectItem {
  public:
    MemoryShape(QString label, const QRectF& bounds)
      : QGraphicsRectItem(bounds), label(std::move(label))
    {
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override
    {
      Q_UNUSED(option);
      Q_UNUSED(widget);

      painter->save();
      painter->setRenderHint(QPainter::Antialiasing, false);
      painter->setPen(QPen(ThemeEngine::getColor(theme::ColorToken::Ink), 3));
      painter->setBrush(ThemeEngine::getColor(theme::ColorToken::Internal));
      painter->drawRect(rect());
      painter->setPen(QPen(ThemeEngine::getColor(theme::ColorToken::Ink)));
      painter->setFont(QFont("Quicksand", 10, QFont::Bold));
      painter->drawText(rect(), label, QTextOption(Qt::AlignCenter));
      painter->restore();
    }

  private:
    QString label;
  };

  std::shared_ptr<ROM> makeRom()
  {
    return std::make_shared<ROM>(Bus(1), std::make_shared<Wire>(),
                                 std::make_shared<Wire>(), Bus(8));
  }

}  // namespace

GraphicalMemory::GraphicalMemory(const std::shared_ptr<Memory>& memory,
                                 QGraphicsItem* shape, QGraphicsItem* parent)
  : GraphicalLogicComponent(memory, shape, parent)
{
  isEditable     = true;
  printPortNames = true;
}

GraphicalROM::GraphicalROM(QGraphicsItem* parent)
  : GraphicalMemory(makeRom(),
                    new MemoryShape(QStringLiteral("ROM"), QRectF(0, 0, 100, 80)), parent)
{
  setPorts({PortPair{"addr", QPoint(-20, 20)}, PortPair{"CS", QPoint(-20, 40)},
            PortPair{"OE", QPoint(-20, 60)}},
           {PortPair{"d", QPoint(120, 40)}});
}

GraphicalRAM::GraphicalRAM(QGraphicsItem* parent)
  : GraphicalMemory(std::make_shared<RAM>(),
                    new MemoryShape(QStringLiteral("RAM"), QRectF(0, 0, 100, 120)),
                    parent)
{
  rebuildPorts();
}

void GraphicalRAM::setComponent(const Component_ptr& component)
{
  GraphicalMemory::setComponent(component);
  rebuildPorts();
}

void GraphicalRAM::applyProperty(const std::string_view key, const PropertyValue& value)
{
  GraphicalMemory::applyProperty(key, value);
  if (key == "readPorts")
    rebuildPorts();
}

void GraphicalRAM::rebuildPorts()
{
  const auto ram = std::dynamic_pointer_cast<RAM>(associatedComponent);
  const auto count = ram->readPortCount();
  const auto height = static_cast<int>((count + 5) * 20);
  setItemShape(new MemoryShape(QStringLiteral("RAM"), QRectF(0, 0, 100, height)));
  std::vector<PortPair> inputs{
      {"raddr", QPoint(-20, 20)}, {"waddr", QPoint(-20, 40)},
      {"d", QPoint(-20, 60)}, {"WE", QPoint(-20, 80)}, {"CLK", QPoint(-20, 100)}};
  std::vector<PortPair> outputs;
  for (std::size_t port = 0; port < count; ++port) {
    if (port != 0)
      inputs.push_back({QString("raddr%1").arg(port),
                        QPoint(-20, static_cast<int>((port + 5) * 20))});
    outputs.push_back({count == 1 ? QStringLiteral("q") : QString("q%1").arg(port),
                       QPoint(120, count == 1 ? 60 : static_cast<int>((port + 1) * 20))});
  }
  setPorts(inputs, outputs);
}

GraphicalMemory::~GraphicalMemory()
{
  unsubscribeFromDocuments();
}

void GraphicalMemory::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event)
{
  const auto slug = currentSlug();
  if (SILICON::project::isValidDocumentSlug(slug)
      && openDocumentOnDoubleClick(event,
                                   SILICON::project::documentPathForSlug(
                                       SILICON::project::DocumentType::RawBinary, slug)))
    return;

  GraphicalLogicComponent::mouseDoubleClickEvent(event);
}

void GraphicalMemory::setComponent(const Component_ptr& component)
{
  if (!component || component->typeName() != associatedComponent->typeName())
    throw std::logic_error("Graphical memory component type mismatch");
  GraphicalLogicComponent::setComponent(component);

  if (documents)
    refreshBinaryContents();
}

void GraphicalMemory::applyProperty(const std::string_view key,
                                    const PropertyValue&   value)
{
  if (key != "binaryContents") {
    GraphicalLogicComponent::applyProperty(key, value);
    return;
  }

  const auto memory = std::dynamic_pointer_cast<Memory>(associatedComponent);
  if (!memory)
    return;

  const auto& slug = std::get<std::string>(value);
  prepareGeometryChange();
  memory->setBinaryDocument(slug, resolveBinaryContents(slug));
  update();
}

void GraphicalMemory::setDocumentStore(
    const SILICON::project::DocumentStore* newDocuments)
{
  if (documents == newDocuments)
    return;
  unsubscribeFromDocuments();
  documents = newDocuments;
  subscribeToDocuments();
  refreshBinaryContents();
}

std::string GraphicalMemory::currentSlug() const
{
  if (!associatedComponent)
    return {};
  return associatedComponent->getPropertyValue<std::string>("binaryContents")
      .value_or(std::string());
}

void GraphicalMemory::subscribeToDocuments()
{
  if (!documents || documentListenerId != 0)
    return;
  documentListenerId =
      documents->addListener([this](const SILICON::project::DocumentChange& change) {
        const auto slug = currentSlug();
        const bool affectsConfiguredDocument =
            SILICON::project::isValidDocumentSlug(slug) && change.path
            && *change.path
                   == SILICON::project::documentPathForSlug(
                       SILICON::project::DocumentType::RawBinary, slug);
        if (change.kind == SILICON::project::DocumentChangeKind::Reset
            || affectsConfiguredDocument)
          refreshBinaryContents();
      });
}

void GraphicalMemory::unsubscribeFromDocuments()
{
  if (documents && documentListenerId != 0)
    documents->removeListener(documentListenerId);
  documentListenerId = 0;
}

void GraphicalMemory::refreshBinaryContents()
{
  if (const auto memory = std::dynamic_pointer_cast<Memory>(associatedComponent))
    memory->refreshBinaryContents(resolveBinaryContents(currentSlug()));
}

std::shared_ptr<const std::vector<std::uint8_t>>
GraphicalMemory::resolveBinaryContents(const std::string_view slug) const
{
  return documents ? SILICON::project::binaryContentsSnapshot(*documents, slug) : nullptr;
}

}  // namespace SILICON::ui
