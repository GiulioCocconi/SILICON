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

namespace Layout {
  constexpr qreal CELL_W = 27.0;
  constexpr qreal LEFT_M = 34.0;
  constexpr qreal ROW_H = 55.0;
  constexpr qreal RECT_H = 33.0;
  constexpr qreal BOX_PAD_X = 15.0;
  constexpr qreal BOX_PAD_Y = 5.0;
  constexpr qreal BASE_Y = 48.0;

  constexpr qreal TITLE_SCALE = 1.4;
  constexpr qreal SUB_TITLE_SCALE = 1.15;
  constexpr qreal NORMAL_SCALE = 1.0;
  constexpr qreal INDEX_SCALE = 0.7;

  inline qreal calcBoxWidth(std::size_t bitsWidth) {
    return std::max<qreal>(550.0, bitsWidth * CELL_W + 35.0);
  }
} // namespace Layout

struct SliceGeometry {
  qreal x;
  qreal w;

  static SliceGeometry calculate(std::size_t totalWidth, std::size_t msb, std::size_t lsb) {
    return {
      Layout::LEFT_M + (totalWidth - msb - 1) * Layout::CELL_W,
      (msb - lsb + 1) * Layout::CELL_W
    };
  }
};

// --- Custom UI Elements ---

class ZoomView final : public QGraphicsView {
public:
  using QGraphicsView::QGraphicsView;

protected:
  void wheelEvent(QWheelEvent* event) override {
    constexpr qreal ZOOM_IN = 1.15;
    constexpr qreal ZOOM_OUT = 1.0 / ZOOM_IN;
    constexpr qreal MIN_SCALE = 0.2;
    constexpr qreal MAX_SCALE = 4.0;

    const qreal factor = event->angleDelta().y() > 0 ? ZOOM_IN : ZOOM_OUT;
    const qreal nextScale = transform().m11() * factor;

    if (nextScale >= MIN_SCALE && nextScale <= MAX_SCALE) {
      scale(factor, factor);
    }
    event->accept();
  }
};

