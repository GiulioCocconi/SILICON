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
#include "siliconStyle.hpp"

#include <algorithm>
#include <cmath>

#include <QAbstractScrollArea>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QScopeGuard>
#include <QStyleOption>
#include <QTabBar>
#include <QTabWidget>

#include <ui/shell/icons.hpp>
#include <ui/shell/theme.hpp>

namespace SILICON::ui {
namespace {
  using theme::ColorToken;
  // Fixed widget metrics belong to the style. Light/dark themes only supply colors.
  constexpr int borderWidth             = 2;
  constexpr int buttonRadius            = 12;
  constexpr int inputRadius             = 8;
  constexpr int menuRadius              = 8;
  constexpr int menuItemRadius          = 6;
  constexpr int indicatorRadius         = 4;
  constexpr int shadowOffset            = 4;
  constexpr int buttonLift              = 2;
  constexpr int controlHeight           = 34;
  constexpr int buttonHeight            = 42;
  constexpr int iconSize                = 16;
  constexpr int indicatorSize           = 16;
  constexpr int scrollBarThickness      = 18;
  constexpr int scrollBarMinimum        = 30;
  constexpr int scrollBarRadius         = 7;
  constexpr int menuArrowWidth          = 16;
  constexpr int tabHeight               = 30;
  constexpr int tabRadius               = 4;
  constexpr int spacingUnit             = 4;
  constexpr int controlHorizontal       = 10;
  constexpr int controlVertical         = 6;
  constexpr int buttonPaddingHorizontal = 18;
  constexpr int buttonPaddingVertical   = 10;
  constexpr int menuVertical            = 8;
  constexpr int menuMargin              = 4;
  constexpr int menuItemMargin          = 2;
  constexpr int toolbarSpacing          = 4;
  constexpr int contentMargin           = 9;
  constexpr int inputButtonWidth        = menuArrowWidth + spacingUnit * 2;

  QRectF alignedRect(const QPainter* p, const QRectF& rect)
  {
    // Snap outer edges in device space, including fractional DPR and translations.
    // Rotated/sheared controls keep their vector geometry instead of losing it to a
    // bbox.
    const auto transform = p->deviceTransform();
    if (transform.type() > QTransform::TxScale || !transform.isInvertible())
      return rect;
    auto device = transform.mapRect(rect);
    device.setCoords(std::round(device.left()), std::round(device.top()),
                     std::round(device.right()), std::round(device.bottom()));
    return transform.inverted().mapRect(device);
  }

  void rounded(QPainter* p, const QRectF& rect, const QColor& color, qreal radius)
  {
    if (rect.isEmpty())
      return;
    radius = std::max(qreal(0), std::min({radius, rect.width() / 2, rect.height() / 2}));
    p->setPen(Qt::NoPen);
    p->setBrush(color);
    p->drawRoundedRect(rect, radius, radius);
  }

  void panel(QPainter* p, QRectF rect, const QColor& fill, qreal radius, int width = 0,
             int shadow = 0)
  {
    p->save();
    const auto guard = qScopeGuard([p] { p->restore(); });
    p->setRenderHint(QPainter::Antialiasing);
    rect              = alignedRect(p, rect);
    const auto border = width ? ThemeEngine::getColor(ColorToken::Ink) : fill;
    if (shadow > 0) {
      rect.adjust(0, 0, -shadow, -shadow);
      rounded(p, rect.translated(shadow, shadow), border, radius);
    }
    // Filled nested shapes keep outlines inside the allocated geometry at any DPR.
    rounded(p, rect, border, radius);
    rounded(p, rect.adjusted(width, width, -width, -width), fill,
            std::max(qreal(0), radius - width));
  }

  // Remember only properties changed by polish(), so switching styles is reversible.
  constexpr auto originalFontProperty  = "_siliconOriginalFont";
  constexpr auto installedFontProperty = "_siliconInstalledFont";
  constexpr auto hoverProperty         = "_siliconOriginalHover";

  const QColor& foreground(QStyle::State state)
  {
    return ThemeEngine::getColor(state & QStyle::State_Enabled ? ColorToken::Ink
                                                               : ColorToken::Disabled);
  }

  bool hovered(QStyle::State state)
  {
    return (state & QStyle::State_Enabled) && (state & QStyle::State_MouseOver)
           && !(state & QStyle::State_Sunken);
  }

  const QColor& buttonBackground(QStyle::State state)
  {
    if (!(state & QStyle::State_Enabled))
      return ThemeEngine::getColor(ColorToken::Surface);
    if (state & QStyle::State_On)
      return ThemeEngine::getColor(ColorToken::Selected);
    return ThemeEngine::getColor(hovered(state) ? ColorToken::Hover
                                                : ColorToken::Surface);
  }

