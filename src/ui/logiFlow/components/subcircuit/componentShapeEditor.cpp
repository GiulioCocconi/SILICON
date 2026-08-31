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

#include "componentShapeEditor.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUndoStack>
#include <QVBoxLayout>

#include <core/projectDocument.hpp>
#include <core/subcircuit.hpp>
#include <ui/common/inputDialogUtils.hpp>
#include <ui/common/portGeometry.hpp>
#include <ui/common/theme.hpp>
#include <ui/common/undoCommands.hpp>
#include <ui/logiFlow/components/graphicalLogicComponent.hpp>
#include <ui/logiFlow/components/subcircuit/metadata.hpp>
#include <ui/logiFlow/components/subcircuit/utils.hpp>


namespace SILICON {
namespace ui {
using namespace SILICON::core;

namespace {

constexpr int ExternalPortMarginGrid = 2;

[[nodiscard]] std::vector<Bus>
previewBuses(const std::vector<GraphicalSubcircuitPortMetadata>& ports)
{
  std::vector<Bus> buses;
  buses.reserve(ports.size());
  for (const auto& port : ports)
    buses.emplace_back(static_cast<unsigned short>(std::max(1U, port.busSize)));
  return buses;
}

[[nodiscard]] QPoint sanitizePortPosition(QPoint pos, QRect shapeRect)
{
  pos                           = DiagramScene::snapToGrid(pos);
  const int extension           = gridToPixels(ExternalPortMarginGrid);
  const auto [side, projection] = nearestPortSide(pos, shapeRect);

  // Offset from the projected edge rather than from the requested point. This
  // guarantees a straight, two-grid-unit port line even for interior positions
  // and points dragged beyond a corner.
  switch (side) {
    case PortSide::LEFT: return projection + QPoint(-extension, 0);
    case PortSide::RIGHT: return projection + QPoint(extension, 0);
    case PortSide::UP: return projection + QPoint(0, -extension);
    case PortSide::DOWN: return projection + QPoint(0, extension);
  }
  std::unreachable();
}

[[nodiscard]] QRect shapeRect(const QSize& size)
{
  return {QPoint(0, 0), size};
}

void sanitizePortPositions(GraphicalSubcircuitMetadata& metadata)
{
  const QRect rect = shapeRect(metadata.widthHeight);
  for (auto& port : metadata.inputs)
    port.position = sanitizePortPosition(port.position, rect);
  for (auto& port : metadata.outputs)
    port.position = sanitizePortPosition(port.position, rect);
}

class PreviewSubcircuitComponent : public Component {
public:
  explicit PreviewSubcircuitComponent(const GraphicalSubcircuitMetadata& metadata)
    : Component(previewBuses(metadata.inputs), previewBuses(metadata.outputs))
  {
  }

  std::string_view typeName() const override { return SubcircuitComponent::Type; }
};

class PreviewPortItem : public QGraphicsItem {
public:
  PreviewPortItem(QString label, bool inputPort, QSize shapeSize,
                  QGraphicsItem* parent = nullptr)
    : QGraphicsItem(parent),
      label(std::move(label)),
      inputPort(inputPort),
      m_shapeSize(shapeSize)
  {
    setFlag(QGraphicsItem::ItemIsMovable);
    setFlag(QGraphicsItem::ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    setCursor(Qt::OpenHandCursor);
    setZValue(2);
  }

  void setConstrainPosition(std::function<QPointF(const QPointF&)> callback)
  {
    constrainPosition = std::move(callback);
  }

  void setPositionChanged(std::function<void(const QPointF&)> callback)
  {
    positionChanged = std::move(callback);
  }

  QRectF boundingRect() const override
  {
    return markerRect().united(labelRect()).adjusted(-4, -4, 4, 4);
  }

  void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
             QWidget* widget) override
  {
    Q_UNUSED(option);
    Q_UNUSED(widget);

    const QColor portColor = inputPort ? ThemeEngine::getColor("SILICON_BLUE")
                                       : ThemeEngine::getColor("SILICON_ORANGE");
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(ThemeEngine::getColor("SILICON_INK"), hovered ? 3 : 2));
    painter->setBrush(hovered ? portColor.lighter(125) : portColor);
    painter->drawEllipse(markerRect());