class FormatBox final : public QGraphicsRectItem {
public:
  FormatBox(const QRectF& rect, std::function<void()> onDoubleClick)
    : QGraphicsRectItem(rect), open(std::move(onDoubleClick)) {
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


// --- Helper Functions ---

QGraphicsSimpleTextItem* addText(QGraphicsScene* scene, const QString& value, qreal x, qreal y,
                                 const QColor& color, qreal scale = Layout::NORMAL_SCALE) {
  auto* item = scene->addSimpleText(value);
  item->setBrush(color);
  item->setPos(x, y);
  item->setScale(scale);
  item->setAcceptedMouseButtons(Qt::NoButton);
  return item;
}

void addFittedText(QGraphicsScene* scene, const QString& text, const SliceGeometry& geom, qreal yOffset,
                   const QColor& color, const QString& tooltip, qreal maxScale, bool centered) {
  auto* item = addText(scene, text, 0, geom.x, color); // Temp X pos

  const qreal scale = std::min(maxScale, (geom.w - 6.0) / std::max<qreal>(1.0, item->boundingRect().width()));
  item->setScale(scale);

  const qreal xPos = centered
                   ? geom.x + (geom.w - item->boundingRect().width() * scale) / 2.0
                   : geom.x + 3.0;

  item->setPos(xPos, yOffset);
  item->setToolTip(tooltip);
}

QString formatBinaryString(const sisl::Integer& value, std::size_t width) {
  QString result(width + 2, QLatin1Char('0'));
  result[0] = '0';
  result[1] = 'b';
  for (std::size_t i = 0; i < width; ++i) {
    if ((value >> i) & 1) {
      result[width + 1 - i] = '1';
    }
  }
  return result;
}

QString formatSliceLabel(const sisl::FieldDescription& field, const sisl::FieldMapping& mapping) {
  const auto& range = mapping.field_bits;
  const QString name = QString::fromStdString(field.name);

  const bool coversEntireField = (range.lsb == 0 && range.msb + 1 == field.width);
  if (coversEntireField) return name;

  if (range.msb == range.lsb) {
    return QStringLiteral("%1[%2]").arg(name).arg(range.msb);
  }
  return QStringLiteral("%1[%2:%3]").arg(name).arg(range.msb).arg(range.lsb);
}

std::size_t getEffectiveWidth(const sisl::FormatDescription& format) {
  if (format.width) return *format.width;

  std::size_t width = 0;
  for (const auto& field : format.fields) {
    for (const auto& mapping : field.mappings) {
      width = std::max(width, mapping.instruction_bits.msb + 1);
    }
  }
  return width;
}

const sisl::FixedFieldDescription* findFixedField(const std::string& name, const std::vector<sisl::FixedFieldDescription>& fixedFields) {
  auto it = std::find_if(fixedFields.begin(), fixedFields.end(), [&](const auto& item) {
    return item.name == name;
  });
  return it != fixedFields.end() ? &(*it) : nullptr;
}

} // namespace

// --- Class Implementation ---

ThemeColors::ThemeColors(const QPalette& pal)
  : ink(pal.color(QPalette::Text)),
    base(pal.color(QPalette::Base)),
    accent(pal.color(QPalette::Highlight)),
    shaded(pal.color(QPalette::AlternateBase)),
    highlightedText(pal.color(QPalette::HighlightedText)),
    mid(pal.color(QPalette::Mid)) {}


SislVisualizer::SislVisualizer(QWidget* parent) : QWidget(parent) {
  setupUI();
}

void SislVisualizer::setupUI() {
  auto* layout = new QVBoxLayout(this);
  auto* topBar = new QHBoxLayout();

  backButton = new QPushButton(tr("← Formats"), this);
  hintLabel = new QLabel(tr("Double-click a format to see its instructions"), this);

  topBar->addWidget(backButton);
  topBar->addStretch();
  topBar->addWidget(hintLabel);
  layout->addLayout(topBar);

  scene = new QGraphicsScene(this);
  view = new ZoomView(scene, this);
  view->setRenderHint(QPainter::Antialiasing);
  view->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  view->setDragMode(QGraphicsView::ScrollHandDrag);
  view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  layout->addWidget(view);

  connect(backButton, &QPushButton::clicked, this, &SislVisualizer::showOverview);
  hide();
}

void SislVisualizer::setDescription(sisl::IsaDescription value) {
  description = std::move(value);
  showOverview();
}

void SislVisualizer::resetScene(bool isOverview) {
  scene->clear();
  backButton->setVisible(!isOverview);
  hintLabel->setVisible(isOverview);
}

void SislVisualizer::finalizeScene() {
  scene->setSceneRect(scene->itemsBoundingRect().adjusted(-25, -20, 45, 25));
  view->resetTransform();
}

// --- Overview Routines ---

void SislVisualizer::showOverview() {
  resetScene(true);
  ThemeColors theme(palette());

  drawOverviewTitle();

  qreal y = Layout::BASE_Y;
  drawAllFormatBoxes(theme, y);
  drawStandaloneInstructionsBox(theme, y);
  drawAllEnums(theme, y);

  finalizeScene();
}

void SislVisualizer::drawOverviewTitle() {
  const QString title = tr("%1 · Instruction formats").arg(QString::fromStdString(description.name));
  addText(scene, title, Layout::LEFT_M, 0, palette().color(QPalette::Text), Layout::TITLE_SCALE);
}

void SislVisualizer::drawAllFormatBoxes(const ThemeColors& theme, qreal& y) {
  for (const auto& format : description.formats) {
    const auto width = getEffectiveWidth(format);

    QString title = QString::fromStdString(format.name);
    if (format.parent) title += tr(" : %1").arg(QString::fromStdString(*format.parent));
    title += format.width ? tr(" · %1 bits").arg(static_cast<qulonglong>(*format.width)) : tr(" · width unspecified");

    auto* box = new FormatBox(
      QRectF(Layout::LEFT_M - Layout::BOX_PAD_X, y - Layout::BOX_PAD_Y, Layout::calcBoxWidth(width), Layout::ROW_H + 25),
      [this, name = format.name] { QTimer::singleShot(0, this, [this, name] { showFormat(name); }); }
    );

    box->setPen(QPen(theme.mid));
    scene->addItem(box);

    addText(scene, title, Layout::LEFT_M, y, theme.ink);
    if (width > 0) {
      drawLayout(format.fields, width, {}, y + 22, theme);
    }

    y += Layout::ROW_H + 42;
  }
}

void SislVisualizer::drawStandaloneInstructionsBox(const ThemeColors& theme, qreal& y) {
  const bool hasStandalone = std::any_of(description.instructions.begin(), description.instructions.end(),
                                         [](const auto& item) { return !item.format; });
  if (!hasStandalone) return;

  auto* box = new FormatBox(QRectF(Layout::LEFT_M - Layout::BOX_PAD_X, y, Layout::calcBoxWidth(0), 48), [this] {
    QTimer::singleShot(0, this, [this] { showFormat(std::nullopt); });
  });

  box->setPen(QPen(theme.mid));
  scene->addItem(box);

  addText(scene, tr("Standalone instructions · double-click"), Layout::LEFT_M, y + 12, theme.ink);
  y += 70;
}

void SislVisualizer::drawAllEnums(const ThemeColors& theme, qreal& y) {
  y += 15;
  addText(scene, tr("Enum values"), Layout::LEFT_M, y, theme.ink, 1.25);
  y += 35;

  for (const auto& enumeration : description.enums) {
    const QString title = QString::fromStdString(enumeration.name) + tr(" · %1 bits").arg(static_cast<qulonglong>(enumeration.width));
    addText(scene, title, Layout::LEFT_M, y, theme.ink);
    y += 25;

    for (const auto& [name, value] : enumeration.members) {
      const QString itemText = QStringLiteral("%1 = %2").arg(QString::fromStdString(name), formatBinaryString(value, enumeration.width));
      addText(scene, itemText, Layout::LEFT_M + 20, y, theme.ink);
      y += 22;
    }
    y += 15;
  }
}

// --- Format Detail Routines ---

void SislVisualizer::showFormat(const std::optional<std::string>& formatName) {
  resetScene(false);
  ThemeColors theme(palette());

  const QString pageTitle = formatName ? QString::fromStdString(*formatName) : tr("Standalone instructions");
  addText(scene, pageTitle, Layout::LEFT_M, 0, theme.ink, Layout::TITLE_SCALE);

  qreal y = 50;
  if (formatName) {
    drawFormatHeader(*formatName, theme, y);
  }

  drawInstructionsForFormat(formatName, theme, y);
  finalizeScene();
}

void SislVisualizer::drawFormatHeader(const std::string& formatName, const ThemeColors& theme, qreal& y) {
  auto format = std::find_if(description.formats.begin(), description.formats.end(),
                             [&](const auto& item) { return item.name == formatName; });

  if (format == description.formats.end()) return;

  const auto width = getEffectiveWidth(*format);
  auto* box = scene->addRect(Layout::LEFT_M - Layout::BOX_PAD_X, y - Layout::BOX_PAD_Y,
                             Layout::calcBoxWidth(width), 83, QPen(theme.mid));
  box->setAcceptedMouseButtons(Qt::NoButton);

  const QString inheritance = format->parent ? tr(" · inherits %1").arg(QString::fromStdString(*format->parent)) : QString();
  addText(scene, tr("Format structure%1").arg(inheritance), Layout::LEFT_M, y, theme.ink);

  if (width > 0) {
    drawLayout(format->fields, width, {}, y + 22, theme);
  }
  y += 115;
}

void SislVisualizer::drawInstructionsForFormat(const std::optional<std::string>& formatName, const ThemeColors& theme, qreal& y) {
  addText(scene, tr("Instructions"), Layout::LEFT_M, y, theme.ink, Layout::SUB_TITLE_SCALE);
  y += 38;

  bool hasAnyInstructions = false;

  for (const auto& instruction : description.instructions) {
    if (instruction.format != formatName) continue;
    hasAnyInstructions = true;

    const QString instName = QString::fromStdString(instruction.assembly.empty() ? instruction.name : instruction.assembly);
    addText(scene, instName, Layout::LEFT_M, y, theme.ink, 1.1);

    drawLayout(instruction.fields, instruction.width, instruction.fixed_fields, y + 22, theme);
    y += 105;

    drawAliases(instruction, theme, y);
    y += 10;
  }

  if (!hasAnyInstructions) {
    addText(scene, tr("No instructions use this format."), Layout::LEFT_M, y, theme.ink);
  }
}

void SislVisualizer::drawAliases(const sisl::InstructionDescription& inst, const ThemeColors& theme, qreal& y) {
  for (const auto& alias : description.aliases) {
    if (alias.target != inst.name) continue;

    auto mergedFixedFields = inst.fixed_fields;
    mergedFixedFields.insert(mergedFixedFields.end(), alias.additional_fixed_fields.begin(), alias.additional_fixed_fields.end());

    const QString aliasName = QString::fromStdString(alias.assembly.empty() ? alias.name : alias.assembly);
    const QString title = tr("Alias: %1 → %2").arg(aliasName, QString::fromStdString(alias.target));

    addText(scene, title, Layout::LEFT_M + 16, y, theme.ink);
    drawLayout(inst.fields, inst.width, mergedFixedFields, y + 22, theme);
    y += 100;
  }
}

// --- Layout Engine ---

void SislVisualizer::drawLayout(const std::vector<sisl::FieldDescription>& fields,
                                std::size_t width,
                                const std::vector<sisl::FixedFieldDescription>& fixedFields,
                                qreal y,
                                const ThemeColors& theme) {
  std::vector<bool> indexes(width, false);
  std::vector<bool> occupied(width, false);

  if (width > 0) {
    indexes.front() = true;
    indexes.back() = true;
  }

  const qreal top = y + 17;

  drawFieldSlices(fields, width, fixedFields, top, theme, occupied, indexes);
  drawUnoccupiedSlices(width, top, theme, occupied, indexes);
  drawAxisIndices(width, y, theme, indexes);
}

void SislVisualizer::drawFieldSlices(const std::vector<sisl::FieldDescription>& fields,
                                     std::size_t width,
                                     const std::vector<sisl::FixedFieldDescription>& fixedFields,
                                     qreal top,
                                     const ThemeColors& theme,
                                     std::vector<bool>& occupied,
                                     std::vector<bool>& indexes) {
  for (const auto& field : fields) {
    const sisl::FixedFieldDescription* fixedInfo = findFixedField(field.name, fixedFields);
    const bool isFixed = (fixedInfo != nullptr);

    for (const auto& mapping : field.mappings) {
      const auto high = mapping.instruction_bits.msb;
      const auto low = mapping.instruction_bits.lsb;

      if (high >= width || high < low) continue;

      indexes[high] = true;
      indexes[low] = true;
      for (auto bit = low; bit <= high; ++bit) occupied[bit] = true;

      const SliceGeometry geom = SliceGeometry::calculate(width, high, low);

      auto* rect = scene->addRect(geom.x, top, geom.w, Layout::RECT_H, QPen(theme.ink), QBrush(isFixed ? theme.accent : theme.shaded));
      rect->setAcceptedMouseButtons(Qt::NoButton);

      QString tooltip = QStringLiteral("%1 : %2 bits").arg(QString::fromStdString(field.name)).arg(field.width);
      if (!field.enumeration.empty()) tooltip += QStringLiteral(" (%1)").arg(QString::fromStdString(field.enumeration));
      if (isFixed && fixedInfo->enum_member) tooltip += QStringLiteral(" = %1").arg(QString::fromStdString(*fixedInfo->enum_member));

      rect->setToolTip(tooltip);

      const QColor textColor = isFixed ? theme.highlightedText : theme.ink;

      if (!isFixed) {
        addFittedText(scene, formatSliceLabel(field, mapping), geom, top + 5, textColor, tooltip, 1.0, false);
      } else {
        const auto sliceWidth = high - low + 1;
        const auto sliceVal = (fixedInfo->value >> mapping.field_bits.lsb) & ((sisl::Integer{1} << sliceWidth) - 1);
        const QString encoded = formatBinaryString(sliceVal, sliceWidth);

        const QString bitString = (geom.w < 70) ? encoded.mid(2) : encoded; // Trim "0b" prefix if too small

        addFittedText(scene, bitString, geom, top + 1, textColor, tooltip, 0.8, true);
        addFittedText(scene, formatSliceLabel(field, mapping), geom, top + 18, textColor, tooltip, 0.55, true);
      }
    }
  }
}

void SislVisualizer::drawUnoccupiedSlices(std::size_t width,
                                          qreal top,
                                          const ThemeColors& theme,
                                          std::vector<bool>& occupied,
                                          std::vector<bool>& indexes) {
  for (std::size_t bit = 0; bit < width;) {
    if (occupied[bit]) {
      ++bit;
      continue;
    }

    const auto low = bit;
    while (bit < width && !occupied[bit]) {
      ++bit;
    }
    const auto high = bit - 1;

    indexes[high] = true;
    indexes[low] = true;

    const SliceGeometry geom = SliceGeometry::calculate(width, high, low);

    auto* rect = scene->addRect(geom.x, top, geom.w, Layout::RECT_H, QPen(theme.ink), QBrush(theme.base));
    rect->setAcceptedMouseButtons(Qt::NoButton);

    auto* dash = addText(scene, QStringLiteral("—"), 0, top + 5, theme.ink);
    dash->setPos(geom.x + (geom.w - dash->boundingRect().width()) / 2.0, top + 5);
  }
}

void SislVisualizer::drawAxisIndices(std::size_t width, qreal y, const ThemeColors& theme, const std::vector<bool>& indexes) {
  for (std::size_t bit = 0; bit < width; ++bit) {
    if (!indexes[bit]) continue;

    auto* index = addText(scene, QString::number(bit), 0, y, theme.ink, Layout::INDEX_SCALE);
    const qreal targetX = Layout::LEFT_M + (width - bit - 1) * Layout::CELL_W;

    index->setPos(targetX + (Layout::CELL_W - index->boundingRect().width() * Layout::INDEX_SCALE) / 2.0, y);
  }
}

} // namespace SILICON::ui
