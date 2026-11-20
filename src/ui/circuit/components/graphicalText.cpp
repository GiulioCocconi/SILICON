/*
  Copyright (c) 2026. Giulio Cocconi

  SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "graphicalText.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <vector>

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QGraphicsTextItem>
#include <QImage>
#include <QObject>
#include <QPainter>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextOption>

#include <jkqtmathtext/jkqtmathtext.h>

#include <ui/shell/theme.hpp>

namespace SILICON::ui {
namespace {

  const StringPropertyOptions FontOptions{"Quicksand", "Chango", "NovaMono"};

  // Inline objects let Qt wrap prose around vector-rendered equations and include
  // their full height when measuring annotations and resizing them.
  class MathTextObjects : public QObject, public QTextObjectInterface {
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)

  public:
    static constexpr int ObjectType   = QTextFormat::UserObject;
    static constexpr int FormulaIndex = QTextFormat::UserProperty;

    void registerWith(QTextDocument* document)
    {
      document->documentLayout()->registerHandler(ObjectType, this);
    }

    bool insert(QTextCursor& cursor, QStringView source, const QTextCharFormat& format)
    {
      auto math = std::make_unique<JKQTMathText>();
      math->setFontSize(cursor.document()->defaultFont().pointSizeF());
      math->setFontRoman(cursor.document()->defaultFont().family());
      math->setFontOption(JKQTMathText::BaseFontBold, format.fontWeight() >= QFont::Bold);
      math->setFontOption(JKQTMathText::BaseFontItalic, format.fontItalic());
      if (!math->parse(source.toString()))
        return false;

      QImage     device(1, 1, QImage::Format_ARGB32_Premultiplied);
      QPainter   painter(&device);
      const auto size = math->getSize(painter);
      if (!size.isValid() || size.isEmpty())
        return false;

      auto objectFormat = format;
      objectFormat.setObjectType(ObjectType);
      objectFormat.setProperty(FormulaIndex, static_cast<int>(formulas.size()));
      objectFormat.setVerticalAlignment(QTextCharFormat::AlignMiddle);
      formulas.push_back({std::move(math), size});
      cursor.insertText(QString(QChar::ObjectReplacementCharacter), objectFormat);
      return true;
    }

    QSizeF intrinsicSize(QTextDocument*, int, const QTextFormat& format) override
    {
      return formulas.at(format.intProperty(FormulaIndex)).size;
    }

    void drawObject(QPainter* painter, const QRectF& rect, QTextDocument*, int,
                    const QTextFormat& format) override
    {
      auto& math = *formulas.at(format.intProperty(FormulaIndex)).math;
      painter->save();
      painter->setRenderHint(QPainter::Antialiasing);
      painter->setRenderHint(QPainter::TextAntialiasing);
      math.setFontColor(ThemeEngine::getColor(theme::ColorToken::Ink));
      math.draw(*painter, Qt::AlignCenter, rect);
      painter->restore();
    }

  private:
    struct Formula {
      std::unique_ptr<JKQTMathText> math;
      QSizeF                        size;
    };
    std::vector<Formula> formulas;
  };

  bool isFormattingCharacter(QChar character)
  {
    return std::ranges::contains(QStringView(u"*_$\\"), character);
  }

  qsizetype findClosingDelimiter(QStringView text, QChar delimiter)
  {
    bool escaped = false;
    for (qsizetype index = 0; index < text.size(); ++index) {
      const auto character = text[index];
      if (delimiter != '$' && (character == '\n' || character == '\r'))
        break;
      if (!escaped && character == delimiter)
        return index;
      // Emphasis delimiters inside an equation belong to LaTeX.
      if (!escaped && character == '$' && delimiter != '$') {
        const auto length = findClosingDelimiter(text.mid(index + 1), '$');
        if (length >= 0)
          index += length + 1;
      }
      escaped = !escaped && character == '\\';
    }
    return -1;
  }

  void insertFormattedText(QTextCursor& cursor, MathTextObjects& math, QStringView text,
                           const QTextCharFormat& format = {})
  {
    QString plain;
    for (qsizetype index = 0; index < text.size();) {
      const auto character = text[index++];
      if (character == '\\' && index < text.size()
          && isFormattingCharacter(text[index])) {
        plain += text[index++];
        continue;
      }

      const auto length = character == '*' || character == '_' || character == '$'
                              ? findClosingDelimiter(text.mid(index), character)
                              : -1;
      // Unmatched and empty delimiters stay visible as ordinary text.
      if (length <= 0) {
        plain += character;
        continue;
      }

      cursor.insertText(plain, format);
      plain.clear();
      if (character == '$') {
        const auto source = text.mid(index - 1, length + 2);
        if (!math.insert(cursor, source, format))
          cursor.insertText(source.toString(), format);
        index += length + 1;
        continue;
      }
      auto emphasis = format;
      if (character == '*')
        emphasis.setFontWeight(QFont::Bold);
      else
        emphasis.setFontItalic(true);
      insertFormattedText(cursor, math, text.mid(index, length), emphasis);
      index += length + 1;
    }
    cursor.insertText(plain, format);
  }

  class TextShape : public QGraphicsTextItem {
  public:
    TextShape(const QString& text, const QFont& font, int wrapWidth)
    {
      setAcceptedMouseButtons(Qt::NoButton);
      setAcceptHoverEvents(false);
      setTextInteractionFlags(Qt::NoTextInteraction);
      document()->setDocumentMargin(0);
      auto option = document()->defaultTextOption();
      option.setWrapMode(QTextOption::WordWrap);
      document()->setDefaultTextOption(option);
      setFont(font);
      math.registerWith(document());
      QTextCursor cursor(document());
      insertFormattedText(cursor, math, text);
      // Measure the formatted text once; probes never alter the live item's geometry.
      measurement.reset(document()->clone());
      math.registerWith(measurement.get());
      measurement->setTextWidth(1);
      (void)measurement->size();
      for (auto block = measurement->begin(); block.isValid(); block = block.next()) {
        auto* layout = block.layout();
        for (int line = 0; line < layout->lineCount(); ++line)
          minimumWidth = std::max(minimumWidth, layout->lineAt(line).naturalTextWidth());
      }
      measurement->setTextWidth(-1);
      (void)measurement->size();
      for (auto block = measurement->begin(); block.isValid(); block = block.next()) {
        naturalWidth = std::max(naturalWidth, block.layout()->maximumWidth());
      }
      setWrapWidth(wrapWidth);
    }

    int clampWidth(qreal width) const
    {
      return static_cast<int>(std::clamp(std::ceil(std::max(width, minimumWidth)), 1.0,
                                         double(std::numeric_limits<int>::max())));
    }

    void setWrapWidth(int width) { setTextWidth(width > 0 ? clampWidth(width) : -1); }

    int automaticWidth() const { return clampWidth(naturalWidth); }

    int widthForHeight(qreal height, int startingWidth)
    {
      const int minimum  = clampWidth(minimumWidth);
      const int maximum  = std::max(automaticWidth(), startingWidth);
      const int start    = startingWidth > 0 ? clampWidth(startingWidth) : maximum;
      auto      heightAt = [this](int width) {
        measurement->setTextWidth(width);
        return measurement->size().height();
      };
      int low  = minimum;
      int high = maximum;
      while (low < high) {
        const int middle = low + (high - low) / 2;
        if (heightAt(middle) > height)
          low = middle + 1;
        else
          high = middle;
      }
      int best = start;
      // A whole interval of widths can produce the same height. Choose its point
      // nearest the starting width rather than jumping to the narrowest box.
      auto nearestWidth = [&](int width) {
        const qreal lineHeight = heightAt(width);
        int         first      = minimum;
        int         last       = maximum;
        while (first < last) {
          const int middle = first + (last - first) / 2;
          if (heightAt(middle) > lineHeight)
            first = middle + 1;
          else
            last = middle;
        }
        const int lower = first;
        last            = maximum;
        while (first < last) {
          const int middle = first + (last - first) / 2 + 1;
          if (heightAt(middle) >= lineHeight)
            first = middle;
          else
            last = middle - 1;
        }
        return std::clamp(start, lower, first);
      };
      auto score = [&](int width) {
        return std::tuple{std::abs(heightAt(width) - height), std::abs(width - start),
                          -width};
      };
      auto bestScore = score(best);
      for (int candidateWidth : {low, std::max(minimum, low - 1)}) {
        const int  width     = nearestWidth(candidateWidth);
        const auto candidate = score(width);
        if (candidate < bestScore) {
          best      = width;
          bestScore = candidate;
        }
      }
      return best;
    }

    int legacyWrapWidth(int length) const
    {
      if (length == 0)
        return 0;
      std::unique_ptr<QTextDocument> legacy(document()->clone());
      // QTextDocument::clone() does not copy custom object handlers.
      math.registerWith(legacy.get());
      legacy->setTextWidth(-1);
      QTextCursor cursor(legacy.get());
      for (auto block = legacy->begin(); block.isValid(); block = block.next()) {
        if (block.length() - 1 <= length)
          continue;
        cursor.setPosition(block.position());
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, length);
        if (cursor.block() == block && !cursor.atBlockEnd())
          cursor.insertBlock();
      }
      return clampWidth(legacy->idealWidth());
    }

    QRectF boundingRect() const override
    {
      // Empty annotations must remain easy to select and edit.
      return QGraphicsTextItem::boundingRect().united(
          QRectF(0, 0, GRID_SIZE, QFontMetricsF(font()).height()));
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override
    {
      const auto ink = ThemeEngine::getColor(theme::ColorToken::Ink);
      if (defaultTextColor() != ink)
        setDefaultTextColor(ink);
      QGraphicsTextItem::paint(painter, option, widget);
    }

  private:
    mutable MathTextObjects        math;
    std::unique_ptr<QTextDocument> measurement;
    qreal                          minimumWidth = GRID_SIZE;
    qreal                          naturalWidth = GRID_SIZE;
  };

}  // namespace

GraphicalText::GraphicalText(QGraphicsItem* parent) : GraphicalComponent(nullptr, parent)
{
  hiddenProperties.insert("wrapWidth");
  // Keep annotations above circuit components and wires, regardless of insertion order.
  setZValue(1);
  setProperties(properties);
}

std::optional<std::reference_wrapper<const StringPropertyOptions>>
GraphicalText::getStringPropertyOptions(std::string_view key) const
{
  if (key == "font")
    return std::cref(FontOptions);
  return std::nullopt;
}

void GraphicalText::setProperties(PropertyMap next)
{
  const auto* text      = std::get_if<std::string>(&next.at("text"));
  const auto* size      = std::get_if<int>(&next.at("size"));
  const auto* font      = std::get_if<std::string>(&next.at("font"));
  const auto* wrapWidth = std::get_if<int>(&next.at("wrapWidth"));
  if (!text || !size || !font || !wrapWidth)
    throw std::invalid_argument(
        "Text properties require text, integer size, font, and integer wrapWidth");
  if (*size <= 0)
    throw std::invalid_argument("Text size must be a positive number of points");
  if (*wrapWidth < 0)
    throw std::invalid_argument("Text wrapWidth must be non-negative");
  if (!std::ranges::contains(FontOptions, *font))
    throw std::invalid_argument("Text font must be Quicksand, Chango, or NovaMono");

  if (auto* existing = dynamic_cast<TextShape*>(getItemShape());
      existing && next.at("text") == properties.at("text")
      && next.at("size") == properties.at("size")
      && next.at("font") == properties.at("font")) {
    prepareSceneBoundsChange();
    existing->setWrapWidth(*wrapWidth);
    update();
  } else {
    auto shape = std::make_unique<TextShape>(QString::fromStdString(*text),
                                             QFont(QString::fromStdString(*font), *size),
                                             *wrapWidth);
    setItemShape(shape.release());
  }
  properties = std::move(next);
}

PropertyMap GraphicalText::resizedProperties(QSizeF size, ResizeHandle handle,
                                             const PropertyMap& initial)
{
  auto      next          = initial;
  auto*     shape         = static_cast<TextShape*>(getItemShape());
  const int startingWidth = std::get<int>(initial.at("wrapWidth"));
  const int width         = resizesLeft(handle) || resizesRight(handle)
                                ? shape->clampWidth(size.width())
                                : shape->widthForHeight(size.height(), startingWidth);
  // Keep automatic width when the reflow has not actually changed the box.
  const int initialWidth =
      startingWidth > 0 ? shape->clampWidth(startingWidth) : shape->automaticWidth();
  if (width == initialWidth)
    return next;
  next.at("wrapWidth") = width;
  return next;
}

void GraphicalText::applyProperty(std::string_view key, const PropertyValue& value)
{
  const auto found = properties.find(key);
  if (found == properties.end())
    throw std::invalid_argument("Unknown text property: " + std::string(key));
  if (found->second == value)
    return;
  auto next                 = properties;
  next.at(std::string(key)) = value;
  setProperties(std::move(next));
}

nlohmann::ordered_json GraphicalText::serialize() const
{
  auto json          = GraphicalComponent::serialize();
  json["properties"] = {{"text", std::get<std::string>(properties.at("text"))},
                        {"size", std::get<int>(properties.at("size"))},
                        {"font", std::get<std::string>(properties.at("font"))},
                        {"wrapWidth", std::get<int>(properties.at("wrapWidth"))}};
  return json;
}

void GraphicalText::loadSerializedState(const nlohmann::json& json)
{
  if (!json.contains("properties"))
    return;
  const auto& values = json.at("properties");
  if (!values.is_object())
    throw std::invalid_argument("Text properties must be an object");
  auto next = properties;
  if (values.contains("text"))
    next.at("text") = values.at("text").get<std::string>();
  for (const auto& [key, minimum] : {std::pair{"size", 1}, std::pair{"wrapWidth", 0}}) {
    if (!values.contains(key))
      continue;
    const auto& value = values.at(key);
    if (!value.is_number_integer() || value < minimum
        || value > std::numeric_limits<int>::max())
      throw std::invalid_argument(std::string(key)
                                  + " must be an integer >= " + std::to_string(minimum));
    next.at(key) = value.get<int>();
  }
  if (values.contains("font"))
    next.at("font") = values.at("font").get<std::string>();
  if (!values.contains("wrapWidth") && values.contains("wrapLength")) {
    const auto& length = values.at("wrapLength");
    if (!length.is_number_integer() || length < 0
        || length > std::numeric_limits<int>::max())
      throw std::invalid_argument("Text wrapLength must be a non-negative integer");
    // Validate all properties before constructing the temporary migration layout.
    GraphicalText legacy;
    legacy.setProperties(next);
    next.at("wrapWidth") = static_cast<TextShape*>(legacy.getItemShape())
                               ->legacyWrapWidth(length.get<int>());
  }
  setProperties(std::move(next));
}

}  // namespace SILICON::ui

#include "graphicalText.moc"
