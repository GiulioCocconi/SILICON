/*
 Copyright (c) 2025. Giulio Cocconi
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

#include "aboutDialog.hpp"

#include <stdexcept>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMainWindow>
#include <QPushButton>
#include <QStringBuilder>


namespace SILICON {
namespace ui {

AboutDialog::AboutDialog(const QString& title, QWidget* parent) : QDialog(parent)
{
  setWindowTitle(QString("About %1").arg(title));
  setFixedSize(600, 500);

  const auto icon = QApplication::windowIcon();
  if (icon.isNull())
    throw std::logic_error("AboutDialog: application window icon is not set");

  titleLabel = new QLabel(title, this);
  titleLabel->setObjectName("titleLabel");

  iconLabel = new QLabel(this);
  iconLabel->setPixmap(icon.pixmap(QSize(128, 128)));
  iconLabel->setScaledContents(true);

  topLayout->setSpacing(20);
  topLayout->addWidget(iconLabel, 20);
  topLayout->addWidget(titleLabel, 80);

  mainLayout->addLayout(topLayout);

  explainatoryText->setWordWrap(true);

  mainLayout->addWidget(explainatoryText);

  licenseTextEdit = new QPlainTextEdit(this);
  licenseTextEdit->setReadOnly(true);
  licenseTextEdit->setFont(QFont("NovaMono"));

  closeButton = new QPushButton(tr("Close"), this);
  connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);

  mainLayout->addWidget(licenseTextEdit);
  mainLayout->addWidget(closeButton);

  // Load the licenses from the Qt resources
  loadLicenses();
}

void AboutDialog::loadLicenses()
{
  if (this->licenseText.isEmpty()) {
    const QDir dir(":/licenses/");

    QStringList fileList = dir.entryList(QDir::Files);

    if (fileList.isEmpty()) {
      licenseTextEdit->setPlainText("Error: License resource directory is empty.");
    }

    for (const QString& fileName : fileList) {
      QString filePath = dir.filePath(fileName);

      if (auto licenseFile = QFile(filePath);
          licenseFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // Add a header for each license to separate them clearly
        this->licenseText.append(
            "=================================================================\n");
        this->licenseText.append("LICENSE: ");
        this->licenseText.append(fileName);
        this->licenseText.append(
            "\n=================================================================\n\n");

        // Append the content of the license file
        QTextStream in(&licenseFile);
        this->licenseText.append(in.readAll());

        // Add some newlines to separate from the next license
        this->licenseText.append("\n\n\n");

        licenseFile.close();
      }

      if (this->licenseText.isEmpty())
        throw std::logic_error("loadLicenses: license text is empty after reading file");

      licenseTextEdit->setPlainText(this->licenseText);
    }
  }
}

}  // namespace ui
}  // namespace SILICON
