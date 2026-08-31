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

#include "tests.hpp"

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <core/serialization/projectFile.hpp>
#include <nlohmann/json.hpp>
#include <zip.h>

using namespace SILICON::core;
using namespace SILICON::project;

namespace {

// RAII wrappers for Libzip C-pointers to prevent test leaks on assertion failures
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

struct FileCleanup {
  std::filesystem::path filename;
  ~FileCleanup() { std::filesystem::remove(filename); }
};

std::filesystem::path tempProjectPath(const std::string_view testName)
{
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path()
         / std::format("silicon_{}_{}.sil", testName, stamp);
}

nlohmann::ordered_json validMetadata()
{
  return nlohmann::ordered_json{{"formatVersion", SILICON::project::FORMAT_VERSION},
                                {"siliconVersion", SILICON_VERSION},
                                {"creationDate", "2026-01-02T03:04:05Z"},
                                {"lastModify", "2026-01-02T03:04:05Z"}};
}

nlohmann::ordered_json validProject(
    const std::string_view mainCircuit = SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH)
{
  return nlohmann::ordered_json{
      {"name", "CPU demo"}, {"mainCircuit", mainCircuit}, {"description", ""}};
}

void addZipEntry(zip_t* archive, const std::string& name, const std::string_view contents)
{
  // freep=0 implies libzip will not free the buffer. This is safe here because
  // contents strings outlive zip_close() in the test's scope.
  UniqueZipSource source(zip_source_buffer(archive, contents.data(), contents.size(), 0));
  if (!source)
    throw std::runtime_error("zip_source_buffer failed");

  if (zip_file_add(archive, name.c_str(), source.get(),
                   ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8)
      < 0) {
    throw std::runtime_error("zip_file_add failed");
  }

  // Release source ownership to the archive now that it has been successfully added
  source.release();
}

void writeZip(const std::filesystem::path&                            path,
              const std::vector<std::pair<std::string, std::string>>& entries)
{
  int       errorCode = 0;
  UniqueZip archive(
      zip_open(path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &errorCode));
  ASSERT_NE(archive, nullptr);

  for (const auto& [name, contents] : entries) {
    addZipEntry(archive.get(), name, contents);
  }

  // zip_close commits and deletes the pointer.
  // If it fails, RAII will zip_discard. If it passes, we release ownership.
  ASSERT_EQ(zip_close(archive.get()), 0);
  archive.release();
}

std::string readZipEntry(const std::filesystem::path& path, const std::string& entryName)
{
  int       errorCode = 0;
  UniqueZip archive(zip_open(path.string().c_str(), ZIP_RDONLY, &errorCode));
  if (!archive)
    throw std::runtime_error("zip_open failed");

  const zip_int64_t index =
      zip_name_locate(archive.get(), entryName.c_str(), ZIP_FL_ENC_UTF_8);
  if (index < 0)
    throw std::runtime_error(std::format("zip_name_locate failed for {}", entryName));

  zip_stat_t stat;
  zip_stat_init(&stat);
  if (zip_stat_index(archive.get(), static_cast<zip_uint64_t>(index), 0, &stat) != 0)
    throw std::runtime_error("zip_stat_index failed");

  UniqueZipFile file(zip_fopen_index(archive.get(), static_cast<zip_uint64_t>(index), 0));
  if (!file)
    throw std::runtime_error("zip_fopen_index failed");

  std::string contents;
  // C++23 String creation optimization avoiding double-initialization
  contents.resize_and_overwrite(stat.size, [&](char* buf, std::size_t) {
    const auto bytesRead = zip_fread(file.get(), buf, stat.size);
    return static_cast<std::size_t>(std::max<zip_int64_t>(0, bytesRead));
  });

  return contents;
}

void readProjectFileIgnoringResult(const std::filesystem::path& path)
{
  [[maybe_unused]] const auto projectFile = SILICON::project::readProjectFile(path);
}

}  // namespace

