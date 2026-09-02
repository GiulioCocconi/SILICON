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
#include <string>
#include <string_view>
#include <vector>

#include <core/projectDocument.hpp>

namespace SILICON::project {

/**
 * @brief MIME marker stored in Silicon project archives.
 *
 * A `.sil` file is a ZIP archive. The first entry written by Silicon is a
 * stored, uncompressed `mimetype` file containing this exact value, allowing
 * readers to reject unrelated ZIP files before parsing JSON payloads.
 */
inline constexpr std::string_view MIME_TYPE = "application/vnd.silicon.project+zip";

/**
 * @brief Current version of the Silicon project archive schema.
 *
 * Readers reject archives whose `metadata.json.formatVersion` differs from
 * this value.
 */
inline constexpr int FORMAT_VERSION = 1;

/**
 * @brief Default path of the initial circuit JSON file.
 *
 * The archive layout is:
 *
 * @code
 * mimetype            application/vnd.silicon.project+zip, stored uncompressed
 * metadata.json       archive/schema metadata
 * project.json        project-level information and main circuit reference
 * circuits/main.json  serialized LogiFlow circuit JSON
 * @endcode
 *
 * `project.json.mainCircuit` must point to one of the document entries whose
 * type is @ref DocumentType::Circuit.
 */
inline const std::string DEFAULT_MAIN_CIRCUIT_PATH =
    documentPathForSlug(DocumentType::Circuit, "main");

/**
 * @brief Metadata stored in `metadata.json`.
 */
struct ProjectMetadata {
  /// Project archive schema version.
  int formatVersion = FORMAT_VERSION;
  /// Silicon application version that wrote the file.
  std::string siliconVersion;
  /// UTC timestamp for the first save, formatted as `YYYY-MM-DDTHH\:MM\:SSZ`.
  std::string creationDate;
  /// UTC timestamp for the latest save, formatted as `YYYY-MM-DDTHH\:MM\:SSZ`.
  std::string lastModify;
};

/**
 * @brief Project-level data stored in `project.json`.
 */
struct ProjectInfo {
  /// Human-readable project name.
  std::string name;
  /// ZIP entry containing the main circuit JSON.
  std::string mainCircuit{DEFAULT_MAIN_CIRCUIT_PATH};
  /// Optional human-readable project description.
  std::string description;
};

/**
 * @brief In-memory representation of a `.sil` project archive.
 *
 * Document contents remain serialized so their editors and serializers can evolve
 * independently from the archive container code.
 */
struct ProjectFile {
  ProjectMetadata       metadata;
  ProjectInfo           project;
  /// Authoritative ordered collection of project documents.
  std::vector<Document> documents;
};

/**
 * @brief Reads and validates a Silicon `.sil` project archive.
 *
 * Only the reserved archive entries and recognized project documents are accepted.
 *
 * @throws std::runtime_error if the archive cannot be opened, does not contain
 * the expected ZIP structure, has invalid JSON, references an unsupported
 * format version, contains an unknown entry, or violates the document layout.
 */
[[nodiscard]] ProjectFile readProjectFile(const std::filesystem::path& path);

/**
 * @brief Writes a Silicon `.sil` project archive.
 *
 * The writer emits every document listed by @p projectFile.
 *
 * @throws std::runtime_error if the project references an invalid/missing circuit
 * path or the archive cannot be created/finalized.
 */
void writeProjectFile(const std::filesystem::path& path, const ProjectFile& projectFile);

/**
 * @brief Returns the current UTC timestamp formatted as `YYYY-MM-DDTHH\:MM\:SSZ`.
 */
[[nodiscard]] std::string currentUtcTimestamp();

/**
 * @brief Creates default metadata for a new project file.
 */
[[nodiscard]] ProjectMetadata metadataForNewFile();

}  // namespace SILICON::project