    painter->setPen(ThemeEngine::getColor("SILICON_INK"));
    painter->setFont(QFont("NovaMono", 9));
    painter->drawText(labelRect(), Qt::AlignCenter, label);
  }

protected:
  QVariant itemChange(GraphicsItemChange change, const QVariant& value) override
  {
    if (change == QGraphicsItem::ItemPositionChange && constrainPosition)
      return constrainPosition(value.toPointF());
    if (change == QGraphicsItem::ItemPositionHasChanged && positionChanged)
      positionChanged(pos());
    return QGraphicsItem::itemChange(change, value);
  }

  void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override
  {
    hovered = true;
    update();
    QGraphicsItem::hoverEnterEvent(event);
  }

  void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override
  {
    hovered = false;
    update();
    QGraphicsItem::hoverLeaveEvent(event);
  }

  void mousePressEvent(QGraphicsSceneMouseEvent* event) override
  {
    setCursor(Qt::ClosedHandCursor);
    QGraphicsItem::mousePressEvent(event);
  }

  void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override
  {
    setCursor(Qt::OpenHandCursor);
    QGraphicsItem::mouseReleaseEvent(event);
  }

private:
  [[nodiscard]] PortSide currentSide() const
  {
    return nearestPortSide(DiagramScene::snapToGrid(pos()), shapeRect(m_shapeSize)).first;
  }

  [[nodiscard]] QRectF markerRect() const
  {
    constexpr qreal radius = 6.0;
    return {-radius, -radius, radius * 2.0, radius * 2.0};
  }

  [[nodiscard]] QRectF labelRect() const
  {
    const QFontMetricsF metrics(QFont("NovaMono", 9));
    const QRectF        text = metrics.boundingRect(label).adjusted(-6, -3, 6, 3);
    constexpr qreal     gap  = 8.0;

    switch (currentSide()) {
      case PortSide::LEFT:
        return {-gap - text.width(), -text.height() / 2.0, text.width(), text.height()};
      case PortSide::RIGHT:
        return {gap, -text.height() / 2.0, text.width(), text.height()};
      case PortSide::UP:
        return {-text.width() / 2.0, -gap - text.height(), text.width(), text.height()};
      case PortSide::DOWN: return {-text.width() / 2.0, gap, text.width(), text.height()};
    }
    std::unreachable();
  }

  QString                                label;
  bool                                   inputPort = true;
  QSize                                  m_shapeSize;
  bool                                   hovered = false;
  std::function<QPointF(const QPointF&)> constrainPosition;
  std::function<void(const QPointF&)>    positionChanged;
};

class ShapePreviewWidget : public QGraphicsView {
public:
  explicit ShapePreviewWidget(QWidget* parent = nullptr)
    : QGraphicsView(parent), scene(new QGraphicsScene(this))
  {
    setScene(scene);
    setMinimumSize(460, 320);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::NoDrag);
    setFrameShape(QFrame::StyledPanel);
  }

  void setPortMovedCallback(
      std::function<void(bool inputPort, std::size_t index, QPoint position)> callback)
  {
    portMovedCallback = std::move(callback);
  }

  void setMetadata(const GraphicalSubcircuitMetadata& metadata)
  {
    shapeSize = metadata.widthHeight;
    rebuild(metadata);
  }

  void setPortPosition(bool inputPort, std::size_t index, const QPoint& position)
  {
    auto& visuals = inputPort ? inputVisuals : outputVisuals;
    if (index >= visuals.size())
      return;

    suppressCallbacks = true;
    visuals[index].item->setPos(sanitizePortPosition(position, shapeRect(shapeSize)));
    updateComponentPorts();
    suppressCallbacks = false;
  }

protected:
  void resizeEvent(QResizeEvent* event) override
  {
    QGraphicsView::resizeEvent(event);
    fitPreview();
  }

private:
  struct PortVisual {
    PreviewPortItem* item = nullptr;
  };

