/*
 Copyright (c) 2026. Giulio Cocconi
 ...
 */

#include <core/projectDocument.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

using namespace SILICON::core;
using namespace SILICON::project;

using SILICON::project::Document;
using SILICON::project::DocumentStore;
using SILICON::project::DocumentType;

TEST(ProjectDocumentTest, ClassifiesCanonicalFlatPaths)
{
  EXPECT_EQ(SILICON::project::documentTypeForPath("circuits/main.json"),
            DocumentType::Circuit);
  EXPECT_EQ(SILICON::project::documentTypeForPath("subcircuits/adder.json"),
            DocumentType::Subcircuit);
  EXPECT_EQ(SILICON::project::documentTypeForPath("code/adder.v"), DocumentType::Code);
  EXPECT_EQ(SILICON::project::documentTypeForPath("bin/firmware"), DocumentType::Binary);
  EXPECT_FALSE(SILICON::project::documentTypeForPath(""));
  EXPECT_FALSE(SILICON::project::documentTypeForPath("circuits/nested/main.json"));
  EXPECT_FALSE(SILICON::project::documentTypeForPath("circuits/main.txt"));
  EXPECT_EQ(SILICON::project::documentTypeForPath("circuits/foo..bar.json"),
            DocumentType::Circuit);
  EXPECT_FALSE(SILICON::project::documentTypeForPath("circuits/../main.json"));
  EXPECT_FALSE(SILICON::project::documentTypeForPath("subcircuits/...json"));
  EXPECT_EQ(SILICON::project::subcircuitSlugForPath("subcircuits/adder.json"), "adder");
  EXPECT_FALSE(SILICON::project::subcircuitSlugForPath("circuits/adder.json"));
  EXPECT_EQ(SILICON::project::binarySlugForPath("bin/firmware"), "firmware");
  EXPECT_FALSE(SILICON::project::binarySlugForPath("bin/nested/firmware"));
}

TEST(ProjectDocumentTest, ValidatesBinarySlugsAndRoundTripsExactNames)
{
  for (const std::string_view invalid : {"", ".", "..", "a/b", "a\\b", "line\nbreak"}) {
    EXPECT_FALSE(isValidBinarySlug(invalid));
    EXPECT_THROW(static_cast<void>(binaryPathForSlug(invalid)), std::invalid_argument);
  }
  for (const std::string_view valid : {"firmware", "rom.bin", "name with spaces"}) {
    const auto path = binaryPathForSlug(valid);
    ASSERT_TRUE(binarySlugForPath(path));
    EXPECT_EQ(*binarySlugForPath(path), valid);
  }
}

TEST(ProjectDocumentTest, ValidatesSubcircuitSlugsAndRoundTripsValidOnes)
{
  for (const std::string_view invalid :
       {"", ".", "..", "a/b", "a\\b", "../foo", "foo/bar", "line\nbreak"}) {
    EXPECT_FALSE(isValidSubcircuitSlug(invalid));
    EXPECT_THROW(static_cast<void>(subcircuitPathForSlug(invalid)),
                 std::invalid_argument);
  }

  for (const std::string_view valid : {"adder", "foo..bar", "name with spaces"}) {
    ASSERT_TRUE(isValidSubcircuitSlug(valid));
    const auto path = subcircuitPathForSlug(valid);
    ASSERT_TRUE(subcircuitSlugForPath(path));
    EXPECT_EQ(*subcircuitSlugForPath(path), valid);
  }
}

TEST(ProjectDocumentTest, RejectsCircuitOnlyStateForCodeDocuments)
{
  EXPECT_THROW(Document("code/adder.v", "module adder; endmodule", "{}"),
               std::invalid_argument);

  Document code("code/adder.v", "old");
  EXPECT_THROW(code.setContents("new", "{}"), std::invalid_argument);
  EXPECT_EQ(code.getContents(), "old");
  EXPECT_FALSE(code.getCoreCircuitJson());
}

TEST(ProjectDocumentTest, ValidatesAssetNamespaceOwnership)
{
  EXPECT_TRUE(isValidProjectAssetPath("assets/readme.txt"));
  EXPECT_TRUE(isValidProjectAssetPath("foo..bar/data.bin"));
  for (const std::string_view invalid :
       {"", "/absolute", "C:/absolute", "trailing/", "a//b", "a/./b", "a/../b", "a\\b",
        "mimetype", "metadata.json", "project.json", "circuits/other.bin",
        "subcircuits/nested/adder.json", "code/unsupported.sv", "bin/raw"})
    EXPECT_FALSE(isValidProjectAssetPath(invalid));
}

TEST(ProjectDocumentTest, ContentReplacementClearsOrReplacesPreparedCoreJson)
{
  Document document("subcircuits/adder.json", "old", "prepared");
  document.setContents("new");
  EXPECT_EQ(document.getContents(), "new");
  EXPECT_FALSE(document.getCoreCircuitJson());

  document.setContents("newer", "new prepared");
  ASSERT_TRUE(document.getCoreCircuitJson());
  EXPECT_EQ(*document.getCoreCircuitJson(), "new prepared");
}

