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

namespace SILICON::project {
namespace {

  constexpr std::string_view MetadataPath = "metadata.json";
  constexpr std::string_view ProjectPath  = "project.json";

  template <auto Fn> struct ZipDeleter {
    template <typename T> void operator()(T* ptr) const noexcept
    {
      if (ptr)
        Fn(ptr);
    }
  };

  using UniqueZip       = std::unique_ptr<zip_t, ZipDeleter<zip_discard>>;
  using UniqueZipFile   = std::unique_ptr<zip_file_t, ZipDeleter<zip_fclose>>;
  using UniqueZipSource = std::unique_ptr<zip_source_t, ZipDeleter<zip_source_free>>;

  [[nodiscard]] std::string readEntry(zip_t* archive, const std::string& entryName)
  {
    const zip_int64_t entryIndex =
        zip_name_locate(archive, entryName.c_str(), ZIP_FL_ENC_UTF_8);
    if (entryIndex < 0)
      throw std::runtime_error(std::format("Project archive is missing {}", entryName));

    zip_stat_t stat;
    zip_stat_init(&stat);
    if (zip_stat_index(archive, entryIndex, 0, &stat) != 0)
      throw std::runtime_error(
          std::format("Cannot inspect project archive entry {}", entryName));

    if ((stat.valid & ZIP_STAT_SIZE) == 0)
      throw std::runtime_error(
          std::format("Project archive entry {} has no size", entryName));

    if (stat.size == 0)
      return {};

    UniqueZipFile file(zip_fopen_index(archive, entryIndex, 0));
    if (!file)
      throw std::runtime_error(
          std::format("Cannot read project archive entry {}", entryName));

    std::string contents;
    contents.resize_and_overwrite(stat.size, [&](char* buf, std::size_t) {
      const zip_int64_t bytesRead = zip_fread(file.get(), buf, stat.size);
      if (bytesRead < 0 || static_cast<zip_uint64_t>(bytesRead) != stat.size) {
        throw std::runtime_error(
            std::format("Cannot read complete project archive entry {}", entryName));
      }
      return static_cast<std::size_t>(bytesRead);
    });

    return contents;
  }