TEST(ProjectFileTest, WritesAndReadsProjectArchive)
{
  EXPECT_EQ(SILICON::project::FORMAT_VERSION, 1);
  const auto  path = tempProjectPath("roundtrip");
  FileCleanup cleanup{path};

  SILICON::project::ProjectFile projectFile{
      .metadata        = {.formatVersion  = SILICON::project::FORMAT_VERSION,
                          .siliconVersion = SILICON_VERSION,
                          .creationDate   = "2026-01-02T03:04:05Z",
                          .lastModify     = "2026-01-02T03:05:06Z"},
      .project         = {.name        = "CPU demo",
                          .mainCircuit = std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH),
                          .description = "Demo project"},
      .documents       = {{std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH),
                           R"({"circuit":{},"visual":{"components":[],"wires":[]}})"}}};

  SILICON::project::writeProjectFile(path, projectFile);

  EXPECT_EQ(readZipEntry(path, "mimetype"), SILICON::project::MIME_TYPE);

  const auto metadataJson = nlohmann::json::parse(readZipEntry(path, "metadata.json"));
  EXPECT_EQ(metadataJson["formatVersion"], SILICON::project::FORMAT_VERSION);
  EXPECT_EQ(metadataJson["siliconVersion"], SILICON_VERSION);
  EXPECT_EQ(metadataJson["creationDate"], "2026-01-02T03:04:05Z");
  EXPECT_EQ(metadataJson["lastModify"], "2026-01-02T03:05:06Z");

  const auto projectJson = nlohmann::json::parse(readZipEntry(path, "project.json"));
  EXPECT_EQ(projectJson["name"], "CPU demo");
  EXPECT_EQ(projectJson["mainCircuit"], SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH);
  EXPECT_EQ(projectJson["description"], "Demo project");

  const auto loaded = SILICON::project::readProjectFile(path);
  EXPECT_EQ(loaded.project.name, "CPU demo");
  EXPECT_EQ(loaded.project.mainCircuit, SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH);
  ASSERT_EQ(loaded.documents.size(), 1);
  EXPECT_EQ(loaded.documents.front().getPath(),
            SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH);
  EXPECT_EQ(loaded.documents.front().getContents(),
            projectFile.documents.front().getContents());
}

TEST(ProjectFileTest, RejectsObsoleteProjectFields)
{
  const auto  path = tempProjectPath("obsolete_project_field");
  FileCleanup cleanup{path};
  auto        project = validProject();
  project["codeFiles"] = nlohmann::ordered_json::array();
  writeZip(path, {{"mimetype", std::string(SILICON::project::MIME_TYPE)},
                  {"metadata.json", validMetadata().dump(2)},
                  {"project.json", project.dump(2)},
                  {std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH), "{}"}});

  EXPECT_THROW(readProjectFileIgnoringResult(path), std::runtime_error);
}

TEST(ProjectFileTest, WritesAndReadsProjectArchiveWithMultipleCircuits)
{
  const auto  path = tempProjectPath("multi_circuit_roundtrip");
  FileCleanup cleanup{path};

  const std::string mainJson =
      R"({"circuit":{"name":"Main"},"visual":{"components":[],"wires":[]}})";
  const std::string controllerJson =
      R"({"circuit":{"name":"Controller"},"visual":{"components":[],"wires":[]}})";

  SILICON::project::ProjectFile projectFile{
      .metadata  = {.formatVersion  = SILICON::project::FORMAT_VERSION,
                    .siliconVersion = SILICON_VERSION,
                    .creationDate   = "2026-01-02T03:04:05Z",
                    .lastModify     = "2026-01-02T03:05:06Z"},
      .project   = {.name        = "CPU demo",
                    .mainCircuit = std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH),
                    .description = "Demo project"},
      .documents = {{std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH), mainJson},
                    {"circuits/controller.json", controllerJson}}};

  SILICON::project::writeProjectFile(path, projectFile);

  EXPECT_EQ(readZipEntry(path, SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH.data()),
            mainJson);
  EXPECT_EQ(readZipEntry(path, "circuits/controller.json"), controllerJson);

  const auto loaded = SILICON::project::readProjectFile(path);
  EXPECT_EQ(loaded.project.mainCircuit, SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH);
  ASSERT_EQ(loaded.documents.size(), 2);
  const auto mainIt = std::ranges::find(
      loaded.documents, std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH),
      &SILICON::project::Document::getPath);
  const auto controllerIt =
      std::ranges::find(loaded.documents, std::string("circuits/controller.json"),
                        &SILICON::project::Document::getPath);
  ASSERT_NE(mainIt, loaded.documents.end());
  ASSERT_NE(controllerIt, loaded.documents.end());
  EXPECT_EQ(mainIt->getContents(), mainJson);
  EXPECT_EQ(controllerIt->getContents(), controllerJson);
}

