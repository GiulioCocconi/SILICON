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

#pragma once

#include <QDialog>
#include <QVector>

#include <ui/common/siliconSettings.hpp>

class QAction;
class QByteArray;
class QComboBox;
class QDialogButtonBox;
class QFormLayout;
class QKeySequenceEdit;
class QPushButton;
class QSettings;
class QSpinBox;


namespace SILICON {
namespace ui {

/**
 * @brief Shortcut row shown in the settings dialog.
 */
struct ShortcutSetting {
  SILICON::ui::settings::Definition setting;
  QString                    label;
  QAction*                   action;
};

/**
 * @brief Dialog for editing persisted application settings and shortcuts.
 */
class SettingsWindow : public QDialog {
  Q_OBJECT

public:
  /**
   * @brief Builds the settings dialog for one application namespace.
   * @param appName Application-specific settings namespace.
   * @param shortcuts Shortcut definitions exposed in the keybindings section.
   * @param parent Parent widget.
   */
  explicit SettingsWindow(const QString& appName, QVector<ShortcutSetting> shortcuts,
                          QWidget* parent = nullptr);

private:
  /** @brief Loads persisted settings into the visible editors. */
  void loadSettings();
  /** @brief Writes the current editor state to disk and applies it to the app. */
  bool saveSettings();
  /** @brief Imports settings from an external TOML file into the active store. */
  void importSettings();
  /** @brief Exports the current settings to an external TOML file. */
  void exportSettings();
  /** @brief Applies settings from another settings store. */
  bool importSettingsStore(QSettings& importedSettings);
  /** @brief Serializes the visible settings to TOML bytes. */
  QByteArray exportSettingsContent();
  /** @brief Resets the visible editors to their declared default values. */
  void restoreDefaults();
  /** @brief Adds one key sequence editor row for a shortcut setting. */
  void addShortcutEditor(const ShortcutSetting& shortcut);
  /** @brief Applies the visible settings to the running application state. */
  void applySettings();
  /** @brief Writes the currently visible values to a target settings store. */
  void writeVisibleSettings(QSettings& target) const;

  SiliconSettings settings;

  QVector<ShortcutSetting>   shortcuts;
  QVector<QKeySequenceEdit*> shortcutEditors;

  QComboBox*        themeCombo;
  QSpinBox*         maxSimulationStepsSpinBox;
  QSpinBox*         maxTransitionsPerDeltaCycleSpinBox;
  QFormLayout*      keybindingsLayout;
  QPushButton*      importButton;
  QPushButton*      exportButton;
  QPushButton*      restoreDefaultsButton;
  QDialogButtonBox* buttonBox;
};

}  // namespace ui
}  // namespace SILICON
