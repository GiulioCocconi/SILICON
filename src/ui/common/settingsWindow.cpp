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

#include "settingsWindow.hpp"

#include <limits>
#include <utility>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryFile>
#include <QVBoxLayout>

#include <core/simulator.hpp>
#include <ui/common/fileDialogUtils.hpp>
#include <ui/common/theme.hpp>

namespace {
void copySettings(QSettings& source, QSettings& target)
{
  for (const QString& key : source.allKeys())
    target.setValue(key, source.value(key));
}

void applySettingHelp(QWidget* widget, const SiliconSetting::Definition& setting)
{
  if (!setting.help.has_value())
    return;

  widget->setToolTip(*setting.help);
  widget->setStatusTip(*setting.help);
}

void addSettingRow(QFormLayout* layout, const QString& label, QWidget* editor,
                   const SiliconSetting::Definition& setting)
{
  const auto labelWidget = new QLabel(label, layout->parentWidget());
  labelWidget->setBuddy(editor);

  applySettingHelp(labelWidget, setting);
  applySettingHelp(editor, setting);

  layout->addRow(labelWidget, editor);
}
}  // namespace

SettingsWindow::SettingsWindow(const QString& appName, QVector<ShortcutSetting> shortcuts,
                               QWidget* parent)
  : QDialog(parent), settings(appName, this), shortcuts(std::move(shortcuts))
{
  setWindowTitle(tr("Settings"));
  resize(560, 520);

  themeCombo = new QComboBox(this);
  themeCombo->addItem(tr("Light"), SiliconSetting::LightTheme);
  themeCombo->addItem(tr("Dark"), SiliconSetting::DarkTheme);

  maxSimulationStepsSpinBox = new QSpinBox(this);
  maxSimulationStepsSpinBox->setRange(1, std::numeric_limits<int>::max());

  maxTransitionsPerDeltaCycleSpinBox = new QSpinBox(this);
  maxTransitionsPerDeltaCycleSpinBox->setRange(1, std::numeric_limits<int>::max());

  const auto generalGroup  = new QGroupBox(tr("General"), this);
  const auto generalLayout = new QFormLayout(generalGroup);
  addSettingRow(generalLayout, tr("Theme"), themeCombo, SiliconSetting::Theme);
  addSettingRow(generalLayout, tr("Max simulation steps"), maxSimulationStepsSpinBox,
                SiliconSetting::MaxSimulationSteps);
  addSettingRow(generalLayout, tr("Max transitions per delta cycle"),
                maxTransitionsPerDeltaCycleSpinBox,
                SiliconSetting::MaxTransitionsPerDeltaCycle);

  keybindingsLayout = new QFormLayout();
  for (const ShortcutSetting& shortcut : this->shortcuts)
    addShortcutEditor(shortcut);

  const auto keybindingsContents = new QWidget(this);
  keybindingsContents->setLayout(keybindingsLayout);

  const auto keybindingsScrollArea = new QScrollArea(this);
  keybindingsScrollArea->setWidgetResizable(true);
  keybindingsScrollArea->setFrameShape(QFrame::NoFrame);
  keybindingsScrollArea->setWidget(keybindingsContents);

  const auto keybindingsGroup  = new QGroupBox(tr("Keybindings"), this);
  const auto keybindingsBoxLay = new QVBoxLayout(keybindingsGroup);
  keybindingsBoxLay->addWidget(keybindingsScrollArea);

  buttonBox =
      new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  restoreDefaultsButton =
      buttonBox->addButton(tr("Restore Defaults"), QDialogButtonBox::ResetRole);

  importButton = new QPushButton(tr("Import..."), this);
  exportButton = new QPushButton(tr("Export..."), this);

  const auto transferLayout = new QHBoxLayout();
  transferLayout->addWidget(importButton);
  transferLayout->addWidget(exportButton);
  transferLayout->addStretch();

  const auto mainLayout = new QVBoxLayout(this);
  mainLayout->addWidget(generalGroup);
  mainLayout->addWidget(keybindingsGroup, 1);
  mainLayout->addLayout(transferLayout);
  mainLayout->addWidget(buttonBox);

  connect(importButton, &QPushButton::clicked, this, &SettingsWindow::importSettings);
  connect(exportButton, &QPushButton::clicked, this, &SettingsWindow::exportSettings);
  connect(restoreDefaultsButton, &QPushButton::clicked, this,
          &SettingsWindow::restoreDefaults);
  connect(buttonBox->button(QDialogButtonBox::Cancel), &QPushButton::clicked, this,
          &QDialog::reject);
  connect(buttonBox->button(QDialogButtonBox::Save), &QPushButton::clicked, this, [this] {
    if (saveSettings())
      accept();
  });

  loadSettings();
}

void SettingsWindow::loadSettings()
{
  settings.sync();

  const CommonSettingsValues values     = readCommonSettings(settings);
  const int                  themeIndex = themeCombo->findData(values.theme);
  themeCombo->setCurrentIndex(
      themeIndex >= 0 ? themeIndex
                      : themeCombo->findData(SiliconSetting::Theme.defaultValue));

  maxSimulationStepsSpinBox->setValue(values.maxSimulationSteps);
  maxTransitionsPerDeltaCycleSpinBox->setValue(values.maxTransitionsPerDeltaCycle);

  for (int i = 0; i < shortcuts.size(); ++i) {
    shortcutEditors[i]->setKeySequence(
        SiliconSetting::value(settings, shortcuts[i].setting).value<QKeySequence>());
  }
}