  const QColor& tabBackground(QStyle::State state)
  {
    if ((state & QStyle::State_Enabled) && (state & QStyle::State_Selected))
      return ThemeEngine::getColor(ColorToken::Surface);
    return ThemeEngine::getColor(hovered(state) ? ColorToken::Hover
                                                : ColorToken::Background);
  }

  const QColor& scrollThumbColor(QStyle::State state)
  {
    if (!(state & QStyle::State_Enabled))
      return ThemeEngine::getColor(ColorToken::Disabled);
    return ThemeEngine::getColor(state & (QStyle::State_MouseOver | QStyle::State_Sunken)
                                     ? ColorToken::Ink
                                     : ColorToken::Grey);
  }

  QColor menuBackground(QStyle::State state, QColor fallback = Qt::transparent)
  {
    // Menus use selection for keyboard navigation and hover/press for pointer input.
    constexpr auto highlightStates =
        QStyle::State_Selected | QStyle::State_MouseOver | QStyle::State_Sunken;
    if ((state & QStyle::State_Enabled) && (state & highlightStates))
      return ThemeEngine::getColor(ColorToken::MenuHover);
    return fallback;
  }

  bool hasClass(const QWidget* widget, const QString& name)
  {
    return widget && widget->property("class").toString().split(' ').contains(name);
  }

  void textPalette(QPalette& palette, const QColor& color)
  {
    for (const auto role : {QPalette::WindowText, QPalette::ButtonText, QPalette::Text})
      palette.setColor(QPalette::All, role, color);
  }

  QRect inset(QRect rect, int horizontal, int vertical)
  {
    return rect.adjusted(horizontal, vertical, -horizontal, -vertical);
  }

  int buttonOffset(QStyle::State state)
  {
    if (!(state & QStyle::State_Enabled))
      return 0;
    if (state & QStyle::State_Sunken)
      return buttonLift;
    return state & QStyle::State_MouseOver ? -buttonLift : 0;
  }

  QRect buttonFaceRect(const QStyleOption& option)
  {
    // Painting and label layout share the same lifted/sunken face.
    const int offset = buttonOffset(option.state);
    return option.rect.adjusted(buttonLift, buttonLift, -shadowOffset, -shadowOffset)
        .translated(offset, offset);
  }

  QColor separatorColor()
  {
    QColor color = ThemeEngine::getColor(ColorToken::Ink);
    color.setAlphaF(0.14);
    return color;
  }

  bool framelessLineEdit(const QStyleOption* option)
  {
    const auto* frame = qstyleoption_cast<const QStyleOptionFrame*>(option);
    return frame && frame->lineWidth == 0;
  }

  void inputPanel(QPainter* p, const QRect& rect)
  {
    panel(p, rect, ThemeEngine::getColor(ColorToken::Surface), inputRadius, borderWidth);
  }

  int inputVerticalPadding(const QStyleOption& option, int border)
  {
    // Existing compact fields can be explicitly shorter than their size hint.
    // Reduce padding before reducing the text's available height.
    const int available =
        std::max(0, option.rect.height() - option.fontMetrics.height() - 2 * border);
    return std::min(controlVertical, available / 2);
  }

  struct InputGeometry {
    QRect editField;
    QRect buttons;
  };

  InputGeometry inputGeometry(const QStyleOption& option, bool frame, int buttonWidth)
  {
    const QRect r        = option.rect;
    const int   border   = frame ? borderWidth : 0;
    const int   vertical = inputVerticalPadding(option, border);
    const QRect edit =
        r.adjusted(border + controlHorizontal, border + vertical,
                   -buttonWidth - border - spacingUnit, -border - vertical);
    const QRect buttons(r.right() + 1 - buttonWidth - border, r.top() + border,
                        buttonWidth, std::max(0, r.height() - border * 2));
    // Lay out once left-to-right, then mirror both regions for RTL widgets.
    return {.editField = QStyle::visualRect(option.direction, r, edit),
            .buttons   = QStyle::visualRect(option.direction, r, buttons)};
  }

  bool verticalTab(QTabBar::Shape shape)
  {
    return shape == QTabBar::RoundedWest || shape == QTabBar::RoundedEast
           || shape == QTabBar::TriangularWest || shape == QTabBar::TriangularEast;
  }

