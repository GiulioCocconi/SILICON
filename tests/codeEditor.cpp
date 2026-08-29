/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#include <memory>

#include <QApplication>
#include <QPalette>
#include <QTest>
#include <QTextBlock>
#include <QTextLayout>

#include <gtest/gtest.h>

#include <core/codeFile.hpp>
#include <ui/common/codeEditor.hpp>

QApplication& application()
{
  static int          argc   = 1;
  static char         name[] = "code-editor-tests";
  static char*        argv[] = {name, nullptr};
  static QApplication app(argc, argv);
  return app;
}

TEST(CodeEditorTest, UsesKdeVerilogHighlightingAndKeywords)
{
  (void)application();
  SILICON::ui::CodeEditor editor;
  editor.setFileType(SILICON::project::CodeFileType::Verilog);
  editor.setPlainText("module test; endmodule");
  QCoreApplication::processEvents();

  const auto candidates = editor.completionCandidates();
  EXPECT_TRUE(candidates.contains("module"));
  EXPECT_TRUE(candidates.contains("endmodule"));
  auto sorted = candidates;
  sorted.sort(Qt::CaseSensitive);
  EXPECT_EQ(candidates, sorted);
  EXPECT_FALSE(editor.document()->firstBlock().layout()->formats().empty());
}

TEST(CodeEditorTest, RefreshesThemeWhenPaletteChanges)
{
  (void)application();
  SILICON::ui::CodeEditor editor;
  editor.setFileType(SILICON::project::CodeFileType::Verilog);
  QPalette light;
  light.setColor(QPalette::Base, Qt::white);
  light.setColor(QPalette::Text, Qt::black);
  editor.setPalette(light);
  const auto lightTheme = editor.highlightingThemeName();

  QPalette dark;
  dark.setColor(QPalette::Base, Qt::black);
  dark.setColor(QPalette::Text, Qt::white);
  editor.setPalette(dark);
  EXPECT_NE(editor.highlightingThemeName(), lightTheme);
}

TEST(CodeEditorTest, OpensAutomaticAndExplicitCompletionAndReplacesPrefix)
{
  (void)application();
  SILICON::ui::CodeEditor editor;
  editor.setFileType(SILICON::project::CodeFileType::Verilog);
  editor.resize(500, 200);
  editor.show();
  editor.setFocus();

  QTest::keyClicks(&editor, "mo");
  QCoreApplication::processEvents();
  EXPECT_TRUE(editor.isCompletionPopupVisible());

  QTest::keyClick(&editor, Qt::Key_Left);
  QCoreApplication::processEvents();
  EXPECT_FALSE(editor.isCompletionPopupVisible());

  editor.setPlainText("module test;\n  instance.mod");
  auto cursor = editor.textCursor();
  cursor.movePosition(QTextCursor::End);
  editor.setTextCursor(cursor);
  EXPECT_EQ(editor.completionPrefix(), "mod");
  QTest::keyClick(&editor, Qt::Key_Space, Qt::ControlModifier);
  QCoreApplication::processEvents();
  EXPECT_TRUE(editor.isCompletionPopupVisible());
  QTest::keyClick(&editor, Qt::Key_Return);
  EXPECT_TRUE(editor.toPlainText().endsWith("instance.module"));
}
