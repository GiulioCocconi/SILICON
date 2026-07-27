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

#include "projectFile.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <format>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <zip.h>

namespace silicon::project {
namespace {

  constexpr const char* MetadataPath = "metadata.json";
  constexpr const char* ProjectPath  = "project.json";

  // RAII wrappers keep libzip's C handles exception-safe until ownership is
  // deliberately transferred or the archive is committed.
  struct ZipDeleter {
    void operator()(zip_t* z) const noexcept
    {
      if (z)
        zip_discard(z);
    }
  };
  struct ZipFileDeleter {
    void operator()(zip_file_t* f) const noexcept
    {
      if (f)
        zip_fclose(f);
    }
  };
  struct ZipSourceDeleter {
    void operator()(zip_source_t* s) const noexcept
    {
      if (s)
        zip_source_free(s);
    }
  };

  using UniqueZip       = std::unique_ptr<zip_t, ZipDeleter>;
  using UniqueZipFile   = std::unique_ptr<zip_file_t, ZipFileDeleter>;
  using UniqueZipSource = std::unique_ptr<zip_source_t, ZipSourceDeleter>;

  [[nodiscard]] std::string readEntry(zip_t* archive, const char* entryName)
  {
    const zip_int64_t index = zip_name_locate(archive, entryName, ZIP_FL_ENC_UTF_8);
    if (index < 0)
      throw std::runtime_error(std::format("Project archive is missing {}", entryName));

    zip_stat_t stat;
    zip_stat_init(&stat);
    if (zip_stat_index(archive, static_cast<zip_uint64_t>(index), 0, &stat) != 0)
      throw std::runtime_error(
          std::format("Cannot inspect project archive entry {}", entryName));

    if ((stat.valid & ZIP_STAT_SIZE) == 0)
      throw std::runtime_error(
          std::format("Project archive entry {} has no size", entryName));

    if (stat.size == 0)
      return {};

    UniqueZipFile file(zip_fopen_index(archive, static_cast<zip_uint64_t>(index), 0));
    if (!file)
      throw std::runtime_error(
          std::format("Cannot read project archive entry {}", entryName));

    std::string contents;
    // C++23: resize directly into the final string buffer so large circuit JSON
    // entries are not first zero-filled and then overwritten by zip_fread.
    contents.resize_and_overwrite(stat.size, [&](char* buf, std::size_t /*capacity*/) {
      const zip_int64_t bytesRead = zip_fread(file.get(), buf, stat.size);
      if (bytesRead < 0 || static_cast<zip_uint64_t>(bytesRead) != stat.size) {
        throw std::runtime_error(
            std::format("Cannot read complete project archive entry {}", entryName));
      }
      return static_cast<std::size_t>(bytesRead);
    });

    return contents;
  }