  QStyle::State subControlState(const QStyleOptionComplex& option,
                                QStyle::SubControl         control)
  {
    // The parent can be hovered/pressed while a different subcontrol is active.
    auto state = option.state;
    if (!(option.activeSubControls & control))
      state &= ~(QStyle::State_MouseOver | QStyle::State_Sunken);
    return state;
  }

  void drawSpinButton(const QStyle& style, QPainter* p, const QStyleOptionSpinBox& spin,
                      QStyle::SubControl control, const QRect& rect)
  {
    const bool   up = control == QStyle::SC_SpinBoxUp;
    QStyleOption button;
    button.rect    = rect;
    button.state   = subControlState(spin, control);
    button.palette = spin.palette;
    // Each direction has its own step limit, independent of the spinbox's state.
    const auto step =
        up ? QAbstractSpinBox::StepUpEnabled : QAbstractSpinBox::StepDownEnabled;
    if (!(spin.stepEnabled & step))
      button.state &= ~QStyle::State_Enabled;

    if (button.state & (QStyle::State_MouseOver | QStyle::State_Sunken))
      panel(p, rect, buttonBackground(button.state), indicatorRadius);
    if (spin.buttonSymbols != QAbstractSpinBox::PlusMinus) {
      if (!(button.state & QStyle::State_Enabled))
        button.palette.setCurrentColorGroup(QPalette::Disabled);

      style.drawPrimitive(
          up ? QStyle::PE_IndicatorArrowUp : QStyle::PE_IndicatorArrowDown, &button, p);
      return;
    }
    p->setPen(QPen(foreground(button.state), borderWidth));
    const auto center = rect.center();
    p->drawLine(center - QPoint(spacingUnit, 0), center + QPoint(spacingUnit, 0));
    if (up)
      p->drawLine(center - QPoint(0, spacingUnit), center + QPoint(0, spacingUnit));
  }

  struct ScrollGeometry {
    QRect groove;
    QRect slider;
    QRect subPage;
    QRect addPage;
  };