void SettingsWindow::restoreDefaults()
{
  const CommonSettingsValues defaults;
  const int                  themeIndex = themeCombo->findData(defaults.theme);
  themeCombo->setCurrentIndex(
      themeIndex >= 0 ? themeIndex
                      : themeCombo->findData(SiliconSetting::Theme.defaultValue));
  maxSimulationStepsSpinBox->setValue(defaults.maxSimulationSteps);
  maxTransitionsPerDeltaCycleSpinBox->setValue(defaults.maxTransitionsPerDeltaCycle);

  for (int i = 0; i < shortcuts.size(); ++i)
    shortcutEditors[i]->setKeySequence(
        shortcuts[i].setting.defaultValue.value<QKeySequence>());
}

bool SettingsWindow::saveSettings()
{
  writeVisibleSettings(settings);
  settings.sync();
  if (settings.status() != QSettings::NoError) {
    QMessageBox::warning(this, tr("Settings Error"),
                         tr("Could not write the settings file."));
    return false;
  }

  applySettings();
  return true;
}

bool SettingsWindow::importSettingsStore(QSettings& importedSettings)
{
  importedSettings.sync();
  if (importedSettings.status() != QSettings::NoError) {
    QMessageBox::warning(this, tr("Import Settings"),
                         tr("Could not read the selected settings file."));
    return false;
  }

  settings.clear();
  copySettings(importedSettings, settings);
  settings.sync();
  if (settings.status() != QSettings::NoError) {
    QMessageBox::warning(this, tr("Import Settings"),
                         tr("Could not save the imported settings."));
    return false;
  }

  loadSettings();
  applySettings();
  return true;
}

QByteArray SettingsWindow::exportSettingsContent()
{
  QTemporaryFile exportedFile(this);
  if (!exportedFile.open()) {
    QMessageBox::warning(this, tr("Export Settings"),
                         tr("Could not write the settings file."));
    return {};
  }

  const QString exportedFileName = exportedFile.fileName();
  exportedFile.close();

  QSettings exportedSettings(exportedFileName, SiliconSettings::format());
  exportedSettings.clear();
  copySettings(settings, exportedSettings);
  writeVisibleSettings(exportedSettings);
  exportedSettings.sync();

  if (exportedSettings.status() != QSettings::NoError) {
    QMessageBox::warning(this, tr("Export Settings"),
                         tr("Could not write the settings file."));
    return {};
  }

  QFile file(exportedFileName);
  if (!file.open(QIODevice::ReadOnly)) {
    QMessageBox::warning(this, tr("Export Settings"),
                         tr("Could not read back the exported settings file."));
    return {};
  }
  return file.readAll();
}

void SettingsWindow::importSettings()
{
  SiliconFileDialog::openFileContent(
      this, tr("Import Settings"), tr("TOML Settings (*.toml);;All Files (*)"),
      [this](const QString& fileName, const QByteArray& fileContent) {
        QTemporaryFile importedFile(this);
        if (!importedFile.open()
            || importedFile.write(fileContent) != fileContent.size()) {
          QMessageBox::warning(this, tr("Import Settings"),
                               tr("Could not read the selected settings file."));
          return;
        }
        importedFile.flush();
        const QString importedFileName = importedFile.fileName();
        importedFile.close();

        QSettings importedSettings(importedFileName, SiliconSettings::format());
        importSettingsStore(importedSettings);
      });
}

void SettingsWindow::exportSettings()
{
  const QByteArray content = exportSettingsContent();
  if (!content.isEmpty())
    SiliconFileDialog::saveFileContent(
        this, tr("Export Settings"), QStringLiteral("silicon-settings.toml"),
        tr("TOML Settings (*.toml);;All Files (*)"), content);
}

void SettingsWindow::addShortcutEditor(const ShortcutSetting& shortcut)
{
  const auto editor = new QKeySequenceEdit(this);
  shortcutEditors.append(editor);
  keybindingsLayout->addRow(shortcut.label, editor);
}

void SettingsWindow::applySettings()
{
  Simulator::setMaxSimulationSteps(
      static_cast<uint64_t>(maxSimulationStepsSpinBox->value()));
  Simulator::setMaxTransitionsPerDeltaCycle(maxTransitionsPerDeltaCycleSpinBox->value());

  ThemeEngine::apply(*qApp, themeModeFromText(themeCombo->currentData().toString()));

  for (int i = 0; i < shortcuts.size(); ++i) {
    if (shortcuts[i].action)
      shortcuts[i].action->setShortcut(shortcutEditors[i]->keySequence());
  }
}

void SettingsWindow::writeVisibleSettings(QSettings& target) const
{
  target.setValue(SiliconSetting::Theme.name, themeCombo->currentData().toString());
  target.setValue(SiliconSetting::MaxSimulationSteps.name,
                  maxSimulationStepsSpinBox->value());
  target.setValue(SiliconSetting::MaxTransitionsPerDeltaCycle.name,
                  maxTransitionsPerDeltaCycleSpinBox->value());

  for (int i = 0; i < shortcuts.size(); ++i)
    target.setValue(shortcuts[i].setting.name,
                    QVariant::fromValue(shortcutEditors[i]->keySequence()));
}
