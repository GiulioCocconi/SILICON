/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */

#pragma once

#include <functional>

#include <QObject>

class QByteArray;
class QUndoStack;
class QWidget;
class QString;

namespace SILICON::ui {

class EditorWorkspace;
class ProjectDocumentController;
struct ProjectSession;

/** Handles project archive persistence and unsaved change prompts. */
class ProjectFileController : public QObject {
  Q_OBJECT

public:
  ProjectFileController(ProjectSession& session, EditorWorkspace& workspace,
                        ProjectDocumentController& documents, QUndoStack& undoStack,
                        QWidget* dialogParent);

  void newFile();
  void open();
  bool save();
  void confirmSaveIfDirty(std::function<void()> continuation);
  void resetProjectState();
  void setFileName(const QString& fileName);

signals:
  void projectChanged();

private:
  void loadProjectContent(const QString& fileName, const QByteArray& fileContent);

  ProjectSession&            session_;
  EditorWorkspace&           workspace_;
  ProjectDocumentController& documents_;
  QUndoStack&                undoStack_;
  QWidget*                   dialogParent_;
};

}  // namespace SILICON::ui
