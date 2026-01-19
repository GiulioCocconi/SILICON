/*
 Copyright (c) 2025-2026. Giulio Cocconi
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

#pragma once

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringBuilder>
#include <QVBoxLayout>

#include <ui/common/icons.hpp>

class AboutDialog : public QDialog {
  Q_OBJECT

public:
  explicit AboutDialog(const QString& title = "SILICON", QWidget* parent = nullptr);

private:
  void loadLicenses();

  QLabel* explainatoryText = new QLabel(
      QString("This software is a part of the SILICON suite (v. %1), developed by "
              "Giulio Cocconi and released under the GNU General Public License "
              "v. 3.0.\n\n It makes use of the following licensed resources:")
          .arg(SILICON_VERSION),
      this);

  QVBoxLayout* mainLayout = new QVBoxLayout(this);
  QHBoxLayout* topLayout  = new QHBoxLayout();

  QLabel*         titleLabel;
  QLabel*         iconLabel;
  QPlainTextEdit* licenseTextEdit;
  QPushButton*    closeButton;
  QString         licenseText = "";
};