  ScrollGeometry scrollGeometry(const QStyleOptionSlider& option)
  {
    const bool  horizontal = option.orientation == Qt::Horizontal;
    const QRect r          = option.rect;
    const int   length     = std::max(0, horizontal ? r.width() : r.height());
    // Widen before subtraction and multiplication: Qt allows the full int range,
    // and page/position products can overflow even when the range fits in an int.
    const qint64 range = std::max(qint64(0), qint64(option.maximum) - option.minimum);
    const qint64 page  = std::max(0, option.pageStep);
    const int    thumb = range == 0 ? length
                                    : std::clamp(int(page * length / (range + page)),
                                                 std::min(length, scrollBarMinimum), length);
    const qint64 value =
        std::clamp(qint64(option.sliderPosition) - option.minimum, qint64(0), range);
    int offset = range == 0 ? 0 : int(value * (length - thumb) / range);

    if (option.upsideDown)
      offset = length - thumb - offset;
    const auto section = [&](int start, int size) {
      const QRect logical = horizontal ? QRect(r.x() + start, r.y(), size, r.height())
                                       : QRect(r.x(), r.y() + start, r.width(), size);
      return QStyle::visualRect(option.direction, r, logical);
    };
    const QRect before = section(0, offset);
    const QRect after  = section(offset + thumb, length - offset - thumb);
    return {.groove  = r,
            .slider  = section(offset, thumb),
            .subPage = option.upsideDown ? after : before,
            .addPage = option.upsideDown ? before : after};
  }
}  // namespace

SiliconStyle::SiliconStyle(QObject* parent) : QProxyStyle(QStringLiteral("Fusion"))
{
  setParent(parent);
  setObjectName("SiliconStyle");
}

QPalette SiliconStyle::standardPalette() const
{
  return ThemeEngine::current().palette();
}

// Override custom surfaces and geometry; delegate other elements to Fusion.
// Each drawing entry point restores painter state, including on early returns.
void SiliconStyle::drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                                 QPainter* p, const QWidget* widget) const
{
  if (!option || !p)
    return;
  p->save();
  const auto guard = qScopeGuard([p] { p->restore(); });

  switch (element) {
    case PE_PanelButtonCommand: {
      const bool pressed = (option->state & (State_Enabled | State_Sunken))
                           == (State_Enabled | State_Sunken);
      const auto face   = buttonFaceRect(*option);
      const int  shadow = pressed ? 0 : shadowOffset - buttonOffset(option->state);
      panel(p, face.adjusted(0, 0, shadow, shadow), buttonBackground(option->state),
            buttonRadius, borderWidth, shadow);
      return;
    }
    case PE_PanelButtonTool: {
      panel(p, option->rect, buttonBackground(option->state), indicatorRadius);
      return;
    }
    case PE_PanelToolBar:
      p->fillRect(option->rect, ThemeEngine::getColor(ColorToken::Background));
      return;
    case PE_IndicatorToolBarSeparator: {
      QRect line = inset(option->rect, spacingUnit, spacingUnit);
      if (option->state & State_Horizontal)
        line = QRect(option->rect.center().x(), line.top(), 1, line.height());
      else
        line = QRect(line.left(), option->rect.center().y(), line.width(), 1);
      p->fillRect(line, separatorColor());
      return;
    }
    case PE_PanelLineEdit:
    case PE_FrameLineEdit: {
      // With lineWidth == 0 the edit is deliberately frameless (e.g. inside a spinbox).
      if (framelessLineEdit(option)) {
        if (element == PE_PanelLineEdit)
          p->fillRect(option->rect, option->palette.brush(QPalette::Base));
        return;
      }

      inputPanel(p, option->rect);
      return;
    }
    case PE_Frame:
      // Views and editors already sit inside docks/pages. Do not frame every
      // nested viewport or draw another box when it receives keyboard focus.
      if (qobject_cast<const QAbstractScrollArea*>(widget))
        return;
      break;
    case PE_FrameTabWidget:
    case PE_FrameTabBarBase:
    case PE_FrameDockWidget:
    case PE_FrameFocusRect: return;
    case PE_PanelMenu:
      panel(p, option->rect, ThemeEngine::getColor(ColorToken::SurfaceRaised), menuRadius,
            borderWidth, shadowOffset);
      return;
    case PE_FrameMenu: return;  // PE_PanelMenu draws the complete border and shadow.
    case PE_IndicatorCheckBox:
    case PE_IndicatorRadioButton:
    case PE_IndicatorItemViewItemCheck: {
      const bool radio = element == PE_IndicatorRadioButton;
      const auto rect  = QStyle::alignedRect(
          option->direction, Qt::AlignCenter,
          QSize(indicatorSize, indicatorSize).boundedTo(option->rect.size()),
          option->rect);
      panel(p, rect, buttonBackground(option->state),
            radio ? rect.width() / 2.0 : indicatorRadius, borderWidth);
      const QRectF r = QRectF(rect).adjusted(borderWidth + 2, borderWidth + 2,
                                             -borderWidth - 2, -borderWidth - 2);
      p->setRenderHint(QPainter::Antialiasing);
      p->setPen(QPen(foreground(option->state), borderWidth, Qt::SolidLine, Qt::SquareCap,
                     Qt::MiterJoin));
      if (option->state & State_NoChange) {
        p->drawLine(QPointF(r.left(), r.center().y()),
                    QPointF(r.right(), r.center().y()));
        return;
      }
      if (!(option->state & State_On))
        return;
      if (radio) {
        p->setPen(Qt::NoPen);
        p->setBrush(foreground(option->state));
        p->drawEllipse(r);
        return;
      }
      const QPointF points[]{QPointF(r.left(), r.center().y()),
                             QPointF(r.center().x() - 1, r.bottom()), r.topRight()};
      p->drawPolyline(points, 3);
      return;
    }
    case PE_IndicatorTabClose: {
      if (option->state & State_MouseOver)
        panel(p, option->rect, buttonBackground(option->state), indicatorRadius);
      const auto r = inset(option->rect, spacingUnit, spacingUnit);
      p->setRenderHint(QPainter::Antialiasing);
      p->setPen(QPen(foreground(option->state), borderWidth));
      p->drawLine(r.topLeft(), r.bottomRight());
      p->drawLine(r.topRight(), r.bottomLeft());
      return;
    }
    default: break;
  }
  QProxyStyle::drawPrimitive(element, option, p, widget);
}

