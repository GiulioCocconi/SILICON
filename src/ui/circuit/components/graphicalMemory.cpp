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

  constexpr int RamWidth        = 160;
  constexpr int RamHeaderHeight = 30;
  constexpr int RamWriteHeight  = 110;
  constexpr int RamReadHeight   = 60;
  constexpr int RamSectionInset = 10;
  constexpr int RamPortOffset   = 30;

  int readPortY(const std::size_t port)
  {
    return RamHeaderHeight + RamWriteHeight + static_cast<int>(port) * RamReadHeight
           + RamPortOffset;
  }

  class RamShape final : public QGraphicsRectItem {
  public:
    explicit RamShape(std::shared_ptr<RAM> ram)
      : QGraphicsRectItem(0, 0, RamWidth,
                          RamHeaderHeight + RamWriteHeight
                              + static_cast<int>(ram->readPortCount()) * RamReadHeight),
        ram(std::move(ram))
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
      painter->drawText(QRectF(0, 4, RamWidth, 22), QStringLiteral("RAM"),
                        QTextOption(Qt::AlignCenter));
      const auto drawSection = [&](int top, int height, const QString& title) {
        const QRectF bounds(RamSectionInset, top, RamWidth - 2 * RamSectionInset,
                            height - RamSectionInset);
        painter->setPen(QPen(ink, 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(bounds);
        painter->setPen(ink);
        painter->setFont(QFont("Quicksand", 9, QFont::Bold));
        painter->drawText(QRectF(bounds.left(), top + 4, bounds.width(), 20), title,
                          QTextOption(Qt::AlignCenter));
      };
      drawSection(RamHeaderHeight, RamWriteHeight, QStringLiteral("WRITE 0"));
      for (std::size_t port = 0; port < ram->readPortCount(); ++port)
        drawSection(readPortY(port) - RamPortOffset, RamReadHeight,
                    QStringLiteral("READ %1").arg(port));
      painter->restore();
    }

  private:
    std::shared_ptr<RAM> ram;
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

void GraphicalRAM::applyProperties(const PropertyMap& values)
{
  const auto ram    = std::dynamic_pointer_cast<RAM>(associatedComponent);
  const auto before = ram->readPortCount();
  GraphicalMemory::applyProperties(values);
  if (before != ram->readPortCount())
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
  setItemShape(new RamShape(ram));
  // Preserve the core input indices while grouping the visible ports by operation.
  std::vector<PortPair> inputs{
      {"ADDR", QPoint(-20, readPortY(0))},
      {"ADDR", QPoint(-20, RamHeaderHeight + RamPortOffset)},
      {"DATA", QPoint(-20, RamHeaderHeight + RamPortOffset + 20)},
      {"WE", QPoint(-20, RamHeaderHeight + RamPortOffset + 40)},
      {"clk", QPoint(-20, RamHeaderHeight + RamPortOffset + 60)}};
  std::vector<PortPair> outputs;
  for (std::size_t port = 0; port < count; ++port) {
    if (port != 0)
      inputs.push_back({"ADDR", QPoint(-20, readPortY(port))});
    outputs.push_back({"DATA", QPoint(RamWidth + 20, readPortY(port))});
  }
  setPorts(inputs, outputs);
  for (auto* port : getPorts())
    port->setLabelInset(2 * RamSectionInset);
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
  emit propertiesChanged();
}

void GraphicalMemory::applyProperties(const PropertyMap& values)
{
  prepareGeometryChange();
  std::dynamic_pointer_cast<Memory>(associatedComponent)->setProperties(values);
  if (documents)
    refreshBinaryContents();  // Resolve current bytes; undo never carries document data.
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
