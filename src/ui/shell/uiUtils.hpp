/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */

#pragma once

#include <QIcon>
#include <QObject>
#include <QString>

#include <core/projectDocument.hpp>
#include <ui/circuit/diagram/scene/diagramScene.hpp>

class QAction;

namespace SILICON::ui {

QString interactionModeName(InteractionMode mode);
QIcon categoryIcon(SILICON::project::DocumentType type);
QAction* makeAction(QObject* parent, const QIcon& icon, const QString& text,
                    const QString& statusTip = {});
QAction* makeAction(QObject* parent, const QString& text,
                    const QString& statusTip = {});

inline QString documentTypeName(SILICON::project::DocumentType type)
{
  if (SILICON::project::categoryOf(type)
      == SILICON::project::DocumentCategory::Architecture)
    return QObject::tr("Architecture File");
  switch (type) {
    case SILICON::project::DocumentType::Circuit: return QObject::tr("Circuit");
    case SILICON::project::DocumentType::Verilog: return QObject::tr("Verilog File");
    case SILICON::project::DocumentType::RawBinary: return QObject::tr("Binary File");
    default: return {};
  }
}

}  // namespace SILICON::ui