void SiliconStyle::drawControl(ControlElement element, const QStyleOption* option,
                               QPainter* p, const QWidget* widget) const
{
  if (!option || !p)
    return;
  p->save();
  const auto guard = qScopeGuard([p] { p->restore(); });

  switch (element) {
    case CE_PushButton: {
      const auto* button = qstyleoption_cast<const QStyleOptionButton*>(option);
      if (!button)
        break;
      // Resolve contents through this style so the label follows the bevel
      // when the button lifts on hover or sinks on press.
      drawControl(CE_PushButtonBevel, button, p, widget);
      auto label = *button;
      label.rect = subElementRect(SE_PushButtonContents, button, widget);
      drawControl(CE_PushButtonLabel, &label, p, widget);
      return;
    }
    case CE_RubberBand: {
      if (!qstyleoption_cast<const QStyleOptionRubberBand*>(option))
        break;
      // The diagram marquee is a translucent selection surface. QCommonStyle's
      // patterned rubber band obscures the circuit instead of tinting it.
      auto fill   = option->palette.color(QPalette::Active, QPalette::Highlight);
      auto border = fill.darker(120);
      fill.setAlpha(80);
      border.setAlpha(180);
      p->setRenderHint(QPainter::Antialiasing);
      p->setPen(QPen(border, 1));
      p->setBrush(fill);
      p->drawRoundedRect(QRectF(option->rect).adjusted(0.5, 0.5, -0.5, -0.5), 1, 1);
      return;
    }
    case CE_ShapedFrame:
      if (qobject_cast<const QAbstractScrollArea*>(widget))
        return;
      break;
    case CE_ToolBar:
    case CE_MenuBarEmptyArea:
      p->fillRect(option->rect, ThemeEngine::getColor(ColorToken::Background));
      return;
    case CE_HeaderSection:
    case CE_HeaderEmptyArea: {
      p->fillRect(option->rect, ThemeEngine::getColor(ColorToken::SurfaceRaised));
      p->fillRect(
          QRect(option->rect.left(), option->rect.bottom(), option->rect.width(), 1),
          separatorColor());
      return;
    }
    case CE_MenuItem: {
      const auto* menu = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
      if (!menu)
        break;
      p->fillRect(menu->rect, ThemeEngine::getColor(ColorToken::SurfaceRaised));
      if (menu->menuItemType != QStyleOptionMenuItem::Separator)
        panel(p, inset(menu->rect, menuItemMargin, menuItemMargin),
              menuBackground(menu->state), menuItemRadius);
      auto item = *menu;
      // Keep Fusion's text, icons, shortcuts and check indicators over our highlight.
      item.palette.setColor(QPalette::All, QPalette::Window, Qt::transparent);
      item.palette.setColor(QPalette::All, QPalette::Highlight, Qt::transparent);
      QProxyStyle::drawControl(element, &item, p, widget);
      return;
    }
    case CE_MenuBarItem: {
      const auto* menu = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
      if (!menu)
        break;
      p->fillRect(option->rect, ThemeEngine::getColor(ColorToken::Background));
      panel(p, option->rect,
            menuBackground(option->state, ThemeEngine::getColor(ColorToken::Background)),
            indicatorRadius);
      auto label = *menu;
      textPalette(label.palette, foreground(option->state));
      // Fusion also paints a bevel here; only delegate the menu's label.
      QCommonStyle::drawControl(element, &label, p, widget);
      return;
    }
    case CE_TabBarTabShape: {
      const auto* tab = qstyleoption_cast<const QStyleOptionTab*>(option);
      if (!tab)
        break;
      panel(p, option->rect, tabBackground(option->state), tabRadius);
      if (!(option->state & State_Selected))
        return;
      QRect line = option->rect;
      switch (tab->shape) {
        case QTabBar::RoundedSouth:
        case QTabBar::TriangularSouth: line.setHeight(borderWidth); break;
        case QTabBar::RoundedWest:
        case QTabBar::TriangularWest: line.setLeft(line.right() - borderWidth + 1); break;
        case QTabBar::RoundedEast:
        case QTabBar::TriangularEast: line.setWidth(borderWidth); break;
        default: line.setTop(line.bottom() - borderWidth + 1); break;
      }
      p->fillRect(line, foreground(option->state));
      return;
    }
    default: break;
  }
  QProxyStyle::drawControl(element, option, p, widget);
}

