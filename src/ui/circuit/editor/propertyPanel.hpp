/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */

#pragma once

#include <functional>
#include <optional>

#include <QObject>
#include <QSpinBox>

#include <core/serialization/projectFile.hpp>

class QDockWidget;
class QUndoStack;

namespace SILICON::ui {

class DiagramScene;
class ProjectTree;

/** Rebuilds the property dock from the current scene or project selection. */
class PropertyPanel : public QObject {
public:
  PropertyPanel(QDockWidget* dock, DiagramScene* scene, ProjectTree* tree,
                QUndoStack*                                   history,
                std::optional<SILICON::project::ProjectInfo>& projectInfo,
                const QString& fileName, std::function<void()> rebuildTree);

  void refresh();

private:
  QDockWidget*                                  propertyDock;
  DiagramScene*                                 diagramScene;
  ProjectTree*                                  projectTree;
  QUndoStack*                                   undoStack;
  std::optional<SILICON::project::ProjectInfo>& currentProjectInfo;
  const QString&                                currentFileName;
  std::function<void()>                         rebuildProjectTree;
};

class PropertySpinBox : public QSpinBox {
  Q_OBJECT

public:
  explicit PropertySpinBox(QWidget* parent = nullptr);
  void               setMixed(bool mixed, const QString& placeholder = QString());
  [[nodiscard]] bool isMixed() const;

protected:
  [[nodiscard]] QString textFromValue(int val) const override;

private:
  bool m_isMixed = false;
};

}  // namespace SILICON::ui