  void rebuild(const GraphicalSubcircuitMetadata& metadata)
  {
    scene->clear();
    component = nullptr;
    inputVisuals.clear();
    outputVisuals.clear();

    const int gridMargin = gridToPixels(ExternalPortMarginGrid + 3);
    scene->setSceneRect(-gridMargin, -gridMargin, shapeSize.width() + gridMargin * 2,
                        shapeSize.height() + gridMargin * 2);
    addGrid();
    addComponent(metadata);

    addPorts(metadata.inputs, true);
    addPorts(metadata.outputs, false);
    updateComponentPorts();
    fitPreview();
  }

  void addComponent(const GraphicalSubcircuitMetadata& metadata)
  {
    component = new GraphicalLogicComponent(
        std::make_shared<PreviewSubcircuitComponent>(metadata),
        new SubcircuitRectShape(shapeSize), nullptr, false);
    component->setZValue(1);
    scene->addItem(component);
  }

  void addGrid()
  {
    const QRectF sceneBounds = scene->sceneRect();
    const QPen   gridPen(ThemeEngine::getColor("SILICON_GRID"), 1);
    for (qreal x = sceneBounds.left(); x <= sceneBounds.right();
         x += DiagramScene::GRID_SIZE) {
      scene->addLine(x, sceneBounds.top(), x, sceneBounds.bottom(), gridPen)
          ->setZValue(0);
    }
    for (qreal y = sceneBounds.top(); y <= sceneBounds.bottom();
         y += DiagramScene::GRID_SIZE) {
      scene->addLine(sceneBounds.left(), y, sceneBounds.right(), y, gridPen)
          ->setZValue(0);
    }
  }

  void addPorts(const std::vector<GraphicalSubcircuitPortMetadata>& ports, bool inputPort)
  {
    auto& visuals = inputPort ? inputVisuals : outputVisuals;
    visuals.reserve(ports.size());

    for (std::size_t index = 0; index < ports.size(); ++index) {
      const auto& port  = ports[index];
      QString     label = QString::fromStdString(port.name);

      auto* item = new PreviewPortItem(label, inputPort, shapeSize);
      item->setToolTip(QString::fromStdString(port.name));
      item->setPos(sanitizePortPosition(port.position, shapeRect(shapeSize)));
      item->setConstrainPosition([this](const QPointF& position) {
        return sanitizePortPosition(DiagramScene::snapToGrid(position),
                                    shapeRect(shapeSize));
      });
      item->setPositionChanged([this, inputPort, index](const QPointF& position) {
        auto& visuals = inputPort ? inputVisuals : outputVisuals;
        if (index >= visuals.size())
          return;
        updateComponentPorts();
        if (!suppressCallbacks && portMovedCallback)
          portMovedCallback(inputPort, index, position.toPoint());
      });
      scene->addItem(item);

      visuals.push_back({.item = item});
    }
  }

  std::vector<PortPair> previewPortPairs(const std::vector<PortVisual>& visuals,
                                         bool                           inputPort) const
  {
    std::vector<PortPair> pairs;
    pairs.reserve(visuals.size());
    for (std::size_t index = 0; index < visuals.size(); ++index) {
      const auto* item = visuals[index].item;
      if (!item)
        continue;
      pairs.emplace_back(item->toolTip().isEmpty()
                             ? QString(inputPort ? "input" : "output")
                             : item->toolTip(),
                         item->pos().toPoint());
    }
    return pairs;
  }

  void updateComponentPorts()
  {
    if (!component)
      return;

    component->setPorts(previewPortPairs(inputVisuals, true),
                        previewPortPairs(outputVisuals, false));
  }

  void fitPreview()
  {
    if (!scene || scene->sceneRect().isEmpty())
      return;
    fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
  }

