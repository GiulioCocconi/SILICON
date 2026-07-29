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

#include <filesystem>
#include <optional>

#include <QMetaType>
#include <QSettings>
#include <QString>
#include <QVariant>

#include <ui/common/theme.hpp>

Q_DECLARE_METATYPE(std::filesystem::path)


namespace SILICON {
namespace ui {

namespace settings {
inline constexpr auto LightTheme = "light";
inline constexpr auto DarkTheme  = "dark";

/**
 * @brief Describes a persisted setting entry.
 */
struct Definition {
  QString                name;
  QVariant               defaultValue;
  std::optional<QString> help = std::nullopt;
};

inline const Definition Theme = {
    .name         = QStringLiteral("ui/theme"),
    .defaultValue = QString::fromLatin1(LightTheme),
};

inline const Definition MaxSimulationSteps = {
    .name         = QStringLiteral("simulation/maxSteps"),
    .defaultValue = 100000,
    .help = QStringLiteral("Maximum number of simulation steps before stopping a run."),
};

inline const Definition MaxTransitionsPerDeltaCycle = {
    .name         = QStringLiteral("simulation/maxTransitionsPerDeltaCycle"),
    .defaultValue = 1000,
    .help         = QStringLiteral(
        "Maximum number of signal transitions allowed within one delta cycle."),
};

/**
 * @brief Reads a setting value, falling back to its declared default when unset.
 * @param settings Source settings object.
 * @param setting Setting definition to read.
 * @return The persisted value or the definition default.
 */
[[nodiscard]] QVariant value(const QSettings& settings, const Definition& setting);
}  // namespace settings

/**
 * @brief Snapshot of the common application settings used at runtime.
 */
struct CommonSettingsValues {
  QString theme              = SILICON::ui::settings::Theme.defaultValue.toString();
  int     maxSimulationSteps = SILICON::ui::settings::MaxSimulationSteps.defaultValue.toInt();
  int     maxTransitionsPerDeltaCycle =
      SILICON::ui::settings::MaxTransitionsPerDeltaCycle.defaultValue.toInt();
};

/**
 * @brief QSettings wrapper configured to read and write SILICON TOML settings files.
 */
class SiliconSettings : public QSettings {
public:
  /**
   * @brief Opens the user-scoped settings store for a SILICON application.
   * @param appName Application-specific settings namespace.
   * @param parent QObject parent.
   */
  explicit SiliconSettings(const QString& appName, QObject* parent = nullptr);

  /**
   * @brief Returns the registered TOML-backed settings format.
   * @return QSettings format identifier for SILICON settings files.
   */
  [[nodiscard]] static QSettings::Format format();
};

/**
 * @brief Reads the common UI and simulation settings into a typed snapshot.
 * @param settings Source settings object.
 * @return Current common settings values with defaults applied.
 */
[[nodiscard]] CommonSettingsValues readCommonSettings(const QSettings& settings);

/**
 * @brief Converts a persisted theme string into a theme engine mode.
 * @param value Persisted theme identifier.
 * @return Matching theme mode, defaulting to light for unknown values.
 */
[[nodiscard]] SILICON::ui::theme::Mode themeModeFromText(const QString& value);

}  // namespace ui
}  // namespace SILICON