  void addEntry(zip_t* archive, const std::string& entryName, std::string_view contents,
                bool storeUncompressed = false)
  {
    void* buffer = std::malloc(contents.size());
    if (!buffer && !contents.empty())
      throw std::bad_alloc();

    if (!contents.empty()) {
      std::memcpy(buffer, contents.data(), contents.size());
    }

    UniqueZipSource source(zip_source_buffer(archive, buffer, contents.size(), 1));
    if (!source) {
      std::free(buffer);
      throw std::runtime_error(std::format("Cannot create zip source for {}", entryName));
    }

    const zip_int64_t index = zip_file_add(archive, entryName.c_str(), source.get(),
                                           ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
    if (index < 0)
      throw std::runtime_error(
          std::format("Cannot add {} to project archive", entryName));

    source.release();  // Ownership successfully transferred to libzip

    if (storeUncompressed
        && zip_set_file_compression(archive, index, ZIP_CM_STORE, 0) != 0)
      throw std::runtime_error(std::format("Cannot store {} uncompressed", entryName));
  }

  template <typename F> void enumerateZipEntries(zip_t* archive, F&& callback)
  {
    const zip_int64_t count = zip_get_num_entries(archive, 0);
    if (count < 0)
      throw std::runtime_error("Cannot inspect project archive entries");

    for (zip_uint64_t i = 0; i < static_cast<zip_uint64_t>(count); ++i) {
      if (const char* name = zip_get_name(archive, i, ZIP_FL_ENC_UTF_8)) {
        callback(std::string_view{name});
      }
    }
  }

  void validateUniqueArchiveEntries(zip_t* archive)
  {
    std::vector<std::string> names;
    enumerateZipEntries(archive,
                        [&](const std::string_view name) { names.emplace_back(name); });
    std::ranges::sort(names);
    if (std::ranges::adjacent_find(names) != names.end())
      throw std::runtime_error("Project archive contains duplicate entries");
  }

  template <typename T>
  [[nodiscard]] T requireField(const nlohmann::json& obj, std::string_view key,
                               std::string_view entryName)
  {
    try {
      return obj.at(key).get<T>();
    } catch (const nlohmann::json::exception&) {
      throw std::runtime_error(
          std::format("{}.{} is missing or of the wrong type", entryName, key));
    }
  }

  [[nodiscard]] std::vector<std::string> documentEntries(zip_t* archive)
  {
    std::vector<std::string> entries;
    enumerateZipEntries(archive, [&](std::string_view name) {
      if (name == "mimetype" || name == MetadataPath || name == ProjectPath
          || name.ends_with('/'))
        return;
      if (documentTypeForPath(name))
        entries.emplace_back(name);
    });
    std::ranges::sort(entries, [](const std::string& left, const std::string& right) {
      const auto leftType  = *documentTypeForPath(left);
      const auto rightType = *documentTypeForPath(right);
      return leftType == rightType ? left < right : leftType < rightType;
    });
    return entries;
  }

  [[nodiscard]] std::vector<std::string> assetEntries(zip_t* archive)
  {
    std::vector<std::string> entries;
    enumerateZipEntries(archive, [&](const std::string_view name) {
      if (name == "mimetype" || name == MetadataPath || name == ProjectPath
          || documentTypeForPath(name))
        return;
      if (name.ends_with('/')) {
        const auto directory = name.substr(0, name.size() - 1);
        if (directory == "circuits" || directory == "subcircuits"
            || directory == "code" || isValidProjectAssetPath(directory))
          return;
        throw std::runtime_error(
            std::format("Project archive contains invalid directory entry {}", name));
      }
      if (!isValidProjectAssetPath(name))
        throw std::runtime_error(
            std::format("Project archive contains invalid or reserved entry {}", name));
      entries.emplace_back(name);
    });
    std::ranges::sort(entries);
    return entries;
  }

  void validateCircuitPaths(std::string_view                mainCircuit,
                            const std::vector<std::string>& circuits)
  {
    if (documentTypeForPath(mainCircuit) != DocumentType::Circuit)
      throw std::runtime_error(
          "project.json.mainCircuit must reference a valid circuit JSON entry");

    if (circuits.empty())
      throw std::runtime_error("Project archive must contain at least one circuit entry");

    std::vector<std::string_view> sortedCircuits(circuits.begin(), circuits.end());
    std::ranges::sort(sortedCircuits);

    if (std::ranges::adjacent_find(sortedCircuits) != sortedCircuits.end())
      throw std::runtime_error("Project archive contains duplicate circuit entries");

    for (const auto& circuit : circuits) {
      if (documentTypeForPath(circuit) != DocumentType::Circuit)
        throw std::runtime_error(
            "Project archive contains an invalid circuit JSON entry");
    }

    if (!std::ranges::binary_search(sortedCircuits, mainCircuit))
      throw std::runtime_error(
          "project.json.mainCircuit does not match an archive circuit entry");
  }

  void validateDocuments(std::string_view             mainCircuit,
                         const std::vector<Document>& documents)
  {
    std::vector<std::string>      circuits;
    std::vector<std::string_view> paths;
    std::vector<std::string>      subcircuitSlugs;

    for (const auto& doc : documents) {
      paths.push_back(doc.getPath());
      if (doc.getType() == DocumentType::Circuit) {
        circuits.push_back(doc.getPath());
      } else if (auto slug = doc.subcircuitSlug()) {
        subcircuitSlugs.push_back(*std::move(slug));
      }
    }

    std::ranges::sort(paths);
    if (std::ranges::adjacent_find(paths) != paths.end())
      throw std::runtime_error("Project archive contains duplicate document entries");

    std::ranges::sort(subcircuitSlugs);
    if (std::ranges::adjacent_find(subcircuitSlugs) != subcircuitSlugs.end())
      throw std::runtime_error("Project archive contains duplicate subcircuit slugs");

    validateCircuitPaths(mainCircuit, circuits);
  }

  void validateAssets(const std::vector<ProjectAsset>& assets)
  {
    std::unordered_set<std::string> paths;
    for (const auto& asset : assets) {
      if (!isValidProjectAssetPath(asset.path))
        throw std::runtime_error(
            std::format("Project contains invalid asset path {}", asset.path));
      if (!paths.insert(asset.path).second)
        throw std::runtime_error(
            std::format("Project contains duplicate asset path {}", asset.path));
    }
  }

  [[nodiscard]] ProjectMetadata parseMetadata(zip_t* archive)
  {
    const auto json =
        nlohmann::json::parse(readEntry(archive, std::string(MetadataPath)));
    ProjectMetadata metadata{
        .formatVersion  = requireField<int>(json, "formatVersion", MetadataPath),
        .siliconVersion = requireField<std::string>(json, "siliconVersion", MetadataPath),
        .creationDate   = requireField<std::string>(json, "creationDate", MetadataPath),
        .lastModify     = requireField<std::string>(json, "lastModify", MetadataPath)};

    if (metadata.formatVersion != FORMAT_VERSION)
      throw std::runtime_error(std::format(
          "Unsupported Silicon project format version {}", metadata.formatVersion));

    return metadata;
  }

  [[nodiscard]] ProjectInfo parseProjectInfo(zip_t* archive)
  {
    const auto json = nlohmann::json::parse(readEntry(archive, std::string(ProjectPath)));
    if (!json.is_object() || json.size() != 3 || !json.contains("name")
        || !json.contains("mainCircuit") || !json.contains("description"))
      throw std::runtime_error(
          "project.json must contain only name, mainCircuit, and description");
    return ProjectInfo{
        .name        = requireField<std::string>(json, "name", ProjectPath),
        .mainCircuit = requireField<std::string>(json, "mainCircuit", ProjectPath),
        .description = requireField<std::string>(json, "description", ProjectPath)};
  }

  [[nodiscard]] nlohmann::ordered_json metadataToJson(const ProjectMetadata& m)
  {
    return {{"formatVersion", m.formatVersion},
            {"siliconVersion", m.siliconVersion},
            {"creationDate", m.creationDate},
            {"lastModify", m.lastModify}};
  }

  [[nodiscard]] nlohmann::ordered_json projectInfoToJson(const ProjectInfo& p)
  {
    return {{"name", p.name},
            {"mainCircuit", p.mainCircuit},
            {"description", p.description}};
  }

}  // namespace

std::string currentUtcTimestamp()
{
  const auto now =
      std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
  return std::format("{:%Y-%m-%dT%H:%M:%SZ}", now);
}

ProjectMetadata metadataForNewFile()
{
  const auto now = currentUtcTimestamp();
  return {.formatVersion  = FORMAT_VERSION,
          .siliconVersion = SILICON_VERSION,
          .creationDate   = now,
          .lastModify     = now};
}

ProjectFile readProjectFile(const std::filesystem::path& path)
{
  int       errorCode = 0;
  UniqueZip archive(zip_open(path.string().c_str(), ZIP_RDONLY, &errorCode));
  if (!archive)
    throw std::runtime_error(
        std::format("Cannot open Silicon project archive: ZIP error code {}", errorCode));

  validateUniqueArchiveEntries(archive.get());
  if (readEntry(archive.get(), "mimetype") != MIME_TYPE)
    throw std::runtime_error("Silicon project archive has an unexpected mimetype");

  auto metadata = parseMetadata(archive.get());
  auto project  = parseProjectInfo(archive.get());

  const auto documentPaths = documentEntries(archive.get());
  std::vector<Document> documents;
  documents.reserve(documentPaths.size());
  for (const auto& documentPath : documentPaths)
    documents.emplace_back(documentPath, readEntry(archive.get(), documentPath));

  validateDocuments(project.mainCircuit, documents);

  std::vector<ProjectAsset> assets;
  for (const auto& assetPath : assetEntries(archive.get()))
    assets.push_back({.path = assetPath,
                      .contents = readEntry(archive.get(), assetPath)});
  validateAssets(assets);

  return ProjectFile{.metadata  = std::move(metadata),
                     .project   = std::move(project),
                     .documents = std::move(documents),
                     .assets    = std::move(assets)};
}

void writeProjectFile(const std::filesystem::path& path, const ProjectFile& projectFile)
{
  validateDocuments(projectFile.project.mainCircuit, projectFile.documents);
  validateAssets(projectFile.assets);

  int       errorCode = 0;
  UniqueZip archive(
      zip_open(path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &errorCode));
  if (!archive) {
    throw std::runtime_error(std::format(
        "Cannot create Silicon project archive, ZIP error code: {}", errorCode));
  }

  addEntry(archive.get(), "mimetype", MIME_TYPE, true);
  addEntry(archive.get(), std::string(MetadataPath),
           metadataToJson(projectFile.metadata).dump(2));
  addEntry(archive.get(), std::string(ProjectPath),
           projectInfoToJson(projectFile.project).dump(2));

  for (const auto& document : projectFile.documents)
    addEntry(archive.get(), document.getPath(), document.getContents());
  for (const auto& asset : projectFile.assets)
    addEntry(archive.get(), asset.path, asset.contents);

  if (zip_close(archive.get()) != 0)
    throw std::runtime_error("Cannot finalize Silicon project archive");

  archive.release();
}

}  // namespace SILICON::project