void SiliconStyle::drawComplexControl(ComplexControl             control,
                                      const QStyleOptionComplex* option, QPainter* p,
                                      const QWidget* widget) const
{
  if (!option || !p)
    return;
  p->save();
  const auto guard = qScopeGuard([p] { p->restore(); });

  switch (control) {
    case CC_ComboBox: {
      const auto* combo = qstyleoption_cast<const QStyleOptionComboBox*>(option);
      if (!combo)
        break;
      if (combo->frame)
        inputPanel(p, combo->rect);
      if (combo->subControls & SC_ComboBoxArrow) {
        QStyleOption indicator = *combo;
        indicator.rect         = subControlRect(control, combo, SC_ComboBoxArrow, widget);
        drawPrimitive(PE_IndicatorArrowDown, &indicator, p, widget);
      }
      return;
    }
    case CC_SpinBox: {
      const auto* spin = qstyleoption_cast<const QStyleOptionSpinBox*>(option);
      if (!spin)
        break;
      if (spin->frame)
        inputPanel(p, spin->rect);
      if (spin->buttonSymbols == QAbstractSpinBox::NoButtons)
        return;
      for (const auto sc : {SC_SpinBoxUp, SC_SpinBoxDown}) {
        if (!(spin->subControls & sc))
          continue;
        drawSpinButton(*this, p, *spin, sc, subControlRect(control, spin, sc, widget));
      }
      return;
    }
    case CC_ToolButton: {
      const auto* tool = qstyleoption_cast<const QStyleOptionToolButton*>(option);
      if (!tool)
        break;
      auto button = *tool;
      if (!(button.state & State_AutoRaise) || button.state & State_MouseOver)
        button.state |= State_Raised;
      QProxyStyle::drawComplexControl(control, &button, p, widget);
      return;
    }
    case CC_ScrollBar: {
      const auto* scroll = qstyleoption_cast<const QStyleOptionSlider*>(option);
      if (!scroll)
        break;
      const bool horizontal = scroll->orientation == Qt::Horizontal;
      const auto r          = scroll->rect;
      p->fillRect(r, ThemeEngine::getColor(ColorToken::Background));
      if (!(scroll->subControls & SC_ScrollBarSlider))
        return;
      const auto fill   = scrollThumbColor(subControlState(*scroll, SC_ScrollBarSlider));
      const auto handle = subControlRect(control, scroll, SC_ScrollBarSlider, widget);
      panel(p, inset(handle, horizontal ? 2 : 3, horizontal ? 3 : 2), fill,
            scrollBarRadius);
      return;
    }
    default: break;
  }
  QProxyStyle::drawComplexControl(control, option, p, widget);
}

QRect SiliconStyle::subElementRect(SubElement element, const QStyleOption* option,
                                   const QWidget* widget) const
{
  if (!option)
    return {};

  switch (element) {
    case SE_PushButtonContents: {
      return inset(buttonFaceRect(*option), buttonPaddingHorizontal + borderWidth,
                   buttonPaddingVertical + borderWidth);
    }
    case SE_LineEditContents:
      if (framelessLineEdit(option))
        return option->rect;  // Embedded combo/spin edits already have parent padding.
      return inset(option->rect, controlHorizontal + borderWidth,
                   inputVerticalPadding(*option, borderWidth) + borderWidth);
    default: return QProxyStyle::subElementRect(element, option, widget);
  }
}

QRect SiliconStyle::subControlRect(ComplexControl             control,
                                   const QStyleOptionComplex* option, SubControl sub,
                                   const QWidget* widget) const
{
  if (!option)
    return {};

  const QRect r = option->rect;
  switch (control) {
    case CC_ComboBox: {
      const auto* combo = qstyleoption_cast<const QStyleOptionComboBox*>(option);
      if (!combo)
        break;
      const auto geometry = inputGeometry(*combo, combo->frame, inputButtonWidth);
      switch (sub) {
        case SC_ComboBoxArrow: return geometry.buttons;
        case SC_ComboBoxEditField: return geometry.editField;
        case SC_ComboBoxFrame:
        case SC_ComboBoxListBoxPopup: return r;
        default: return {};
      }
    }
    case CC_SpinBox: {
      const auto* spin = qstyleoption_cast<const QStyleOptionSpinBox*>(option);
      if (!spin)
        break;
      const int buttons =
          spin->buttonSymbols == QAbstractSpinBox::NoButtons ? 0 : inputButtonWidth;
      const auto  geometry = inputGeometry(*spin, spin->frame, buttons);
      const QRect column   = geometry.buttons;
      switch (sub) {
        case SC_SpinBoxFrame: return r;
        case SC_SpinBoxEditField: return geometry.editField;
        case SC_SpinBoxUp:
          return QRect(column.x(), column.y(), buttons, column.height() / 2);
        case SC_SpinBoxDown:
          return QRect(column.x(), column.y() + column.height() / 2, buttons,
                       column.height() - column.height() / 2);
        default: return {};
      }
    }
    case CC_ScrollBar: {
      const auto* scroll = qstyleoption_cast<const QStyleOptionSlider*>(option);
      if (!scroll)
        break;
      const auto geometry = scrollGeometry(*scroll);
      switch (sub) {
        case SC_ScrollBarGroove: return geometry.groove;
        case SC_ScrollBarSlider: return geometry.slider;
        case SC_ScrollBarSubPage: return geometry.subPage;
        case SC_ScrollBarAddPage: return geometry.addPage;
        default: return {};
      }
    }
    default: break;
  }
  return QProxyStyle::subControlRect(control, option, sub, widget);
}