TEST(ProjectFileTest, RejectsEntriesCollidingWithCircuitNamespace)
{
  const auto  path = tempProjectPath("nested_circuit_entry");
  FileCleanup cleanup{path};

  writeZip(path, {{"mimetype", std::string(SILICON::project::MIME_TYPE)},
                  {"metadata.json", validMetadata().dump(2)},
                  {"project.json", validProject().dump(2)},
                  {std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH), "{}"},
                  {"circuits/nested/controller.json", "{}"}});

  EXPECT_THROW(readProjectFileIgnoringResult(path), std::runtime_error);
}

TEST(ProjectFileTest, WritesAndReadsMixedCircuitAndSubcircuitDocuments)
{
  const auto        path = tempProjectPath("mixed_documents");
  FileCleanup       cleanup{path};
  const std::string mainJson = R"({"circuit":{"name":"Main"}})";
  const std::string subJson  = R"({"circuit":{"name":"Adder"}})";

  SILICON::project::ProjectFile projectFile{
      .metadata  = {.formatVersion  = SILICON::project::FORMAT_VERSION,
                    .siliconVersion = SILICON_VERSION,
                    .creationDate   = "2026-01-02T03:04:05Z",
                    .lastModify     = "2026-01-02T03:05:06Z"},
      .project   = {.name        = "Mixed",
                    .mainCircuit = std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH),
                    .description = ""},
      .documents = {{std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH), mainJson},
                    {"subcircuits/adder.json", subJson}}};

  SILICON::project::writeProjectFile(path, projectFile);
  EXPECT_EQ(readZipEntry(path, "subcircuits/adder.json"), subJson);

  const auto loaded = SILICON::project::readProjectFile(path);
  ASSERT_EQ(loaded.documents.size(), 2);
  EXPECT_EQ(loaded.documents[0].getType(), SILICON::project::DocumentType::Circuit);
  EXPECT_EQ(loaded.documents[1].getType(), SILICON::project::DocumentType::Subcircuit);
  EXPECT_FALSE(loaded.documents[1].getCoreCircuitJson());
}

TEST(ProjectFileTest, RoundTripsCodeDocuments)
{
  const auto        path = tempProjectPath("code_files");
  FileCleanup       cleanup{path};
  const std::string mainJson = R"({"circuit":{"name":"Main"}})";
  const std::string source = "module adder(input a, output y); assign y = a; endmodule\n";

  SILICON::project::ProjectFile projectFile{
      .metadata  = SILICON::project::metadataForNewFile(),
      .project   = {.name        = "Code",
                    .mainCircuit = std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH),
                    .description = ""},
      .documents = {{std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH), mainJson},
                    {"code/adder.v", source},
                    {"code/control.v", "module control; endmodule\n"}}};

  SILICON::project::writeProjectFile(path, projectFile);
  EXPECT_EQ(readZipEntry(path, "code/adder.v"), source);
  const auto projectJson = nlohmann::json::parse(readZipEntry(path, "project.json"));
  EXPECT_FALSE(projectJson.contains("codeFiles"));

  const auto loaded = SILICON::project::readProjectFile(path);
  ASSERT_EQ(loaded.documents.size(), 3);
  EXPECT_EQ(loaded.documents[1].getType(), SILICON::project::DocumentType::Code);
  EXPECT_EQ(loaded.documents[1].getContents(), source);
}

