/*
 Copyright (c) 2026. Giulio Cocconi
 ...
 */

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <core/projectDocument.hpp>

namespace silicon::project {

/**
 * @brief MIME marker stored in Silicon project archives.
 *
 * A `.sil` file is a ZIP archive. The first entry written by Silicon is a
 * stored, uncompressed `mimetype` file containing this exact value, allowing
 * readers to reject unrelated ZIP files before parsing JSON payloads.
 */
inline constexpr std::string_view ProjectMimeType = "application/vnd.silicon.project+zip";

/**
 * @brief Current version of the Silicon project archive schema.
 *
 * Readers reject archives whose `metadata.json.formatVersion` differs from
 * this value.
 */
inline constexpr int ProjectFormatVersion = 1;

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
 * `project.json.mainCircuit` must point to one of the JSON entries under
 * `circuits/`.
 */
inline constexpr std::string_view DefaultMainCircuitPath = "circuits/main.json";

/**
 * @brief Metadata stored in `metadata.json`.
 */
struct ProjectMetadata {
  /// Project archive schema version.
  int formatVersion = ProjectFormatVersion;
  /// Silicon application version that wrote the file.
  std::string siliconVersion;
  /// UTC timestamp for the first save, formatted as `YYYY-MM-DDTHH:MM:SSZ`.
  std::string creationDate;
  /// UTC timestamp for the latest save, formatted as `YYYY-MM-DDTHH:MM:SSZ`.
  std::string lastModify;
};

/**
 * @brief Project-level data stored in `project.json`.
 */
struct ProjectInfo {
  /// Human-readable project name.
  std::string name;
  /// ZIP entry containing the main circuit JSON.
  std::string mainCircuit{DefaultMainCircuitPath};
  /// Optional human-readable project description.
  std::string description;
};

/**
 * @brief In-memory representation of a `.sil` project archive.
 *
 * The circuit payload is kept as serialized JSON so UI and core circuit
 * serializers can evolve independently from the archive container code.
 */
struct ProjectFile {
  ProjectMetadata metadata;
  ProjectInfo     project;
  /// Authoritative ordered collection of project documents.
  std::vector<Document> documents;
  /// Compatibility mirror of the document referenced by project.mainCircuit.
  /// This is derived data and is never an independent document store.
  std::string mainCircuitJson;
};

/**
 * @brief Reads and validates a Silicon `.sil` project archive.
 *
 * @throws std::runtime_error if the archive cannot be opened, does not contain
 * the expected ZIP structure, has invalid JSON, references an unsupported
 * format version, or violates the circuit archive layout.
 */
[[nodiscard]] ProjectFile readProjectFile(const std::filesystem::path& path);

/**
 * @brief Writes a Silicon `.sil` project archive.
 *
 * The writer emits all circuit payloads listed in ProjectFile::documents.
 *
 * @throws std::runtime_error if the project references an invalid/missing circuit
 * path or the archive cannot be created/finalized.
 */
void writeProjectFile(const std::filesystem::path& path, const ProjectFile& projectFile);

/**
 * @brief Returns the current UTC timestamp formatted as `YYYY-MM-DDTHH:MM:SSZ`.
 */
[[nodiscard]] std::string currentUtcTimestamp();

/**
 * @brief Creates default metadata for a new project file.
 */
[[nodiscard]] ProjectMetadata metadataForNewFile();

}  // namespace silicon::project