  QGraphicsScene*                                scene     = nullptr;
  GraphicalLogicComponent*                       component = nullptr;
  QSize                                          shapeSize;
  std::vector<PortVisual>                        inputVisuals;
  std::vector<PortVisual>                        outputVisuals;
  bool                                           suppressCallbacks = false;
  std::function<void(bool, std::size_t, QPoint)> portMovedCallback;
};

QTableWidget*
makePortPositionTable(const std::vector<GraphicalSubcircuitPortMetadata>& ports,
                      QWidget*                                            parent)
{
  auto* table = new QTableWidget(parent);
  table->setColumnCount(4);
  table->setHorizontalHeaderLabels(
      {QObject::tr("Name"), QObject::tr("X"), QObject::tr("Y"), QObject::tr("Bus")});
  table->setRowCount(static_cast<int>(ports.size()));

  for (int row = 0; row < static_cast<int>(ports.size()); ++row) {
    const auto& port = ports[static_cast<std::size_t>(row)];
    auto*       name = new QTableWidgetItem(QString::fromStdString(port.name));
    name->setFlags(name->flags() & ~Qt::ItemIsEditable);
    table->setItem(row, 0, name);
    const auto gridPosition = pixelsToGrid(port.position);
    table->setItem(row, 1, new QTableWidgetItem(QString::number(gridPosition.x())));
    table->setItem(row, 2, new QTableWidgetItem(QString::number(gridPosition.y())));
    auto* bus = new QTableWidgetItem(QString::number(port.busSize));
    bus->setFlags(bus->flags() & ~Qt::ItemIsEditable);
    table->setItem(row, 3, bus);
  }
  table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
  return table;
}

void setPortTablePosition(QTableWidget* table, int row, const QPoint& position)
{
  if (row < 0 || row >= table->rowCount())
    return;

  const QSignalBlocker blocker(table);
  Q_UNUSED(blocker);
  const auto gridPosition = pixelsToGrid(position);
  table->item(row, 1)->setText(QString::number(gridPosition.x()));
  table->item(row, 2)->setText(QString::number(gridPosition.y()));
}

void setPortTablePositions(QTableWidget*                                       table,
                           const std::vector<GraphicalSubcircuitPortMetadata>& ports)
{
  const int rowCount = std::min(table->rowCount(), static_cast<int>(ports.size()));
  for (int row = 0; row < rowCount; ++row)
    setPortTablePosition(table, row, ports[static_cast<std::size_t>(row)].position);
}

}  // namespace