TEST(ProjectFileTest, RoundTripsRawBinaryDocumentsByteForByte)
{
  const auto                    path = tempProjectPath("binary_files");
  FileCleanup                   cleanup{path};
  const std::string             raw("\0\x01\x7f\x80\xff", 5);
  SILICON::project::ProjectFile projectFile{
      .metadata  = SILICON::project::metadataForNewFile(),
      .project   = {.name        = "Binary",
                    .mainCircuit = std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH),
                    .description = ""},
      .documents = {{std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH), "{}"},
                    {"bin/firmware", raw}}};

  SILICON::project::writeProjectFile(path, projectFile);
  EXPECT_EQ(readZipEntry(path, "bin/firmware"), raw);
  const auto loaded = SILICON::project::readProjectFile(path);
  ASSERT_EQ(loaded.documents.size(), 2);
  EXPECT_EQ(loaded.documents[1].getType(), SILICON::project::DocumentType::Binary);
  EXPECT_EQ(loaded.documents[1].getContents(), raw);
}

TEST(ProjectFileTest, RejectsNonDocumentEntries)
{
  const auto  assetPath = tempProjectPath("asset_entry");
  FileCleanup assetCleanup{assetPath};
  writeZip(assetPath, {{"mimetype", std::string(SILICON::project::MIME_TYPE)},
                       {"metadata.json", validMetadata().dump(2)},
                       {"project.json", validProject().dump(2)},
                       {std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH), "{}"},
                       {"notes/readme.txt", "preserved"}});
  EXPECT_THROW(readProjectFileIgnoringResult(assetPath), std::runtime_error);

  const auto  collisionPath = tempProjectPath("asset_namespace_collision");
  FileCleanup collisionCleanup{collisionPath};
  writeZip(collisionPath,
           {{"mimetype", std::string(SILICON::project::MIME_TYPE)},
            {"metadata.json", validMetadata().dump(2)},
            {"project.json", validProject().dump(2)},
            {std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH), "{}"},
            {"code/adder.sv", "unsupported"}});
  EXPECT_THROW(readProjectFileIgnoringResult(collisionPath), std::runtime_error);
}

TEST(ProjectFileTest, RejectsInvalidEntriesInsideCodeNamespace)
{
  for (const auto& entry :
       {std::string("code/nested/adder.v"), std::string("code/../adder.v")}) {
    const auto  path = tempProjectPath("invalid_code_path");
    FileCleanup cleanup{path};
    writeZip(path, {{"mimetype", std::string(SILICON::project::MIME_TYPE)},
                    {"metadata.json", validMetadata().dump(2)},
                    {"project.json", validProject().dump(2)},
                    {std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH), "{}"},
                    {entry, "module adder; endmodule"}});
    EXPECT_THROW(readProjectFileIgnoringResult(path), std::runtime_error);
  }
}

TEST(ProjectFileTest, RejectsNestedSubcircuitPathBeforeCreatingArchive)
{
  const auto                    path = tempProjectPath("nested_subcircuit");
  FileCleanup                   cleanup{path};
  SILICON::project::ProjectFile projectFile{
      .metadata        = SILICON::project::metadataForNewFile(),
      .project         = {.name        = "Invalid",
                          .mainCircuit = std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH),
                          .description = ""},
      .documents       = {{std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH), "{}"}}};

  EXPECT_THROW(projectFile.documents.emplace_back("subcircuits/nested/adder.json", "{}"),
               std::invalid_argument);
}

TEST(ProjectFileTest, RejectsEntriesCollidingWithSubcircuitNamespace)
{
  const auto  path = tempProjectPath("nested_subcircuit_entry");
  FileCleanup cleanup{path};

  writeZip(path, {{"mimetype", std::string(SILICON::project::MIME_TYPE)},
                  {"metadata.json", validMetadata().dump(2)},
                  {"project.json", validProject().dump(2)},
                  {std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH), "{}"},
                  {"subcircuits/nested/adder.json", "{}"}});

  EXPECT_THROW(readProjectFileIgnoringResult(path), std::runtime_error);
}

