/*
  Copyright (c) 2026. Giulio Cocconi

  SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "graphicalText.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>

#include <QFontMetricsF>
#include <QGraphicsTextItem>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>

#include <ui/shell/theme.hpp>
#include <utils/ranges_wrapper.hpp>

namespace SILICON::ui {
namespace {

  const StringPropertyOptions FontOptions{"Quicksand", "Chango", "NovaMono"};

  bool isFormattingCharacter(QChar character)
  {
    return std::ranges::contains(QStringView(u"*_\\"), character);
  }

  qsizetype findClosingDelimiter(QStringView text, QChar delimiter)
  {
    bool escaped = false;
    for (auto [index, character] : text | SILICON::views::enumerate) {
      if (character == '\n' || character == '\r')
        break;
      if (!escaped && character == delimiter)
        return index;
      escaped = !escaped && character == '\\';
    }
    return -1;
  }

  void insertFormattedText(QTextCursor& cursor, QStringView text,
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

      const auto length = character == '*' || character == '_'
                              ? findClosingDelimiter(text.mid(index), character)
                              : -1;
      // Unmatched and empty delimiters stay visible as ordinary text.
      if (length <= 0) {
        plain += character;
        continue;
      }

      cursor.insertText(plain, format);
      plain.clear();
      auto emphasis = format;
      if (character == '*')
        emphasis.setFontWeight(QFont::Bold);
      else
        emphasis.setFontItalic(true);
      insertFormattedText(cursor, text.mid(index, length), emphasis);
      index += length + 1;
    }
    cursor.insertText(plain, format);
  }

  class TextShape : public QGraphicsTextItem {
  public:
    TextShape(const QString& text, const QFont& font, int wrapLength)
    {
      setAcceptedMouseButtons(Qt::NoButton);
      setTextInteractionFlags(Qt::NoTextInteraction);
      document()->setDocumentMargin(0);
      setFont(font);
      QTextCursor cursor(document());
      insertFormattedText(cursor, text);
      if (wrapLength > 0) {
        // Wrap the rendered text so markup does not count toward the line length.
        // QTextCursor moves by complete characters, keeping Unicode sequences intact.
        cursor.beginEditBlock();
        for (auto block = document()->begin(); block.isValid(); block = block.next()) {
          if (block.length() - 1 <= wrapLength)
            continue;
          cursor.setPosition(block.position());
          cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor,
                              wrapLength);
          if (cursor.block() == block && !cursor.atBlockEnd())
            cursor.insertBlock();
        }
        cursor.endEditBlock();
      }
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
  };

}  // namespace

GraphicalText::GraphicalText(QGraphicsItem* parent) : GraphicalComponent(nullptr, parent)
{
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
  const auto* text       = std::get_if<std::string>(&next.at("text"));
  const auto* size       = std::get_if<int>(&next.at("size"));
  const auto* font       = std::get_if<std::string>(&next.at("font"));
  const auto* wrapLength = std::get_if<int>(&next.at("wrapLength"));
  if (!text || !size || !font || !wrapLength)
    throw std::invalid_argument(
        "Text properties require text, integer size, font, and integer wrapLength");
  if (*size <= 0)
    throw std::invalid_argument("Text size must be a positive number of points");
  if (*wrapLength < 0)
    throw std::invalid_argument("Text wrapLength must be non-negative");
  if (!std::ranges::contains(FontOptions, *font))
    throw std::invalid_argument("Text font must be Quicksand, Chango, or NovaMono");

  auto shape = std::make_unique<TextShape>(QString::fromStdString(*text),
                                           QFont(QString::fromStdString(*font), *size),
                                           *wrapLength);
  setItemShape(shape.release());
  properties = std::move(next);
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
                        {"wrapLength", std::get<int>(properties.at("wrapLength"))}};
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
  for (const auto& [key, minimum] : {std::pair{"size", 1}, std::pair{"wrapLength", 0}}) {
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
  setProperties(std::move(next));
}

}  // namespace SILICON::ui
