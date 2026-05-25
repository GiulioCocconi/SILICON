/*
 * Qt geometry adapters for libavoid.
 *
 * This header is intentionally separate from libavoid.h so non-Qt consumers
 * of the vendored library do not inherit a Qt dependency.
 */

#ifndef AVOID_QTGEOMTYPES_H
#define AVOID_QTGEOMTYPES_H

#include <QPointF>
#include <QPolygonF>
#include <QRectF>

#include "libavoid/geomtypes.h"

namespace Avoid
{

inline Point pointFromQPointF(const QPointF& point)
{
    return Point(point.x(), point.y());
}

inline QPointF toQPointF(const Point& point)
{
    return QPointF(point.x, point.y);
}

inline Rectangle rectangleFromQRectF(const QRectF& rect)
{
    const QRectF normalized = rect.normalized();
    return Rectangle(pointFromQPointF(normalized.topLeft()),
            pointFromQPointF(normalized.bottomRight()));
}

inline QRectF toQRectF(const Box& box)
{
    return QRectF(toQPointF(box.min), toQPointF(box.max)).normalized();
}

inline Polygon polygonFromQPolygonF(const QPolygonF& polygon)
{
    Polygon avoidPolygon(static_cast<int>(polygon.size()));

    for (qsizetype i = 0; i < polygon.size(); ++i)
    {
        avoidPolygon.setPoint(static_cast<size_t>(i), pointFromQPointF(polygon.at(i)));
    }

    return avoidPolygon;
}

inline QPolygonF toQPolygonF(const PolygonInterface& polygon)
{
    QPolygonF qtPolygon;
    qtPolygon.reserve(static_cast<qsizetype>(polygon.size()));

    for (size_t i = 0; i < polygon.size(); ++i)
    {
        qtPolygon.append(toQPointF(polygon.at(i)));
    }

    return qtPolygon;
}

}

#endif
