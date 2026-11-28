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

  constexpr int MemoryWidth        = 160;
  constexpr int MemoryHeaderHeight = 30;
  constexpr int MemoryWriteHeight  = 110;
  constexpr int MemoryReadHeight   = 60;
  constexpr int MemorySectionInset = 10;
  constexpr int MemoryPortOffset   = 30;

  int memoryTop(const Memory& memory)
  {
    return memory.isWritable() ? -MemoryWriteHeight : 0;
  }

  int readPortY(const std::size_t port)
  {
    // Read lanes keep their endpoints in both modes. The optional write section
    // grows above them, so no surviving wire can land on another semantic port.
    return MemoryHeaderHeight + static_cast<int>(port) * MemoryReadHeight
           + MemoryPortOffset;
  }

  class MemoryShape final : public QGraphicsRectItem {
  public:
    explicit MemoryShape(std::shared_ptr<Memory> memory)
      : QGraphicsRectItem(
            0, memoryTop(*memory), MemoryWidth,
            MemoryHeaderHeight + (memory->isWritable() ? MemoryWriteHeight : 0)
                + static_cast<int>(memory->readPortCount()) * MemoryReadHeight),
        memory(std::move(memory))
    {
      setPen(QPen(ThemeEngine::getColor(theme::ColorToken::Ink), 2));
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override
    {
      Q_UNUSED(option);
      Q_UNUSED(widget);

      painter->save();
      painter->setRenderHint(QPainter::Antialiasing);
      const auto& ink = ThemeEngine::getColor(theme::ColorToken::Ink);
      painter->setPen(QPen(ink, 2));
      painter->setBrush(ThemeEngine::getColor(theme::ColorToken::Internal));
      painter->drawRect(rect());
      painter->setPen(ink);
      painter->setFont(QFont("Quicksand", 10, QFont::Bold));
      painter->drawText(
          QRectF(0, memoryTop(*memory) + 4, MemoryWidth, 22),
          QString::fromStdString(memory->getPropertyValue<std::string>("type").value()),
          QTextOption(Qt::AlignCenter));
      const auto drawSection = [&](int top, int height, const QString& title) {
        const QRectF bounds(MemorySectionInset, top, MemoryWidth - 2 * MemorySectionInset,
                            height - MemorySectionInset);
        painter->setPen(QPen(ink, 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(bounds);
        painter->setPen(ink);
        painter->setFont(QFont("Quicksand", 9, QFont::Bold));
        painter->drawText(QRectF(bounds.left(), top + 4, bounds.width(), 20), title,
                          QTextOption(Qt::AlignCenter));
      };
      if (memory->isWritable())
        drawSection(memoryTop(*memory) + MemoryHeaderHeight, MemoryWriteHeight,
                    QStringLiteral("WRITE 0"));
      for (std::size_t port = 0; port < memory->readPortCount(); ++port)
        drawSection(readPortY(port) - MemoryPortOffset, MemoryReadHeight,
                    QStringLiteral("READ %1").arg(port));
      painter->restore();
    }

  private:
    std::shared_ptr<Memory> memory;
  };

}  // namespace

GraphicalMemory::GraphicalMemory(QGraphicsItem* parent)
  : GraphicalLogicComponent(std::make_shared<Memory>(), nullptr, parent)
{
  isEditable     = true;
  printPortNames = true;
  rebuildPorts();
}

PropertyPresentation GraphicalMemory::propertyPresentation(std::string_view key) const
{
  auto result = GraphicalLogicComponent::propertyPresentation(key);
  if (key == "effectiveDepth")
    result.formatValue = [](const PropertyValue& value) {
      return QString("%1 words").arg(std::get<int>(value));
    };
  if (key == "capacity")
    result.formatValue = [](const PropertyValue& value) {
      const auto bits = std::get<int>(value);
      return QString("%1 bits (%2 bytes)").arg(bits).arg((bits + 7) / 8);
    };
  result.advanced = key == "depthOverride"
                    && associatedComponent->getPropertyValue<int>(key).value() == 0;
  return result;
}

void GraphicalMemory::rebuildPorts()
{
  const auto memory = std::dynamic_pointer_cast<Memory>(associatedComponent);
  setItemShape(new MemoryShape(memory));
  std::vector<PortPair> inputs(memory->inputBuses().size());
  std::vector<PortPair> outputs;
  if (memory->isWritable()) {
    const auto writeY = memoryTop(*memory) + MemoryHeaderHeight + MemoryPortOffset;
    inputs[memory->writeInputIndex(Memory::WriteInputs::Address)] = {QString{},
                                                                     QPoint(-20, writeY)};
    inputs[memory->writeInputIndex(Memory::WriteInputs::Data)]    = {
        QString{}, QPoint(-20, writeY + 20)};
    inputs[memory->writeInputIndex(Memory::WriteInputs::Enable)] = {
        QString{}, QPoint(-20, writeY + 40)};
    inputs[memory->writeInputIndex(Memory::WriteInputs::Clock)] = {
        QString{}, QPoint(-20, writeY + 60)};
  }
  for (std::size_t port = 0; port < memory->readPortCount(); ++port) {
    const auto y = readPortY(port);
    inputs[port] = {QString{}, QPoint(-20, y)};
    outputs.push_back({QString{}, QPoint(MemoryWidth + 20, y)});
  }
  setPorts(inputs, outputs);
  for (auto* port : getPorts())
    port->setLabelInset(2 * MemorySectionInset);
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
  rebuildPorts();
}

void GraphicalMemory::applyProperty(const std::string_view key,
                                    const PropertyValue&   value)
{
  if (key != "binaryContents") {
    GraphicalLogicComponent::applyProperty(key, value);
    if (key == "type" || key == "readPorts")
      rebuildPorts();
    return;
  }

  const auto memory = std::dynamic_pointer_cast<Memory>(associatedComponent);
  if (!memory)
    return;

  const auto& slug = std::get<std::string>(value);
  prepareGeometryChange();
  memory->setBinaryDocument(slug, resolveBinaryContents(slug));
  update();
  emit propertiesChanged();
}

void GraphicalMemory::applyProperties(const PropertyMap& values)
{
  prepareGeometryChange();
  std::dynamic_pointer_cast<Memory>(associatedComponent)->setProperties(values);
  if (documents)
    refreshBinaryContents();  // Resolve current bytes; undo never carries document data.
  rebuildPorts();
  update();
  emit propertiesChanged();
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
  if (const auto memory = std::dynamic_pointer_cast<Memory>(associatedComponent)) {
    memory->refreshBinaryContents(resolveBinaryContents(currentSlug()));
    emit propertiesChanged();
  }
}

std::shared_ptr<const std::vector<std::uint8_t>>
GraphicalMemory::resolveBinaryContents(const std::string_view slug) const
{
  return documents ? SILICON::project::binaryContentsSnapshot(*documents, slug) : nullptr;
}

}  // namespace SILICON::ui
