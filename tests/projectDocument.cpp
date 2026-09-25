/*
 Copyright (c) 2026. Giulio Cocconi
 ...
 */

#include <core/projectContext.hpp>
#include <core/projectDocument.hpp>
#include <core/isaArchitecture.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <sisl/sisl.hpp>

using namespace SILICON::core;
using namespace SILICON::project;

using SILICON::project::Document;
using SILICON::project::DocumentStore;
using SILICON::project::DocumentType;

static_assert(std::is_class_v<sisl::Isa>);

TEST(ProjectDocumentTest, ClassifiesCanonicalFlatPaths)
{
  EXPECT_EQ(SILICON::project::documentTypeForPath("circuits/main.json"),
            DocumentType::Circuit);
  EXPECT_EQ(SILICON::project::documentTypeForPath("code/adder.v"), DocumentType::Verilog);
  EXPECT_EQ(SILICON::project::documentTypeForPath("isa/rv32/instr_format.sisl"), DocumentType::Sisl);
  EXPECT_FALSE(SILICON::project::documentTypeForPath("code/rv32.isa"));
  EXPECT_EQ(SILICON::project::documentTypeForPath("bin/firmware"),
            DocumentType::RawBinary);
  EXPECT_FALSE(SILICON::project::documentTypeForPath(""));
  EXPECT_FALSE(SILICON::project::documentTypeForPath("circuits/nested/main.json"));
  EXPECT_FALSE(SILICON::project::documentTypeForPath("circuits/main.txt"));
  EXPECT_EQ(SILICON::project::documentTypeForPath("circuits/foo..bar.json"),
            DocumentType::Circuit);
  EXPECT_FALSE(SILICON::project::documentTypeForPath("circuits/../main.json"));
  EXPECT_EQ(SILICON::project::documentSlugForPath("circuits/adder.json"), "adder");
  EXPECT_EQ(SILICON::project::documentSlugForPath("bin/firmware"), "firmware");
  EXPECT_FALSE(SILICON::project::documentSlugForPath("bin/nested/firmware"));
  EXPECT_EQ(SILICON::project::documentSlugForPath("code/adder.v"), "adder");
}

TEST(ProjectDocumentTest, ClassifiesConcreteTypesByCategory)
{
  static_assert(categoryOf(DocumentType::Circuit) == DocumentCategory::Diagram);
  static_assert(categoryOf(DocumentType::Verilog) == DocumentCategory::Code);
  static_assert(categoryOf(DocumentType::Sisl) == DocumentCategory::Architecture);
  static_assert(categoryOf(DocumentType::RawBinary) == DocumentCategory::Binary);
}

TEST(ProjectDocumentTest, ValidatesBinarySlugsAndRoundTripsExactNames)
{
  for (const std::string_view invalid : {"", ".", "..", "a/b", "a\\b", "line\nbreak"}) {
    EXPECT_FALSE(isValidDocumentSlug(invalid));
    EXPECT_THROW(static_cast<void>(documentPathForSlug(DocumentType::RawBinary, invalid)),
                 std::invalid_argument);
  }
  for (const std::string_view valid : {"firmware", "rom.bin", "name with spaces"}) {
    const auto path = documentPathForSlug(DocumentType::RawBinary, valid);
    ASSERT_TRUE(documentSlugForPath(path));
    EXPECT_EQ(*documentSlugForPath(path), valid);
  }
}

TEST(ProjectDocumentTest, ValidatesDocumentNamesAndRoundTripsEveryType)
{
  for (const std::string_view invalid :
       {"", ".", "..", "a/b", "a\\b", "../foo", "foo/bar", "line\nbreak"}) {
    EXPECT_FALSE(isValidDocumentSlug(invalid));
    EXPECT_THROW(static_cast<void>(documentPathForSlug(DocumentType::Circuit, invalid)),
                 std::invalid_argument);
  }

  for (const std::string_view valid : {"adder", "foo..bar", "name with spaces"}) {
    ASSERT_TRUE(isValidDocumentSlug(valid));
    const auto path = documentPathForSlug(DocumentType::Circuit, valid);
    ASSERT_TRUE(documentSlugForPath(path));
    EXPECT_EQ(*documentSlugForPath(path), valid);
  }

  const auto verilogPath = documentPathForSlug(DocumentType::Verilog, "adder");
  EXPECT_EQ(verilogPath, "code/adder.v");
  EXPECT_EQ(documentSlugForPath(verilogPath), "adder");
  const auto sislPath = documentPathForSlug(DocumentType::Sisl, "rv32");
  EXPECT_EQ(sislPath, "isa/rv32/instr_format.sisl");
  EXPECT_EQ(documentSlugForPath(sislPath), "rv32");
}