TEST(ProjectFileTest, RejectsDuplicateDocumentPathsBeforeCreatingArchive)
{
  const auto                    path = tempProjectPath("duplicate_document_paths");
  FileCleanup                   cleanup{path};
  SILICON::project::ProjectFile projectFile{
      .metadata        = SILICON::project::metadataForNewFile(),
      .project         = {.name        = "Invalid",
                          .mainCircuit = std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH),
                          .description = ""},
      .documents       = {{std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH), "{}"},
                          {std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH), "{}"}}};

  EXPECT_THROW(SILICON::project::writeProjectFile(path, projectFile), std::runtime_error);
  EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(ProjectFileTest, RejectsDuplicateSubcircuitSlugsBeforeCreatingArchive)
{
  const auto                    path = tempProjectPath("duplicate_subcircuit_slugs");
  FileCleanup                   cleanup{path};
  SILICON::project::ProjectFile projectFile{
      .metadata        = SILICON::project::metadataForNewFile(),
      .project         = {.name        = "Invalid",
                          .mainCircuit = std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH),
                          .description = ""},
      .documents       = {{std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH), "{}"},
                          {"subcircuits/adder.json", "{}"},
                          {"subcircuits/adder.json", "{}"}}};

  EXPECT_THROW(SILICON::project::writeProjectFile(path, projectFile), std::runtime_error);
  EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(ProjectFileTest, RejectsWrongMimetype)
{
  const auto  path = tempProjectPath("wrong_mimetype");
  FileCleanup cleanup{path};

  writeZip(path, {{"mimetype", "application/octet-stream"},
                  {"metadata.json", validMetadata().dump(2)},
                  {"project.json", validProject().dump(2)},
                  {std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH), "{}"}});

  EXPECT_THROW(readProjectFileIgnoringResult(path), std::runtime_error);
}

TEST(ProjectFileTest, RejectsRawJsonFile)
{
  const auto  path = tempProjectPath("raw_json");
  FileCleanup cleanup{path};

  std::ofstream file(path);
  file << R"({"circuit":{},"visual":{}})";
  file.close();

  EXPECT_THROW(readProjectFileIgnoringResult(path), std::runtime_error);
}

TEST(ProjectFileTest, RejectsMissingProjectJson)
{
  const auto  path = tempProjectPath("missing_project");
  FileCleanup cleanup{path};

  writeZip(path, {{"mimetype", std::string(SILICON::project::MIME_TYPE)},
                  {"metadata.json", validMetadata().dump(2)},
                  {std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH), "{}"}});

  EXPECT_THROW(readProjectFileIgnoringResult(path), std::runtime_error);
}

TEST(ProjectFileTest, RejectsMissingMetadataJson)
{
  const auto  path = tempProjectPath("missing_metadata");
  FileCleanup cleanup{path};

  writeZip(path, {{"mimetype", std::string(SILICON::project::MIME_TYPE)},
                  {"project.json", validProject().dump(2)},
                  {std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH), "{}"}});

  EXPECT_THROW(readProjectFileIgnoringResult(path), std::runtime_error);
}

TEST(ProjectFileTest, RejectsMissingMainCircuit)
{
  const auto  path = tempProjectPath("missing_circuit");
  FileCleanup cleanup{path};

  writeZip(path, {{"mimetype", std::string(SILICON::project::MIME_TYPE)},
                  {"metadata.json", validMetadata().dump(2)},
                  {"project.json", validProject().dump(2)}});

  EXPECT_THROW(readProjectFileIgnoringResult(path), std::runtime_error);
}

TEST(ProjectFileTest, RejectsMainCircuitOutsideCircuitsDirectory)
{
  const auto  path = tempProjectPath("main_outside_circuits");
  FileCleanup cleanup{path};

  writeZip(path, {{"mimetype", std::string(SILICON::project::MIME_TYPE)},
                  {"metadata.json", validMetadata().dump(2)},
                  {"project.json", validProject("main.json").dump(2)},
                  {std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH), "{}"}});

  EXPECT_THROW(readProjectFileIgnoringResult(path), std::runtime_error);
}

TEST(ProjectFileTest, RejectsMainCircuitNotPresentInArchive)
{
  const auto  path = tempProjectPath("main_not_present");
  FileCleanup cleanup{path};

  writeZip(path, {{"mimetype", std::string(SILICON::project::MIME_TYPE)},
                  {"metadata.json", validMetadata().dump(2)},
                  {"project.json", validProject("circuits/controller.json").dump(2)},
                  {std::string(SILICON::project::DEFAULT_MAIN_CIRCUIT_PATH), "{}"},
                  {"circuits/io.json", "{}"}});

  EXPECT_THROW(readProjectFileIgnoringResult(path), std::runtime_error);
}
