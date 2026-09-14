/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#include "graphicalMemory.hpp"

#include <memory>
#include <stdexcept>

#include <QGraphicsRectItem>
#include <QPainter>
#include <QTextOption>

#include <core/projectDocument.hpp>
#include <ui/common/theme.hpp>

namespace SILICON::ui {
using namespace SILICON::core;

namespace {

  constexpr QRectF RomBodyRect(0.0, 0.0, 100.0, 80.0);

  class RomShape final : public QGraphicsRectItem {
  public:
    explicit RomShape(QGraphicsItem* parent = nullptr)
      : QGraphicsRectItem(RomBodyRect, parent)
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
      painter->setFont(QFont("Quicksand", 10, QFont::Bold));
      painter->drawText(rect(), QStringLiteral("ROM"), QTextOption(Qt::AlignCenter));
      painter->restore();
    }
  };

  std::shared_ptr<ROM> makeRom()
  {
    return std::make_shared<ROM>(Bus(1), std::make_shared<Wire>(),
                                 std::make_shared<Wire>(), Bus(8));
  }

}  // namespace

GraphicalROM::GraphicalROM(QGraphicsItem* parent)
  : GraphicalLogicComponent(makeRom(), new RomShape(), parent)
{
  isEditable     = true;
  printPortNames = true;
  setPorts({PortPair{"addr", QPoint(-20, 20)}, PortPair{"CS", QPoint(-20, 40)},
            PortPair{"OE", QPoint(-20, 60)}},
           {PortPair{"d", QPoint(120, 40)}});
}

GraphicalROM::~GraphicalROM()
{
  unsubscribeFromDocuments();
}

void GraphicalROM::setComponent(const Component_ptr& component)
{
  GraphicalLogicComponent::setComponent(component);
  const auto rom = std::dynamic_pointer_cast<ROM>(component);
  if (!rom)
    throw std::logic_error("GraphicalROM requires a ROM component");

  if (documents)
    refreshBinaryContents();
}

void GraphicalROM::applyProperty(const std::string_view key, const PropertyValue& value)
{
  if (key != "binaryContents") {
    GraphicalLogicComponent::applyProperty(key, value);
    return;
  }

  const auto rom = std::dynamic_pointer_cast<ROM>(associatedComponent);
  if (!rom)
    return;

  const auto& slug = std::get<std::string>(value);
  prepareGeometryChange();
  rom->setBinaryDocument(slug, resolveBinaryContents(slug));
  update();
}

void GraphicalROM::setDocumentStore(const SILICON::project::DocumentStore* newDocuments)
{
  if (documents == newDocuments)
    return;
  unsubscribeFromDocuments();
  documents = newDocuments;
  subscribeToDocuments();
  refreshBinaryContents();
}

std::string GraphicalROM::currentSlug() const
{
  if (!associatedComponent)
    return {};
  return associatedComponent->getPropertyValue<std::string>("binaryContents")
      .value_or(std::string());
}

void GraphicalROM::subscribeToDocuments()
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

void GraphicalROM::unsubscribeFromDocuments()
{
  if (documents && documentListenerId != 0)
    documents->removeListener(documentListenerId);
  documentListenerId = 0;
}

void GraphicalROM::refreshBinaryContents()
{
  if (const auto rom = std::dynamic_pointer_cast<ROM>(associatedComponent))
    rom->refreshBinaryContents(resolveBinaryContents(currentSlug()));
}

std::shared_ptr<const std::string>
GraphicalROM::resolveBinaryContents(const std::string_view slug) const
{
  if (!documents || !SILICON::project::isValidDocumentSlug(slug))
    return nullptr;

  const auto path = SILICON::project::documentPathForSlug(
      SILICON::project::DocumentType::RawBinary, slug);
  const auto* document = documents->find(path);
  if (!document)
    return nullptr;

  return std::make_shared<const std::string>(document->getContents());
}

}  // namespace SILICON::ui