TEST(ProjectDocumentTest, DescribesRegisteredCodeTypes)
{
  const auto registry = codeFileTypeRegistry();
  ASSERT_EQ(registry.size(), 1);
  EXPECT_EQ(registry.front().type, CodeFileType::Verilog);
  EXPECT_EQ(registry.front().displayName, "Verilog");
  EXPECT_EQ(registry.front().extension, ".v");
  EXPECT_EQ(registry.front().kdeSyntaxDefinition, "Verilog");
  EXPECT_EQ(codeFileTypeForPath("code/adder.v"), CodeFileType::Verilog);
  EXPECT_FALSE(codeFileTypeForPath("code/adder.sv"));
}

TEST(ProjectDocumentTest, ValidatesCodePaths)
{
  EXPECT_TRUE(isValidCodeFilePath("code/adder.v", CodeFileType::Verilog));
  EXPECT_FALSE(isValidCodeFilePath("code/adder.sv", CodeFileType::Verilog));
  EXPECT_FALSE(isValidCodeFilePath("code/nested/adder.v", CodeFileType::Verilog));
  EXPECT_FALSE(isValidCodeFilePath("code/../adder.v", CodeFileType::Verilog));
  EXPECT_EQ(codeFilePath("adder", CodeFileType::Verilog), "code/adder.v");
}

TEST(ProjectDocumentStoreTest, PreservesOrderAcrossMixedKindsAndUpserts)
{
  DocumentStore store;
  store.setDocuments({{"circuits/main.json", "main"},
                      {"subcircuits/adder.json", "adder"},
                      {"code/adder.v", "module adder; endmodule"},
                      {"bin/firmware", std::string("\0\xff", 2)},
                      {"circuits/control.json", "control"}});

  store.upsertDocument({"subcircuits/adder.json", "updated"});
  ASSERT_EQ(store.getDocuments().size(), 5);
  EXPECT_EQ(store.getDocuments()[1].getPath(), "subcircuits/adder.json");
  EXPECT_EQ(store.getDocuments()[1].getContents(), "updated");

  const auto circuits = store.getDocuments(DocumentType::Circuit);
  ASSERT_EQ(circuits.size(), 2);
  EXPECT_EQ(circuits[0].get().getPath(), "circuits/main.json");
  EXPECT_EQ(circuits[1].get().getPath(), "circuits/control.json");

  const auto codeFiles = store.getDocuments(DocumentType::Code);
  ASSERT_EQ(codeFiles.size(), 1);
  EXPECT_EQ(codeFiles.front().get().getContents(), "module adder; endmodule");
  EXPECT_EQ(codeFileTypeForPath(codeFiles.front().get().getPath()),
            CodeFileType::Verilog);

  const auto binaries = store.getDocuments(DocumentType::Binary);
  ASSERT_EQ(binaries.size(), 1);
  EXPECT_EQ(binaries.front().get().getContents(), std::string("\0\xff", 2));

  store.removeDocument("subcircuits/adder.json");
  EXPECT_FALSE(store.contains("subcircuits/adder.json"));
  EXPECT_EQ(store.indexOf("circuits/control.json"), 3);
}

TEST(ProjectDocumentStoreTest, RejectsDuplicatePaths)
{
  DocumentStore store;
  EXPECT_THROW(
      store.setDocuments({{"circuits/main.json", "one"}, {"circuits/main.json", "two"}}),
      std::invalid_argument);
}

TEST(ProjectDocumentStoreTest, NotificationsUseSnapshotAndCanonicalPaths)
{
  DocumentStore            store;
  std::vector<std::pair<DocumentChangeKind, std::optional<std::string>>> notifications;
  std::uint64_t            selfRemovingId = 0;
  std::uint64_t            addedId        = 0;

  selfRemovingId = store.addListener([&](const DocumentChange& change) {
    notifications.emplace_back(change.kind, change.path);
    store.removeListener(selfRemovingId);
    if (addedId == 0) {
      addedId = store.addListener([&](const DocumentChange& nextChange) {
        notifications.emplace_back(nextChange.kind, nextChange.path);
      });
    }
  });

  store.upsertDocument({"circuits/main.json", "{}"});
  EXPECT_EQ(notifications,
            (decltype(notifications){
                {DocumentChangeKind::Added, std::string("circuits/main.json")}}));

  store.upsertDocument({"circuits/main.json", "updated"});
  store.removeDocument("missing.json");
  store.removeDocument("circuits/main.json");

  store.clear();
  EXPECT_EQ(notifications,
            (decltype(notifications){
                {DocumentChangeKind::Added, std::string("circuits/main.json")},
                {DocumentChangeKind::Updated, std::string("circuits/main.json")},
                {DocumentChangeKind::Removed, std::string("circuits/main.json")},
                {DocumentChangeKind::Reset, std::nullopt}}));
  store.removeListener(addedId);
}
