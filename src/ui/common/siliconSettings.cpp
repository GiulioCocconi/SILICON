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

#include "siliconSettings.hpp"

#include <QIODevice>
#include <QKeySequence>
#include <QMetaType>

#include <filesystem>
#include <optional>
#include <sstream>
#include <string_view>

#include <toml++/toml.hpp>


namespace SILICON {
namespace ui {

namespace {
constexpr std::string_view typedValueTypeKey  = "__type";
constexpr std::string_view typedValueValueKey = "value";
constexpr std::string_view pathTypeName       = "path";
constexpr std::string_view shortcutTypeName   = "key_sequence";

QString keyToQString(const toml::key& key)
{
  const std::string_view keyName = key.str();
  return QString::fromUtf8(keyName.data(), static_cast<qsizetype>(keyName.size()));
}

bool isTypedValueTable(const toml::table& table)
{
  const auto* typeNode  = table.get(typedValueTypeKey);
  const auto* valueNode = table.get(typedValueValueKey);

  return table.size() == 2 && typeNode != nullptr && valueNode != nullptr
         && typeNode->is_string();
}

std::optional<QVariant> decodeTypedValueTable(const toml::table& table)
{
  const auto* typeNode  = table.get_as<std::string>(typedValueTypeKey);
  const auto* valueNode = table.get_as<std::string>(typedValueValueKey);
  if (typeNode == nullptr || valueNode == nullptr) {
    return std::nullopt;
  }

  if (*typeNode == pathTypeName) {
    return QVariant::fromValue(std::filesystem::path(valueNode->get()));
  }

  if (*typeNode == shortcutTypeName) {
    return QVariant::fromValue(QKeySequence(QString::fromStdString(valueNode->get()),
                                            QKeySequence::PortableText));
  }

  return std::nullopt;
}

std::optional<QVariant> decodeTomlValue(const toml::node& node)
{
  if (const auto* stringNode = node.as_string()) {
    return QString::fromStdString(stringNode->get());
  }

  if (const auto* boolNode = node.as_boolean()) {
    return boolNode->get();
  }

  if (const auto* intNode = node.as_integer()) {
    return QVariant::fromValue(static_cast<int>(intNode->get()));
  }

  if (const auto* tableNode = node.as_table()) {
    if (isTypedValueTable(*tableNode)) {
      return decodeTypedValueTable(*tableNode);
    }
  }

  return std::nullopt;
}

// QSettings stores groups as slash-delimited flat keys, while TOML stores them as
// nested tables. Reading expands TOML back into the flat key space QSettings expects.
bool populateSettingsMap(const toml::table& table, QSettings::SettingsMap& map,
                         const QString& prefix = {})
{
  for (const auto& [key, node] : table) {
    const QString keyName = keyToQString(key);
    const QString currentKey =
        prefix.isEmpty() ? keyName : QString("%1/%2").arg(prefix, keyName);

    if (const auto* childTable = node.as_table();
        childTable != nullptr && !isTypedValueTable(*childTable)) {
      if (!populateSettingsMap(*childTable, map, currentKey)) {
        return false;
      }
      continue;
    }

    const auto value = decodeTomlValue(node);
    if (!value.has_value()) {
      return false;
    }

    map.insert(currentKey, *value);
  }

  return true;
}

toml::table encodeTypedStringValue(std::string_view typeName, const QString& value)
{
  toml::table table;
  table.insert_or_assign(typedValueTypeKey, std::string(typeName));
  table.insert_or_assign(typedValueValueKey, value.toStdString());
  return table;
}

bool insertVariantNode(toml::table& table, const QString& key, const QVariant& value)
{
  const std::string tomlKey    = key.toStdString();
  const int         metaTypeId = value.metaType().id();

  if (metaTypeId == QMetaType::QString) {
    table.insert_or_assign(tomlKey, value.toString().toStdString());
    return true;
  }

  if (metaTypeId == QMetaType::Bool) {
    table.insert_or_assign(tomlKey, value.toBool());
    return true;
  }

  if (metaTypeId == QMetaType::Int) {
    table.insert_or_assign(tomlKey, value.toInt());
    return true;
  }

  // TOML has no native path or shortcut type, so these are stored as tagged tables
  // to preserve their QVariant type on readback.
  if (metaTypeId == qMetaTypeId<std::filesystem::path>()) {
    const auto typedValue = encodeTypedStringValue(
        pathTypeName,
        QString::fromStdString(value.value<std::filesystem::path>().generic_string()));
    table.insert_or_assign(tomlKey, typedValue);
    return true;
  }

  if (metaTypeId == qMetaTypeId<QKeySequence>()) {
    const auto typedValue = encodeTypedStringValue(
        shortcutTypeName,
        value.value<QKeySequence>().toString(QKeySequence::PortableText));
    table.insert_or_assign(tomlKey, typedValue);
    return true;
  }

  return false;
}

bool writeSettingsEntry(toml::table& root, const QString& key, const QVariant& value)
{
  const QStringList path = key.split('/', Qt::SkipEmptyParts);
  if (path.isEmpty()) {
    return false;
  }

  toml::table* currentTable = &root;
  for (qsizetype i = 0; i < path.size() - 1; ++i) {
    const std::string section    = path[i].toStdString();
    auto*             childTable = currentTable->get_as<toml::table>(section);
    if (childTable == nullptr) {
      auto [it, inserted] = currentTable->insert(section, toml::table{});
      if (!inserted) {
        return false;
      }

      childTable = it->second.as_table();
    }

    if (childTable == nullptr) {
      return false;
    }

    currentTable = childTable;
  }

  return insertVariantNode(*currentTable, path.back(), value);
}

bool readTomlFile(QIODevice& device, QSettings::SettingsMap& map)
{
  try {
    const QByteArray contents = device.readAll();
    if (contents.trimmed().isEmpty()) {
      return true;
    }

    const toml::table table = toml::parse(contents.toStdString());
    return populateSettingsMap(table, map);
  } catch (const toml::parse_error&) {
    return false;
  }
}

bool writeTomlFile(QIODevice& device, const QSettings::SettingsMap& map)
{
  toml::table table;
  for (auto it = map.cbegin(); it != map.cend(); ++it) {
    if (!writeSettingsEntry(table, it.key(), it.value())) {
      return false;
    }
  }

  std::ostringstream stream;
  stream << table;
  const std::string serialized = stream.str();
  return device.write(serialized.c_str(), static_cast<qint64>(serialized.size()))
         == static_cast<qint64>(serialized.size());
}

const QSettings::Format tomlFormat =
    QSettings::registerFormat("toml", readTomlFile, writeTomlFile);

}  // namespace

namespace settings {

QVariant value(const QSettings& settings, const Definition& setting)
{
  return settings.value(setting.name, setting.defaultValue);
}

}  // namespace settings

SiliconSettings::SiliconSettings(const QString& appName, QObject* parent)
  : QSettings(tomlFormat, QSettings::UserScope, "SILICON", appName, parent)
{
}

QSettings::Format SiliconSettings::format()
{
  return tomlFormat;
}

CommonSettingsValues readCommonSettings(const QSettings& settings)
{
  return {
      .theme = SILICON::ui::settings::value(settings, SILICON::ui::settings::Theme).toString(),
      .maxSimulationSteps =
          SILICON::ui::settings::value(settings, SILICON::ui::settings::MaxSimulationSteps).toInt(),
      .maxTransitionsPerDeltaCycle =
          SILICON::ui::settings::value(settings, SILICON::ui::settings::MaxTransitionsPerDeltaCycle)
              .toInt(),
  };
}

SILICON::ui::theme::Mode themeModeFromText(const QString& value)
{
  return value == SILICON::ui::settings::DarkTheme ? SILICON::ui::theme::Mode::Dark
                                            : SILICON::ui::theme::Mode::Light;
}

}  // namespace ui
}  // namespace SILICON
