/*
  Copyright (c) 2026. Giulio Cocconi

  SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <QPointF>
#include <QRectF>
#include <array>
#include <core/component.hpp>

namespace SILICON::ui {

enum class ResizeHandle {
  Left,
  Right,
  Top,
  Bottom,
  TopLeft,
  TopRight,
  BottomLeft,
  BottomRight
};

inline constexpr std::array AllResizeHandles{
    ResizeHandle::TopLeft,     ResizeHandle::TopRight, ResizeHandle::BottomLeft,
    ResizeHandle::BottomRight, ResizeHandle::Left,     ResizeHandle::Right,
    ResizeHandle::Top,         ResizeHandle::Bottom};

inline bool resizesLeft(ResizeHandle handle)
{
  return handle == ResizeHandle::Left || handle == ResizeHandle::TopLeft
         || handle == ResizeHandle::BottomLeft;
}

inline bool resizesRight(ResizeHandle handle)
{
  return handle == ResizeHandle::Right || handle == ResizeHandle::TopRight
         || handle == ResizeHandle::BottomRight;
}

inline bool resizesTop(ResizeHandle handle)
{
  return handle == ResizeHandle::Top || handle == ResizeHandle::TopLeft
         || handle == ResizeHandle::TopRight;
}

inline bool resizesBottom(ResizeHandle handle)
{
  return handle == ResizeHandle::Bottom || handle == ResizeHandle::BottomLeft
         || handle == ResizeHandle::BottomRight;
}

inline QPointF resizeHandlePoint(const QRectF& bounds, ResizeHandle handle)
{
  return {resizesLeft(handle)    ? bounds.left()
          : resizesRight(handle) ? bounds.right()
                                 : bounds.center().x(),
          resizesTop(handle)      ? bounds.top()
          : resizesBottom(handle) ? bounds.bottom()
                                  : bounds.center().y()};
}

/** Fixed anchor: opposite corner, opposite vertical midpoint, or top of the opposite
 * side. */
inline QPointF resizeAnchor(const QRectF& bounds, ResizeHandle handle)
{
  return {resizesLeft(handle)    ? bounds.right()
          : resizesRight(handle) ? bounds.left()
                                 : bounds.center().x(),
          resizesTop(handle) ? bounds.bottom() : bounds.top()};
}

struct ComponentResizeState {
  QPointF           position;
  core::PropertyMap properties;

  bool operator==(const ComponentResizeState&) const = default;
};

}  // namespace SILICON::ui