TEST(ProjectDocumentTest, ContentReplacementUpdatesOnlyPersistedContents)
{
  Document document("circuits/adder.json", "old");
  document.setContents("new");
  EXPECT_EQ(document.getContents(), "new");
}

TEST(ProjectDocumentTest, ImportsDocumentsFromDefinitiveExtensions)
{
  const auto verilog = importDocument("/tmp/Adder.V", "not validated by extension");
  EXPECT_EQ(verilog.getType(), DocumentType::Verilog);
  EXPECT_EQ(verilog.getPath(), "code/Adder.v");
  EXPECT_EQ(verilog.getContents(), "not validated by extension");

  const auto sisl = importDocument("/tmp/format.SISL", "arch RV32 = {};\n");
  EXPECT_EQ(sisl.getType(), DocumentType::Sisl);
  EXPECT_EQ(sisl.getPath(), "isa/RV32/instr_format.sisl");
  EXPECT_EQ(sisl.getContents(), "arch RV32 = {};\n");

  const std::string bytes("\0\xfftext", 6);
  const auto        binary = importDocument("firmware.BIN", bytes);
  EXPECT_EQ(binary.getType(), DocumentType::RawBinary);
  EXPECT_EQ(binary.getPath(), "bin/firmware.BIN");
  EXPECT_EQ(binary.getContents(), bytes);
}

TEST(ProjectDocumentTest, ReadsArchitectureDeclarationWithoutCompilingTheBody)
{
  EXPECT_TRUE(isValidArchitectureName("RV32_I"));
  EXPECT_FALSE(isValidArchitectureName("instr"));
  EXPECT_FALSE(isValidArchitectureName("bad name"));
  EXPECT_EQ(declaredArchitectureName("// note\n/* comment */ arch /* gap */ RV32 = {"),
            "RV32");
  EXPECT_FALSE(declaredArchitectureName("// arch Wrong\nmodule X;"));
  EXPECT_NO_THROW(validateArchitectureDeclaration("RV32", "arch RV32 = {};"));
  EXPECT_THROW(validateArchitectureDeclaration("rv32", "arch RV32 = {};"),
               std::invalid_argument);
  EXPECT_EQ(renameArchitectureDeclaration("// note\narch RV32 = {};", "RV32", "RV64"),
            "// note\narch RV64 = {};");
  EXPECT_THROW((void)importDocument("broken.sisl", "module X;"),
               std::invalid_argument);
}

TEST(ProjectDocumentTest, ImportsDocumentsDetectedFromContents)
{
  const auto circuit = importDocument(
      "Adder.json", R"({"circuit":{"name":"Adder","components":[]},"visual":{}})");
  EXPECT_EQ(circuit.getType(), DocumentType::Circuit);
  EXPECT_EQ(circuit.getPath(), "circuits/Adder.json");

  const auto binary = importDocument("assets/rom.dat", std::string("\x89PNG\r\n", 6));
  EXPECT_EQ(binary.getType(), DocumentType::RawBinary);
  EXPECT_EQ(binary.getPath(), "bin/rom.dat");
}

TEST(ProjectDocumentTest, RejectsUnsupportedImportedDocuments)
{
  EXPECT_THROW((void)importDocument("notes.txt", "ordinary text"), std::invalid_argument);
  EXPECT_THROW((void)importDocument("alu.txt", "module alu; endmodule"),
               std::invalid_argument);
  EXPECT_THROW((void)importDocument("data.json", R"({"unrelated":true})"),
               std::invalid_argument);
  EXPECT_THROW((void)importDocument("", "module top; endmodule"), std::invalid_argument);
  EXPECT_THROW((void)importDocument(".v", "module top; endmodule"),
               std::invalid_argument);
}

TEST(ProjectDocumentTest, ExportsCanonicalDocumentLeafNames)
{
  EXPECT_EQ(documentFileName(Document("circuits/adder.json", "{}")), "adder.json");
  EXPECT_EQ(documentFileName(Document("code/adder.v", "")), "adder.v");
  EXPECT_EQ(documentFileName(Document("isa/rv32/instr_format.sisl", "")), "instr_format.sisl");
  EXPECT_EQ(documentFileName(Document("bin/firmware.bin", "")), "firmware.bin");
}

TEST(ProjectDocumentTest, DescribesRegisteredDocumentTypes)
{
  ASSERT_EQ(DOCUMENT_TYPE_INFO.size(), 4);
  const auto& verilog = documentTypeInfo(DocumentType::Verilog);
  EXPECT_EQ(verilog.category, DocumentCategory::Code);
  EXPECT_EQ(verilog.root, "code/");
  EXPECT_EQ(verilog.suffix, ".v");
  EXPECT_EQ(documentTypeForPath("code/adder.v"), DocumentType::Verilog);
  EXPECT_EQ(documentTypeForPath("isa/rv32/instr_format.sisl"), DocumentType::Sisl);
  EXPECT_FALSE(documentTypeForPath("code/adder.sv"));
  EXPECT_FALSE(documentTypeForPath("isa/rv32/other.sisl"));
  EXPECT_FALSE(documentTypeForPath("isa/../instr_format.sisl"));
  EXPECT_FALSE(documentTypeForPath("code/nested/adder.v"));
  EXPECT_FALSE(documentTypeForPath("code/../adder.v"));
}