QStyle::SubControl SiliconStyle::hitTestComplexControl(ComplexControl             control,
                                                       const QStyleOptionComplex* option,
                                                       const QPoint&  position,
                                                       const QWidget* widget) const
{
  if (!option || !option->rect.contains(position))
    return SC_None;
  // Hit testing must use the same geometry as painting.
  const auto hit = QProxyStyle::hitTestComplexControl(control, option, position, widget);
  // Qt tests the spinbox frame before the edit field. Give the embedded edit priority
  // after Qt has ruled out the step buttons.
  if (control == CC_SpinBox && hit == SC_SpinBoxFrame
      && subControlRect(control, option, SC_SpinBoxEditField, widget).contains(position))
    return SC_SpinBoxEditField;
  return hit;
}

QSize SiliconStyle::sizeFromContents(ContentsType type, const QStyleOption* option,
                                     const QSize& size, const QWidget* widget) const
{
  switch (type) {
    case CT_CheckBox:
    case CT_RadioButton:
      return QCommonStyle::sizeFromContents(type, option, size, widget);
    case CT_MenuBarItem: return size + QSize(controlHorizontal * 2, spacingUnit * 2);
    case CT_PushButton: {
      QSize result =
          size
          + QSize(2 * (buttonPaddingHorizontal + borderWidth) + shadowOffset + buttonLift,
                  2 * (buttonPaddingVertical + borderWidth) + shadowOffset + buttonLift);
      if (const auto* button = qstyleoption_cast<const QStyleOptionButton*>(option);
          button && button->features & QStyleOptionButton::HasMenu)
        result.rwidth() += menuArrowWidth;
      return result.expandedTo(QSize(0, buttonHeight));
    }
    case CT_ToolButton: {
      QSize result =
          size
          + QSize(spacingUnit * 2 + borderWidth * 2, spacingUnit * 2 + borderWidth * 2);
      if (const auto* tool = qstyleoption_cast<const QStyleOptionToolButton*>(option);
          tool && tool->features & QStyleOptionToolButton::MenuButtonPopup)
        result.rwidth() += menuArrowWidth;
      return result;
    }
    case CT_LineEdit:
    case CT_ComboBox:
    case CT_SpinBox: {
      // A spinbox uses its embedded edit's height as content size.
      if (type == CT_LineEdit && framelessLineEdit(option))
        return size;
      QSize result = size
                     + QSize(2 * (controlHorizontal + borderWidth),
                             2 * (controlVertical + borderWidth));
      if (type != CT_LineEdit)
        result.rwidth() += inputButtonWidth;
      return result.expandedTo(QSize(0, controlHeight));
    }
    case CT_MenuItem: {
      const auto  native = QProxyStyle::sizeFromContents(type, option, size, widget);
      const auto* menu   = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
      const bool  separator =
          menu && menu->menuItemType == QStyleOptionMenuItem::Separator;
      return native
             + QSize(spacingUnit * 2, separator ? spacingUnit * 2 : menuVertical * 2);
    }
    case CT_TabBarTab: {
      QSize result = QProxyStyle::sizeFromContents(type, option, size, widget);
      if (const auto* tab = qstyleoption_cast<const QStyleOptionTab*>(option);
          tab && verticalTab(tab->shape))
        result.setWidth(std::max(result.width(), tabHeight));
      else
        result.setHeight(std::max(result.height(), tabHeight));
      return result;
    }
    default: break;
  }
  return QProxyStyle::sizeFromContents(type, option, size, widget);
}