  void addEntry(zip_t* archive, const char* entryName, const std::string_view contents,
                const bool storeUncompressed = false)
  {
    void* buffer = std::malloc(contents.size());
    if (!buffer && !contents.empty())
      throw std::bad_alloc();

    if (!contents.empty())
      std::memcpy(buffer, contents.data(), contents.size());

    // libzip takes ownership of the malloc'd buffer when freep=1, but only if
    // zip_source_buffer succeeds. Until then, this function must free it.
    UniqueZipSource source(zip_source_buffer(archive, buffer, contents.size(), 1));
    if (!source) {
      std::free(buffer);
      throw std::runtime_error(std::format("Cannot create zip source for {}", entryName));
    }

    const zip_int64_t index = zip_file_add(archive, entryName, source.get(),
                                           ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
    if (index < 0)
      throw std::runtime_error(
          std::format("Cannot add {} to project archive", entryName));

    // zip_file_add attaches the source to the archive. Releasing here prevents
    // the local UniqueZipSource from freeing an object now owned by libzip.
    source.release();

    if (storeUncompressed
        && zip_set_file_compression(archive, static_cast<zip_uint64_t>(index),
                                    ZIP_CM_STORE, 0)
               != 0) {
      throw std::runtime_error(std::format("Cannot store {} uncompressed", entryName));
    }
  }

  template <typename T>
  [[nodiscard]] T requireField(const nlohmann::json& obj, const std::string_view key,
                               const std::string_view entryName,
                               const std::string_view typeName)
  {
    auto it = obj.find(key);
    if (it == obj.end())
      throw std::runtime_error(std::format("{}.{} is missing", entryName, key));
    try {
      return it->get<T>();
    } catch (const nlohmann::json::type_error&) {
      throw std::runtime_error(
          std::format("{}.{} must be a(n) {}", entryName, key, typeName));
    }
  }

  [[nodiscard]] nlohmann::json parseRequiredObject(const std::string_view jsonStr,
                                                   const std::string_view entryName)
  {
    try {
      auto parsed = nlohmann::json::parse(jsonStr);
      if (!parsed.is_object())
        throw std::runtime_error(std::format("{} must contain a JSON object", entryName));
      return parsed;
    } catch (const nlohmann::json::parse_error&) {
      throw std::runtime_error(std::format("{} is not valid JSON", entryName));
    }
  }

  [[nodiscard]] std::vector<std::string> documentEntries(zip_t*                 archive,
                                                         const std::string_view directory)
  {
    std::vector<std::string> entries;

    const zip_int64_t count = zip_get_num_entries(archive, 0);
    if (count < 0)
      throw std::runtime_error("Cannot inspect project archive entries");

    for (zip_uint64_t index = 0; index < static_cast<zip_uint64_t>(count); ++index) {
      if (const char* name = zip_get_name(archive, index, ZIP_FL_ENC_UTF_8)) {
        const std::string_view entryName{name};
        // Documents are intentionally discovered from the archive rather
        // than trusting project.json; validation compares both views below.
        if (entryName.starts_with(directory) && entryName.ends_with(".json"))
          entries.emplace_back(entryName);
      }
    }

    // Sorting makes diagnostics and validation deterministic even though ZIP
    // central directory order is an implementation detail of the writer.
    std::ranges::sort(entries);
    return entries;
  }

  [[nodiscard]] std::vector<std::string> assetEntries(zip_t* archive)
  {
    std::vector<std::string>        entries;
    std::unordered_set<std::string> seen;
    const zip_int64_t               count = zip_get_num_entries(archive, 0);
    if (count < 0)
      throw std::runtime_error("Cannot inspect project archive entries");

    for (zip_uint64_t index = 0; index < static_cast<zip_uint64_t>(count); ++index) {
      const char* name = zip_get_name(archive, index, ZIP_FL_ENC_UTF_8);
      if (!name)
        continue;
      const std::string entryName{name};
      if (entryName == "mimetype" || entryName == MetadataPath || entryName == ProjectPath
          || classifyDocumentPath(entryName) || entryName.ends_with('/')) {
        continue;
      }
      if (!isValidProjectAssetPath(entryName))
        throw std::runtime_error("Project archive contains an invalid asset path");
      if (!seen.insert(entryName).second)
        throw std::runtime_error("Project archive contains duplicate asset entries");
      entries.push_back(entryName);
    }
    std::ranges::sort(entries);
    return entries;
  }

  void validateCircuitPaths(const std::string_view          mainCircuit,
                            const std::vector<std::string>& circuits)
  {
    if (classifyDocumentPath(mainCircuit) != DocumentKind::Circuit)
      throw std::runtime_error(
          "project.json.mainCircuit must reference a valid circuit JSON entry");

    if (circuits.empty())
      throw std::runtime_error("Project archive must contain at least one circuit entry");

    std::unordered_set<std::string> seen;
    for (const auto& circuit : circuits) {
      if (classifyDocumentPath(circuit) != DocumentKind::Circuit)
        throw std::runtime_error(
            "Project archive contains an invalid circuit JSON entry");
      if (!seen.insert(circuit).second)
        throw std::runtime_error("Project archive contains duplicate circuit entries");
    }

    if (!seen.contains(std::string(mainCircuit)))
      throw std::runtime_error(
          "project.json.mainCircuit does not match an archive circuit entry");
  }

  void validateDocuments(const std::string_view       mainCircuit,
                         const std::vector<Document>& documents)
  {
    std::vector<std::string>        circuits;
    std::unordered_set<std::string> paths;
    std::unordered_set<std::string> subcircuitSlugs;
    for (const auto& document : documents) {
      if (!paths.insert(document.path()).second)
        throw std::runtime_error("Project archive contains duplicate document entries");
      if (document.kind() == DocumentKind::Circuit) {
        circuits.push_back(document.path());
      } else {
        const auto slug = document.subcircuitSlug();
        if (!slug || !subcircuitSlugs.insert(*slug).second)
          throw std::runtime_error("Project archive contains duplicate subcircuit slugs");
      }
    }
    validateCircuitPaths(mainCircuit, circuits);
  }

  void validateAssets(const std::vector<Document>&     documents,
                      const std::vector<ProjectAsset>& assets)
  {
    std::unordered_set<std::string> paths;
    for (const auto& asset : assets) {
      if (!isValidProjectAssetPath(asset.path))
        throw std::runtime_error("Project contains an invalid asset path");
      if (!paths.insert(asset.path).second)
        throw std::runtime_error("Project contains duplicate asset paths");
    }

    std::unordered_set<std::string> referencedPaths;
    for (const auto& document : documents) {
      if (document.kind() != DocumentKind::Subcircuit)
        continue;
      const auto hdl = parseHdlDescriptor(document.sceneJson());
      if (hdl) {
        if (!paths.contains(hdl->path)) {
          throw std::runtime_error(
              std::format("Subcircuit HDL asset '{}' is missing", hdl->path));
        }
        if (!referencedPaths.insert(hdl->path).second) {
          throw std::runtime_error(std::format(
              "Subcircuit HDL asset '{}' is referenced more than once", hdl->path));
        }
      }
    }
  }

  [[nodiscard]] ProjectMetadata parseMetadata(const nlohmann::json& metadataJson)
  {
    ProjectMetadata metadata;
    metadata.formatVersion =
        requireField<int>(metadataJson, "formatVersion", MetadataPath, "integer");
    if (metadata.formatVersion != ProjectFormatVersion) {
      throw std::runtime_error(std::format(
          "Unsupported Silicon project format version {}", metadata.formatVersion));
    }

    metadata.siliconVersion =
        requireField<std::string>(metadataJson, "siliconVersion", MetadataPath, "string");
    metadata.creationDate =
        requireField<std::string>(metadataJson, "creationDate", MetadataPath, "string");
    metadata.lastModify =
        requireField<std::string>(metadataJson, "lastModify", MetadataPath, "string");
    return metadata;
  }

  [[nodiscard]] ProjectInfo parseProjectInfo(const nlohmann::json& projectJson)
  {
    ProjectInfo project;
    project.name = requireField<std::string>(projectJson, "name", ProjectPath, "string");
    project.mainCircuit =
        requireField<std::string>(projectJson, "mainCircuit", ProjectPath, "string");
    project.description =
        requireField<std::string>(projectJson, "description", ProjectPath, "string");
    return project;
  }

  [[nodiscard]] nlohmann::ordered_json metadataToJson(const ProjectMetadata& metadata)
  {
    return nlohmann::ordered_json{{"formatVersion", metadata.formatVersion},
                                  {"siliconVersion", metadata.siliconVersion},
                                  {"creationDate", metadata.creationDate},
                                  {"lastModify", metadata.lastModify}};
  }

  [[nodiscard]] nlohmann::ordered_json projectInfoToJson(const ProjectInfo& project)
  {
    return nlohmann::ordered_json{{"name", project.name},
                                  {"mainCircuit", project.mainCircuit},
                                  {"description", project.description}};
  }

}  // namespace

std::string currentUtcTimestamp()
{
  // Format an ISO-8601-like UTC timestamp without storing local timezone state
  // in project files.
  const auto now =
      std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
  return std::format("{:%Y-%m-%dT%H:%M:%SZ}", now);
}

ProjectMetadata metadataForNewFile()
{
  const auto now = currentUtcTimestamp();
  return {.formatVersion  = ProjectFormatVersion,
          .siliconVersion = SILICON_VERSION,
          .creationDate   = now,
          .lastModify     = now};
}

ProjectFile readProjectFile(const std::filesystem::path& path)
{
  int       errorCode = 0;
  UniqueZip archive(zip_open(path.string().c_str(), ZIP_RDONLY, &errorCode));
  if (!archive) {
    zip_error_t error;
    zip_error_init_with_code(&error, errorCode);
    const std::string message = zip_error_strerror(&error);
    zip_error_fini(&error);
    throw std::runtime_error(
        std::format("Cannot open Silicon project archive: {}", message));
  }

  // Check the marker entry before parsing JSON so raw JSON files and unrelated
  // ZIP archives fail with a container-level error.
  const auto mimetype = readEntry(archive.get(), "mimetype");
  if (mimetype != ProjectMimeType)
    throw std::runtime_error("Silicon project archive has an unexpected mimetype");

  const auto metadataJson =
      parseRequiredObject(readEntry(archive.get(), MetadataPath), MetadataPath);
  const auto projectJson =
      parseRequiredObject(readEntry(archive.get(), ProjectPath), ProjectPath);

  auto metadata = parseMetadata(metadataJson);
  auto project  = parseProjectInfo(projectJson);

  const auto circuitPaths    = documentEntries(archive.get(), "circuits/");
  const auto subcircuitPaths = documentEntries(archive.get(), "subcircuits/");
  validateCircuitPaths(project.mainCircuit, circuitPaths);

  std::vector<Document> documents;
  documents.reserve(circuitPaths.size() + subcircuitPaths.size());
  for (const auto& path : circuitPaths)
    documents.emplace_back(path, readEntry(archive.get(), path.c_str()));
  for (const auto& path : subcircuitPaths)
    documents.emplace_back(path, readEntry(archive.get(), path.c_str()));
  validateDocuments(project.mainCircuit, documents);

  std::vector<ProjectAsset> assets;
  for (const auto& path : assetEntries(archive.get()))
    assets.push_back({path, readEntry(archive.get(), path.c_str())});
  validateAssets(documents, assets);

  auto mainCircuitIt = std::ranges::find(documents, project.mainCircuit, &Document::path);
  if (mainCircuitIt == documents.end())
    throw std::runtime_error(
        "project.json.mainCircuit does not match an archive circuit entry");
  auto mainCircuitJson = mainCircuitIt->sceneJson();

  return ProjectFile{.metadata        = std::move(metadata),
                     .project         = std::move(project),
                     .documents       = std::move(documents),
                     .assets          = std::move(assets),
                     .mainCircuitJson = std::move(mainCircuitJson)};
}

void writeProjectFile(const std::filesystem::path& path, const ProjectFile& projectFile)
{
  auto documents = projectFile.documents;
  if (std::ranges::none_of(documents,
                           [](const Document& document) {
                             return document.kind() == DocumentKind::Circuit;
                           })
      && !projectFile.mainCircuitJson.empty()) {
    documents.emplace_back(std::string(DefaultMainCircuitPath),
                           projectFile.mainCircuitJson);
  }

  validateDocuments(projectFile.project.mainCircuit, documents);
  validateAssets(documents, projectFile.assets);

  int       errorCode = 0;
  UniqueZip archive(
      zip_open(path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &errorCode));
  if (!archive) {
    zip_error_t error;
    zip_error_init_with_code(&error, errorCode);
    const std::string message = zip_error_strerror(&error);
    zip_error_fini(&error);
    throw std::runtime_error(
        std::format("Cannot create Silicon project archive: {}", message));
  }

  // The mimetype entry is stored uncompressed so external tools can identify a
  // `.sil` file by reading the ZIP entry directly.
  addEntry(archive.get(), "mimetype", ProjectMimeType, true);
  addEntry(archive.get(), MetadataPath, metadataToJson(projectFile.metadata).dump(2));
  addEntry(archive.get(), ProjectPath, projectInfoToJson(projectFile.project).dump(2));
  for (const auto& circuit : documents) {
    const auto sceneJson =
        circuit.sceneJson().empty() && circuit.path() == projectFile.project.mainCircuit
            ? std::string_view(projectFile.mainCircuitJson)
            : std::string_view(circuit.sceneJson());
    addEntry(archive.get(), circuit.path().c_str(), sceneJson);
  }
  for (const auto& asset : projectFile.assets)
    addEntry(archive.get(), asset.path.c_str(), asset.contents);

  if (zip_close(archive.get()) != 0)
    throw std::runtime_error("Cannot finalize Silicon project archive");

  // zip_close commits and frees the archive; release prevents ZipDeleter from
  // calling zip_discard on a handle that libzip has already consumed.
  archive.release();
}

}  // namespace silicon::project