TEST(ProjectDocumentStoreTest, ProjectContextsOwnIndependentDocumentStores)
{
  ProjectContext first;
  ProjectContext second;

  first.upsertDocument({"code/main.v", "first"});
  second.upsertDocument({"code/main.v", "second"});

  ASSERT_NE(first.documents().find("code/main.v"), nullptr);
  ASSERT_NE(second.documents().find("code/main.v"), nullptr);
  EXPECT_EQ(first.documents().find("code/main.v")->getContents(), "first");
  EXPECT_EQ(second.documents().find("code/main.v")->getContents(), "second");
}

TEST(ProjectDocumentStoreTest, PreservesOrderAcrossMixedKindsAndUpserts)
{
  ProjectContext project;
  project.setDocuments({{"circuits/main.json", "{}"},
                        {"circuits/adder.json", "{}"},
                        {"code/adder.v", "module adder; endmodule"},
                        {"bin/firmware", std::string("\0\xff", 2)},
                        {"circuits/control.json", "{}"}});

  project.upsertDocument({"circuits/adder.json", R"({"components":[]})"});
  const auto& store = project.documents();
  ASSERT_EQ(store.getDocuments().size(), 5);
  EXPECT_EQ(store.getDocuments()[1].getPath(), "circuits/adder.json");
  EXPECT_EQ(store.getDocuments()[1].getContents(), R"({"components":[]})");

  const auto& allDocuments = store.getDocuments();
  const auto  circuitCount = std::ranges::count_if(
      allDocuments, [](const auto& d) { return d.getType() == DocumentType::Circuit; });
  ASSERT_EQ(circuitCount, 3);
  EXPECT_TRUE(store.contains(DocumentType::Circuit));

  const auto codeIt = std::ranges::find_if(
      allDocuments, [](const auto& d) { return d.getType() == DocumentType::Verilog; });
  ASSERT_NE(codeIt, allDocuments.end());
  EXPECT_EQ(codeIt->getContents(), "module adder; endmodule");
  EXPECT_EQ(documentTypeForPath(codeIt->getPath()), DocumentType::Verilog);

  const auto binaryIt = std::ranges::find_if(
      allDocuments, [](const auto& d) { return d.getType() == DocumentType::RawBinary; });
  ASSERT_NE(binaryIt, allDocuments.end());
  EXPECT_EQ(binaryIt->getContents(), std::string("\0\xff", 2));

  project.removeDocument("circuits/adder.json");
  EXPECT_FALSE(store.contains("circuits/adder.json"));
  EXPECT_EQ(store.indexOf("circuits/control.json"), 3);
}

TEST(ProjectDocumentStoreTest, RejectsDuplicatePaths)
{
  ProjectContext project;
  EXPECT_THROW(
      project.setDocuments({{"circuits/main.json", "{}"}, {"circuits/main.json", "{}"}}),
      std::invalid_argument);
}

TEST(ProjectDocumentStoreTest, NotificationsUseSnapshotAndCanonicalPaths)
{
  ProjectContext       project;
  const DocumentStore& store = project.documents();
  std::vector<std::pair<DocumentChangeKind, std::optional<std::string>>> notifications;
  std::uint64_t selfRemovingId = 0;
  std::uint64_t addedId        = 0;

  selfRemovingId = store.addListener([&](const DocumentChange& change) {
    notifications.emplace_back(change.kind, change.path);
    store.removeListener(selfRemovingId);
    if (addedId == 0) {
      addedId = store.addListener([&](const DocumentChange& nextChange) {
        notifications.emplace_back(nextChange.kind, nextChange.path);
      });
    }
  });

  project.upsertDocument({"circuits/main.json", "{}"});
  EXPECT_EQ(notifications,
            (decltype(notifications){
                {DocumentChangeKind::Added, std::string("circuits/main.json")}}));

  project.upsertDocument({"circuits/main.json", R"({"components":[]})"});
  project.removeDocument("missing.json");
  project.removeDocument("circuits/main.json");

  project.setDocuments({});
  EXPECT_EQ(notifications,
            (decltype(notifications){
                {DocumentChangeKind::Added, std::string("circuits/main.json")},
                {DocumentChangeKind::Updated, std::string("circuits/main.json")},
                {DocumentChangeKind::Removed, std::string("circuits/main.json")},
                {DocumentChangeKind::Reset, std::nullopt}}));
  store.removeListener(addedId);
}