void editGraphicalSubcircuitShape(const std::string& slug, QUndoStack* undoStack,
                                  QWidget* parent)
{
  if (slug.empty())
    return;

  const auto path = SILICON::project::documentPathForSlug(
      SILICON::project::DocumentType::Subcircuit, slug);
  const auto* document = SILICON::project::DocumentStore::active().find(path);
  if (!document)
    return;

  auto metadata = parseGraphicalSubcircuitMetadata(document->getContents());
  if (!metadata)
    return;
  metadata   = synchronizeGraphicalSubcircuitMetadata(document->getContents(), *metadata);
  auto draft = std::make_shared<GraphicalSubcircuitMetadata>(*metadata);
  sanitizePortPositions(*draft);

  auto* dialog = new QDialog(parent);
  dialog->setWindowTitle(QObject::tr("Edit shape"));
  dialog->resize(860, 560);
  auto* layout = new QVBoxLayout(dialog);
  auto* body   = new QHBoxLayout();
  layout->addLayout(body);

  auto* preview = new ShapePreviewWidget(dialog);
  preview->setMetadata(*draft);
  body->addWidget(preview, 1);

  auto* controls = new QVBoxLayout();
  body->addLayout(controls);

  auto* form = new QFormLayout();
  controls->addLayout(form);

  auto* width = new QSpinBox(dialog);
  width->setRange(1, 100);
  width->setValue(pixelsToGrid(draft->widthHeight.width()));
  auto* height = new QSpinBox(dialog);
  height->setRange(1, 100);
  height->setValue(pixelsToGrid(draft->widthHeight.height()));
  form->addRow(QObject::tr("Width"), width);
  form->addRow(QObject::tr("Height"), height);

  auto* inputTable  = makePortPositionTable(draft->inputs, dialog);
  auto* outputTable = makePortPositionTable(draft->outputs, dialog);
  controls->addWidget(new QLabel(QObject::tr("Inputs"), dialog));
  controls->addWidget(inputTable);
  controls->addWidget(new QLabel(QObject::tr("Outputs"), dialog));
  controls->addWidget(outputTable);

  auto updatePortTables = [draft, inputTable, outputTable] {
    setPortTablePositions(inputTable, draft->inputs);
    setPortTablePositions(outputTable, draft->outputs);
  };

  auto applySize = [draft, preview, updatePortTables](int widthGrid, int heightGrid) {
    draft->widthHeight = QSize(gridToPixels(widthGrid), gridToPixels(heightGrid));
    sanitizePortPositions(*draft);
    updatePortTables();
    preview->setMetadata(*draft);
  };

  QObject::connect(width, &QSpinBox::valueChanged, dialog,
                   [height, applySize](int value) { applySize(value, height->value()); });
  QObject::connect(height, &QSpinBox::valueChanged, dialog,
                   [width, applySize](int value) { applySize(width->value(), value); });

  auto applyTableEdit = [draft, preview](QTableWidget* table, bool inputPort,
                                         QTableWidgetItem* item) {
    if (!item || (item->column() != 1 && item->column() != 2))
      return;

    auto&     ports = inputPort ? draft->inputs : draft->outputs;
    const int row   = item->row();
    if (row < 0 || row >= static_cast<int>(ports.size()))
      return;

    auto&      port                = ports[static_cast<std::size_t>(row)];
    const auto currentGridPosition = pixelsToGrid(port.position);
    bool       okX                 = false;
    bool       okY                 = false;
    const int  requestedX          = table->item(row, 1)->text().toInt(&okX);
    const int  requestedY          = table->item(row, 2)->text().toInt(&okY);
    port.position                  = sanitizePortPosition(
        gridToPixels(QPoint(okX ? requestedX : currentGridPosition.x(),
                            okY ? requestedY : currentGridPosition.y())),
        shapeRect(draft->widthHeight));
    setPortTablePosition(table, row, port.position);
    preview->setPortPosition(inputPort, static_cast<std::size_t>(row), port.position);
  };

  QObject::connect(inputTable, &QTableWidget::itemChanged, dialog,
                   [inputTable, applyTableEdit](QTableWidgetItem* item) {
                     applyTableEdit(inputTable, true, item);
                   });
  QObject::connect(outputTable, &QTableWidget::itemChanged, dialog,
                   [outputTable, applyTableEdit](QTableWidgetItem* item) {
                     applyTableEdit(outputTable, false, item);
                   });

  preview->setPortMovedCallback([draft, inputTable, outputTable](
                                    bool inputPort, std::size_t index, QPoint position) {
    auto& ports = inputPort ? draft->inputs : draft->outputs;
    if (index >= ports.size())
      return;

    ports[index].position = position;
    setPortTablePosition(inputPort ? inputTable : outputTable, static_cast<int>(index),
                         position);
  });

  auto* buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
  layout->addWidget(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
  QObject::connect(dialog, &QDialog::accepted, dialog, [draft, path, undoStack, parent] {
    try {
      const auto* currentDocument = SILICON::project::DocumentStore::active().find(path);
      if (!currentDocument)
        return;

      const auto oldSceneJson    = currentDocument->getContents();
      auto       json            = nlohmann::json::parse(oldSceneJson);
      json["graphicalComponent"] = graphicalSubcircuitMetadataToJson(*draft);
      const auto newSceneJson    = json.dump(2);
      if (newSceneJson == oldSceneJson)
        return;

      // Shape edits are registry document edits, so route them through the window undo
      // stack.
      if (undoStack) {
        auto apply = [path](const std::string& sceneJson) {
          SILICON::project::DocumentStore::active().upsertDocument(
              preparedSubcircuitDocument(path, sceneJson));
        };
        undoStack->push(new MetadataEditCommand(QObject::tr("Edit Subcircuit Shape"),
                                                oldSceneJson, newSceneJson,
                                                std::move(apply)));
      } else {
        SILICON::project::DocumentStore::active().upsertDocument(
            preparedSubcircuitDocument(path, newSceneJson));
      }
    } catch (const std::exception& e) {
      SILICON::ui::inputDialog::warning(parent, QObject::tr("Edit shape"), e.what());
    }
  });

#ifdef __EMSCRIPTEN__
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->open();
#else
  dialog->exec();
  delete dialog;
#endif
}

}  // namespace ui
}  // namespace SILICON