int SiliconStyle::pixelMetric(PixelMetric metric, const QStyleOption* option,
                              const QWidget* widget) const
{
  switch (metric) {
    case PM_DefaultFrameWidth:
      if (widget && widget->windowType() == Qt::Popup
          && qobject_cast<const QComboBox*>(widget->parentWidget()))
        return borderWidth + shadowOffset + menuMargin;
      if (qobject_cast<const QAbstractScrollArea*>(widget)
          || qobject_cast<const QTabWidget*>(widget))
        return 0;
      return borderWidth;
    case PM_MenuBarHMargin: return spacingUnit;
    case PM_MenuBarVMargin: return spacingUnit / 2;
    case PM_MenuBarItemSpacing: return spacingUnit;
    case PM_DockWidgetFrameWidth: return 0;
    case PM_DockWidgetTitleMargin:
    case PM_HeaderMargin: return controlVertical;
    case PM_SpinBoxFrameWidth: return borderWidth;
    case PM_ButtonMargin: return buttonPaddingVertical;
    case PM_MenuButtonIndicator: return menuArrowWidth;
    case PM_ScrollBarExtent: return scrollBarThickness;
    case PM_ScrollBarSliderMin: return scrollBarMinimum;
    case PM_IndicatorWidth:
    case PM_IndicatorHeight:
    case PM_ExclusiveIndicatorWidth:
    case PM_ExclusiveIndicatorHeight: return indicatorSize;
    case PM_FocusFrameHMargin:
    case PM_FocusFrameVMargin: return 0;
    case PM_MenuPanelWidth: return borderWidth + shadowOffset;
    case PM_MenuHMargin:
    case PM_MenuVMargin: return menuMargin;
    case PM_TabBarTabVSpace: return spacingUnit * 2;
    case PM_TabBarTabShiftVertical: return 0;
    case PM_TabCloseIndicatorWidth:
    case PM_TabCloseIndicatorHeight: return iconSize;
    case PM_ToolBarFrameWidth: return 1;
    case PM_ToolBarItemSpacing:
    case PM_ToolBarItemMargin: return toolbarSpacing;
    case PM_LayoutLeftMargin:
    case PM_LayoutTopMargin:
    case PM_LayoutRightMargin:
    case PM_LayoutBottomMargin: return contentMargin;
    default: return QProxyStyle::pixelMetric(metric, option, widget);
  }
}

int SiliconStyle::styleHint(StyleHint hint, const QStyleOption* option,
                            const QWidget* widget, QStyleHintReturn* result) const
{
  if (hint == SH_ComboBox_Popup)
    return true;
  if (hint == SH_RubberBand_Mask || hint == SH_UnderlineShortcut
      || hint == SH_Widget_Animate)
    return false;
  return QProxyStyle::styleHint(hint, option, widget, result);
}

QIcon SiliconStyle::standardIcon(StandardPixmap pixmap, const QStyleOption* option,
                                 const QWidget* widget) const
{
  switch (pixmap) {
    case SP_TitleBarCloseButton:
    case SP_DockWidgetCloseButton:
    case SP_LineEditClearButton:
    case SP_DialogCloseButton: return Icon("xmark");
    case SP_DialogOkButton:
    case SP_DialogApplyButton: return Icon("check");
    case SP_MessageBoxInformation: return Icon("info");
    case SP_MessageBoxCritical:
    case SP_MessageBoxWarning: return Icon("xmark-circle");
    default: return QProxyStyle::standardIcon(pixmap, option, widget);
  }
}

void SiliconStyle::polish(QWidget* widget)
{
  if (!widget)
    return;
  if (!widget->property(hoverProperty).isValid())
    widget->setProperty(hoverProperty, widget->testAttribute(Qt::WA_Hover));
  QProxyStyle::polish(widget);
  // Fusion enables hover on the remaining interactive controls.
  if (qobject_cast<QLineEdit*>(widget) || qobject_cast<QTabBar*>(widget))
    widget->setAttribute(Qt::WA_Hover);
  // Explicit application fonts win; repeated polishing must not replace our backup.
  if (widget->testAttribute(Qt::WA_SetFont)
      || widget->property(installedFontProperty).isValid())
    return;
  QFont font;
  if (qobject_cast<QLabel*>(widget)
      && (widget->property("heading").toBool() || widget->objectName() == "titleLabel")) {
    font = QFont("Chango");
    font.setPixelSize(24);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1);
  } else if (hasClass(widget, "mono")) {
    font = QApplication::font("QPlainTextEdit");
  } else {
    return;
  }
  widget->setProperty(originalFontProperty, widget->font());
  widget->setProperty(installedFontProperty, font);
  widget->setFont(font);
}

void SiliconStyle::unpolish(QWidget* widget)
{
  if (!widget)
    return;
  const auto installed = widget->property(installedFontProperty);
  if (installed.isValid()) {
    const auto font    = installed.value<QFont>();
    const auto current = widget->font();
    // Only undo our assignment. Preserve subsequent application changes, including zoom.
    if (current.resolveMask() == font.resolveMask() && current == font.resolve(current)) {
      widget->setFont(widget->property(originalFontProperty).value<QFont>());
      widget->setAttribute(Qt::WA_SetFont, false);
    }
    widget->setProperty(originalFontProperty, {});
    widget->setProperty(installedFontProperty, {});
  }
  QProxyStyle::unpolish(widget);
  const auto hover = widget->property(hoverProperty);
  if (hover.isValid()) {
    widget->setAttribute(Qt::WA_Hover, hover.toBool());
    widget->setProperty(hoverProperty, {});
  }
}

}  // namespace SILICON::ui