TEST(ProjectDocumentStoreTest, RenamesCircuitAndRewritesSubcircuitReferences)
{
  ProjectContext project;
  project.setDocuments(
      {{"circuits/main.json",
        R"({"circuit":{"name":"main","components":[{"type":"Subcircuit","properties":{"slug":"child"}},{"type":"ROM","properties":{"binaryContents":"child"}}]},"visual":{"components":[],"wires":[]}})"},
       {"circuits/child.json",
        R"({"circuit":{"name":"Child title","components":[]},"visual":{"components":[],"wires":[]}})"},
       {"bin/child", "binary"}});

  project.renameDocument("circuits/child.json", "circuits/renamed child.json");

  const auto& documents = project.documents().getDocuments();
  ASSERT_EQ(documents.size(), 3);
  EXPECT_EQ(documents[0].getPath(), "circuits/main.json");
  EXPECT_EQ(documents[1].getPath(), "circuits/renamed child.json");
  EXPECT_EQ(documents[2].getPath(), "bin/child");

  const auto main = nlohmann::json::parse(documents[0].getContents());
  EXPECT_EQ(main["circuit"]["components"][0]["properties"]["slug"], "renamed child");
  EXPECT_EQ(main["circuit"]["components"][1]["properties"]["binaryContents"], "child");

  const auto renamed = nlohmann::json::parse(documents[1].getContents());
  EXPECT_EQ(renamed["circuit"]["name"], "renamed child");
  EXPECT_EQ(project.circuitDependencies().dependentsOf("circuits/renamed child.json"),
            (CircuitDependencyGraph::DocumentPathList{"circuits/main.json"}));
}

TEST(ProjectDocumentStoreTest, RenamesBinaryAndRewritesRomReferences)
{
  ProjectContext project;
  project.setDocuments(
      {{"circuits/main.json",
        R"({"circuit":{"name":"main","components":[{"type":"ROM","properties":{"binaryContents":"program"}},{"type":"Subcircuit","properties":{"slug":"program"}}]},"visual":{"components":[],"wires":[]}})"},
       {"circuits/program.json",
        R"({"circuit":{"name":"program","components":[]},"visual":{"components":[],"wires":[]}})"},
       {"bin/program", "binary"}});

  project.renameDocument("bin/program", "bin/firmware");

  const auto& documents = project.documents().getDocuments();
  ASSERT_EQ(documents.size(), 3);
  EXPECT_EQ(documents[2].getPath(), "bin/firmware");
  EXPECT_EQ(documents[2].getContents(), "binary");
  const auto main = nlohmann::json::parse(documents[0].getContents());
  EXPECT_EQ(main["circuit"]["components"][0]["properties"]["binaryContents"], "firmware");
  EXPECT_EQ(main["circuit"]["components"][1]["properties"]["slug"], "program");
}

TEST(ProjectDocumentStoreTest, RenamesCodeWithoutChangingContents)
{
  ProjectContext project;
  project.setDocuments({{"code/old.v", "module old; endmodule"},
                        {"isa/old/instr_format.sisl", "arch Old = {};"}});

  project.renameDocument("code/old.v", "code/new.v");
  project.renameDocument("isa/old/instr_format.sisl", "isa/new/instr_format.sisl");

  ASSERT_EQ(project.documents().getDocuments().size(), 2);
  EXPECT_EQ(project.documents().getDocuments()[0].getPath(), "code/new.v");
  EXPECT_EQ(project.documents().getDocuments()[0].getContents(), "module old; endmodule");
  EXPECT_EQ(project.documents().getDocuments()[1].getPath(), "isa/new/instr_format.sisl");
  EXPECT_EQ(project.documents().getDocuments()[1].getContents(), "arch Old = {};");
}

TEST(ProjectDocumentStoreTest, RejectsInvalidDocumentRenames)
{
  ProjectContext project;
  project.setDocuments({{"circuits/main.json", R"({"name":"main","components":[]})"},
                        {"circuits/other.json", R"({"name":"other","components":[]})"}});

  EXPECT_THROW(project.renameDocument("circuits/missing.json", "circuits/new.json"),
               std::invalid_argument);
  EXPECT_THROW(project.renameDocument("circuits/main.json", "circuits/other.json"),
               std::invalid_argument);
  EXPECT_THROW(project.renameDocument("circuits/main.json", "code/main.v"),
               std::invalid_argument);
  EXPECT_THROW(project.renameDocument("circuits/missing.json", "circuits/missing.json"),
               std::invalid_argument);

  EXPECT_NO_THROW(project.renameDocument("circuits/main.json", "circuits/main.json"));
  EXPECT_EQ(project.documents().getDocuments().size(), 2);
}
