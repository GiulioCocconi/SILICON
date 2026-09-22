#include "sislVisualizer.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>


namespace SILICON::ui {
namespace {

constexpr qreal cell = 27;
constexpr qreal left = 34;
constexpr qreal rowHeight = 55;

class ZoomView final : public QGraphicsView {
public:
  using QGraphicsView::QGraphicsView;

protected:
  void wheelEvent(QWheelEvent* event) override {
    const qreal factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    const qreal next = transform().m11() * factor;
    if (next >= 0.2 && next <= 4.0) scale(factor, factor);
    event->accept();
  }
};

class FormatBox final : public QGraphicsRectItem {
public:
  FormatBox(const QRectF& rect, std::function<void()> open)
    : QGraphicsRectItem(rect), open(std::move(open)) {
    setCursor(Qt::PointingHandCursor);
  }
protected:
  void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override {
    open();
    event->accept();
  }
private:
  std::function<void()> open;
};

void addText(QGraphicsScene* scene, const QString& value, qreal x, qreal y,
             const QColor& color, qreal scale = 1.0) {
  auto* item = scene->addSimpleText(value);
  item->setBrush(color);
  item->setPos(x, y);
  item->setScale(scale);
  item->setAcceptedMouseButtons(Qt::NoButton);
}

QString bits(const sisl::Integer& value, std::size_t width) {
  QString result = QStringLiteral("0b");
  for (std::size_t bit = width; bit > 0; --bit)
    result += ((value >> (bit - 1)) & 1) == 0 ? QLatin1Char('0') : QLatin1Char('1');
  return result;
}

QString sliceLabel(const sisl::FieldDescription& field,
                   const sisl::FieldMapping& mapping) {
  const auto& range = mapping.field_bits;
  if (range.lsb == 0 && range.msb + 1 == field.width)
    return QString::fromStdString(field.name);
  return QString::fromStdString(field.name) + QLatin1Char('[')
         + QString::number(range.msb)
         + (range.msb == range.lsb ? QString() : QLatin1Char(':') + QString::number(range.lsb))
         + QLatin1Char(']');
}

std::size_t effectiveWidth(const sisl::FormatDescription& format) {
  if (format.width) return *format.width;
  std::size_t width = 0;
  for (const auto& field : format.fields)
    for (const auto& mapping : field.mappings)
      width = std::max(width, mapping.instruction_bits.msb + 1);
  return width;
}

} // namespace

SislVisualizer::SislVisualizer(QWidget* parent) : QWidget(parent) {
  auto* layout = new QVBoxLayout(this);
  auto* bar = new QHBoxLayout();
  back = new QPushButton(tr("← Formats"), this);
  bar->addWidget(back);
  bar->addStretch();
  hint = new QLabel(tr("Double-click a format to see its instructions"), this);
  bar->addWidget(hint);
  layout->addLayout(bar);
  scene = new QGraphicsScene(this);
  view = new ZoomView(scene, this);
  view->setRenderHint(QPainter::Antialiasing);
  view->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  view->setDragMode(QGraphicsView::ScrollHandDrag);
  view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  layout->addWidget(view);
  connect(back, &QPushButton::clicked, this, [this] { showOverview(); });
  hide();
}

void SislVisualizer::setDescription(sisl::IsaDescription value) {
  description = std::move(value);
  showOverview();
}

void SislVisualizer::drawLayout(
    const std::vector<sisl::FieldDescription>& fields, std::size_t width,
    const std::vector<sisl::FixedFieldDescription>& fixed, qreal y) {
  const QColor ink = palette().color(QPalette::Text);
  const QColor base = palette().color(QPalette::Base);
  const QColor accent = palette().color(QPalette::Highlight);
  const QColor shaded = palette().color(QPalette::AlternateBase);
  const qreal top = y + 17;
  std::vector<bool> indexes(width, false);
  if (width) {
    indexes.front() = true;
    indexes.back() = true;
  }
  std::vector<bool> occupied(width, false);
  for (const auto& field : fields) {
    const auto found = std::find_if(fixed.begin(), fixed.end(), [&](const auto& item) {
      return item.name == field.name;
    });
    for (const auto& mapping : field.mappings) {
      const auto high = mapping.instruction_bits.msb;
      const auto low = mapping.instruction_bits.lsb;
      if (high >= width || high < low) continue;
      indexes[high] = true;
      indexes[low] = true;
      const qreal x = left + (width - high - 1) * cell;
      const qreal w = (high - low + 1) * cell;
      for (auto bit = low; bit <= high; ++bit) occupied[bit] = true;
      auto* rect = scene->addRect(x, top, w, 33, QPen(ink), QBrush(found == fixed.end() ? shaded : accent));
      rect->setAcceptedMouseButtons(Qt::NoButton);
      QString tooltip = QString::fromStdString(field.name) + QStringLiteral(" : ")
                        + QString::number(field.width) + QStringLiteral(" bits");
      if (!field.enumeration.empty()) tooltip += QStringLiteral(" (") + QString::fromStdString(field.enumeration) + QLatin1Char(')');
      if (found != fixed.end() && found->enum_member)
        tooltip += QStringLiteral(" = ") + QString::fromStdString(*found->enum_member);
      rect->setToolTip(tooltip);
      const auto drawFitted = [&](const QString& content, qreal maxScale, qreal textY,
                                  bool centered) {
        auto* text = scene->addSimpleText(content);
        text->setBrush(found == fixed.end() ? ink : palette().color(QPalette::HighlightedText));
        const qreal scale = std::min(maxScale, (w - 6) / std::max<qreal>(1, text->boundingRect().width()));
        text->setScale(scale);
        text->setPos(centered ? x + (w - text->boundingRect().width() * scale) / 2 : x + 3,
                     textY);
        text->setToolTip(tooltip);
        text->setAcceptedMouseButtons(Qt::NoButton);
      };
      if (found == fixed.end()) {
        drawFitted(sliceLabel(field, mapping), 1.0, top + 5, false);
      } else {
        const auto slice = (found->value >> mapping.field_bits.lsb)
                           & ((sisl::Integer{1} << (high - low + 1)) - 1);
        const QString encoded = bits(slice, high - low + 1);
        drawFitted(w < 70 ? encoded.mid(2) : encoded, 0.8, top + 1, true);
        drawFitted(sliceLabel(field, mapping), 0.55, top + 18, true);
      }
    }
  }
  for (std::size_t bit = 0; bit < width;) {
    if (occupied[bit]) { ++bit; continue; }
    const auto low = bit;
    while (bit < width && !occupied[bit]) ++bit;
    const auto high = bit - 1;
    indexes[high] = true;
    indexes[low] = true;
    const qreal x = left + (width - high - 1) * cell;
    const qreal w = (high - low + 1) * cell;
    scene->addRect(x, top, w, 33, QPen(ink), QBrush(base))->setAcceptedMouseButtons(Qt::NoButton);
    addText(scene, QStringLiteral("—"), x + w / 2 - 4, top + 5, ink);
  }
  for (std::size_t bit = 0; bit < width; ++bit) {
    if (!indexes[bit]) continue;
    auto* index = scene->addSimpleText(QString::number(bit));
    index->setBrush(ink);
    index->setScale(0.7);
    index->setAcceptedMouseButtons(Qt::NoButton);
    const qreal x = left + (width - bit - 1) * cell;
    index->setPos(x + (cell - index->boundingRect().width() * 0.7) / 2, y);
  }
}

void SislVisualizer::showOverview() {
  scene->clear();
  back->hide();
  hint->show();
  const QColor ink = palette().color(QPalette::Text);
  addText(scene, tr("%1 · Instruction formats").arg(QString::fromStdString(description.name)), left, 0, ink, 1.4);
  qreal y = 48;
  for (const auto& format : description.formats) {
    const auto width = effectiveWidth(format);
    const QString title = QString::fromStdString(format.name)
                          + (format.parent ? tr(" : %1").arg(QString::fromStdString(*format.parent)) : QString())
                          + (format.width ? tr(" · %1 bits").arg(static_cast<qulonglong>(*format.width)) : tr(" · width unspecified"));
    auto* box = new FormatBox(QRectF(left - 15, y - 5, std::max<qreal>(550, width * cell + 35), rowHeight + 25),
                              [this, name = format.name] {
                                QTimer::singleShot(0, this, [this, name] { showFormat(name); });
                              });
    box->setPen(QPen(palette().color(QPalette::Mid)));
    scene->addItem(box);
    addText(scene, title, left, y, ink);
    if (width) drawLayout(format.fields, width, {}, y + 22);
    y += rowHeight + 42;
  }
  const bool standalone = std::any_of(description.instructions.begin(), description.instructions.end(),
                                      [](const auto& item) { return !item.format; });
  if (standalone) {
    auto* box = new FormatBox(QRectF(left - 15, y, 550, 48), [this] {
      QTimer::singleShot(0, this, [this] { showFormat(std::nullopt); });
    });
    box->setPen(QPen(palette().color(QPalette::Mid)));
    scene->addItem(box);
    addText(scene, tr("Standalone instructions · double-click"), left, y + 12, ink);
    y += 70;
  }
  y += 15;
  addText(scene, tr("Enum values"), left, y, ink, 1.25);
  y += 35;
  for (const auto& enumeration : description.enums) {
    addText(scene, QString::fromStdString(enumeration.name) + tr(" · %1 bits").arg(static_cast<qulonglong>(enumeration.width)), left, y, ink);
    y += 25;
    for (const auto& [name, value] : enumeration.members) {
      addText(scene, QString::fromStdString(name) + QStringLiteral(" = ") + bits(value, enumeration.width), left + 20, y, ink);
      y += 22;
    }
    y += 15;
  }
  scene->setSceneRect(scene->itemsBoundingRect().adjusted(-25, -20, 45, 25));
  view->resetTransform();
}

void SislVisualizer::showFormat(const std::optional<std::string>& name) {
  scene->clear();
  back->show();
  hint->hide();
  const QColor ink = palette().color(QPalette::Text);
  const QString title = name ? QString::fromStdString(*name) : tr("Standalone instructions");
  addText(scene, title, left, 0, ink, 1.4);
  qreal y = 50;
  if (name) {
    const auto format = std::find_if(description.formats.begin(), description.formats.end(),
                                     [&](const auto& item) { return item.name == *name; });
    if (format != description.formats.end()) {
      const auto width = effectiveWidth(*format);
      auto* box = scene->addRect(left - 15, y - 5,
                                 std::max<qreal>(550, width * cell + 35), 83,
                                 QPen(palette().color(QPalette::Mid)));
      box->setAcceptedMouseButtons(Qt::NoButton);
      addText(scene, tr("Format structure%1")
                         .arg(format->parent ? tr(" · inherits %1").arg(QString::fromStdString(*format->parent))
                                             : QString()), left, y, ink);
      if (width)
        drawLayout(format->fields, width, {}, y + 22);
      y += 115;
    }
  }
  addText(scene, tr("Instructions"), left, y, ink, 1.15);
  y += 38;
  bool any = false;
  for (const auto& instruction : description.instructions) {
    if (instruction.format != name) continue;
    any = true;
    addText(scene, QString::fromStdString(instruction.assembly.empty()
                                             ? instruction.name : instruction.assembly),
            left, y, ink, 1.1);
    drawLayout(instruction.fields, instruction.width, instruction.fixed_fields, y + 22);
    y += 105;
    for (const auto& alias : description.aliases) {
      if (alias.target != instruction.name) continue;
      auto merged = instruction.fixed_fields;
      merged.insert(merged.end(), alias.additional_fixed_fields.begin(), alias.additional_fixed_fields.end());
      addText(scene, tr("Alias: %1 → %2").arg(QString::fromStdString(alias.assembly.empty()
                                                                         ? alias.name : alias.assembly),
                                               QString::fromStdString(alias.target)), left + 16, y, ink);
      drawLayout(instruction.fields, instruction.width, merged, y + 22);
      y += 100;
    }
    y += 10;
  }
  if (!any) addText(scene, tr("No instructions use this format."), left, y, ink);
  scene->setSceneRect(scene->itemsBoundingRect().adjusted(-25, -20, 45, 25));
  view->resetTransform();
}

} // namespace SILICON::ui
