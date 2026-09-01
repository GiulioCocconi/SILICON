/*
 Copyright (c) 2026. Giulio Cocconi
 ...
 */

#include <core/projectDocument.hpp>

#include <algorithm>
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
  EXPECT_EQ(SILICON::project::documentTypeForPath("code/adder.v"),
            DocumentType::Verilog);
  EXPECT_EQ(SILICON::project::documentTypeForPath("bin/firmware"),
            DocumentType::RawBinary);
  EXPECT_FALSE(SILICON::project::documentTypeForPath(""));
  EXPECT_FALSE(SILICON::project::documentTypeForPath("circuits/nested/main.json"));
  EXPECT_FALSE(SILICON::project::documentTypeForPath("circuits/main.txt"));
  EXPECT_EQ(SILICON::project::documentTypeForPath("circuits/foo..bar.json"),
            DocumentType::Circuit);
  EXPECT_FALSE(SILICON::project::documentTypeForPath("circuits/../main.json"));
  EXPECT_FALSE(SILICON::project::documentTypeForPath("subcircuits/adder.json"));
  EXPECT_EQ(SILICON::project::documentSlugForPath("circuits/adder.json"), "adder");
  EXPECT_EQ(SILICON::project::documentSlugForPath("bin/firmware"), "firmware");
  EXPECT_FALSE(SILICON::project::documentSlugForPath("bin/nested/firmware"));
  EXPECT_EQ(SILICON::project::documentSlugForPath("code/adder.v"), "adder");
}

TEST(ProjectDocumentTest, ClassifiesConcreteTypesByCategory)
{
  static_assert(categoryOf(DocumentType::Circuit) == DocumentCategory::Diagram);
  static_assert(categoryOf(DocumentType::Verilog) == DocumentCategory::Code);
  static_assert(categoryOf(DocumentType::RawBinary) == DocumentCategory::Binary);
  static_assert(kdeSyntaxDefinition(DocumentType::Verilog) == "Verilog");
  static_assert(!kdeSyntaxDefinition(DocumentType::Circuit));
  static_assert(!kdeSyntaxDefinition(DocumentType::RawBinary));
  static_assert(documentCategoryIconName(DocumentCategory::Diagram)
                == "circuit-board");
  static_assert(documentCategoryIconName(DocumentCategory::Code) == "code");
  static_assert(documentCategoryIconName(DocumentCategory::Binary) == "file");
}

TEST(ProjectDocumentTest, ValidatesBinarySlugsAndRoundTripsExactNames)
{
  for (const std::string_view invalid : {"", ".", "..", "a/b", "a\\b", "line\nbreak"}) {
    EXPECT_FALSE(isValidDocumentSlug(invalid));
    EXPECT_THROW(static_cast<void>(
                     documentPathForSlug(DocumentType::RawBinary, invalid)),
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
    EXPECT_THROW(static_cast<void>(
                     documentPathForSlug(DocumentType::Circuit, invalid)),
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

TEST(ProjectDocumentTest, ContentReplacementClearsOrReplacesPreparedCoreJson)
{
  Document document("circuits/adder.json", "old", "prepared");
  document.setContents("new");
  EXPECT_EQ(document.getContents(), "new");
  EXPECT_FALSE(document.getCoreCircuitJson());

  document.setContents("newer", "new prepared");
  ASSERT_TRUE(document.getCoreCircuitJson());
  EXPECT_EQ(*document.getCoreCircuitJson(), "new prepared");
}

TEST(ProjectDocumentTest, DescribesRegisteredDocumentTypes)
{
  ASSERT_EQ(DOCUMENT_TYPE_INFO.size(), 3);
  const auto& verilog = documentTypeInfo(DocumentType::Verilog);
  EXPECT_EQ(verilog.displayName, "Verilog");
  EXPECT_EQ(verilog.root, "code/");
  EXPECT_EQ(verilog.suffix, ".v");
  EXPECT_EQ(documentTypeForPath("code/adder.v"), DocumentType::Verilog);
  EXPECT_FALSE(documentTypeForPath("code/adder.sv"));
  EXPECT_FALSE(documentTypeForPath("code/nested/adder.v"));
  EXPECT_FALSE(documentTypeForPath("code/../adder.v"));
}

TEST(ProjectDocumentStoreTest, PreservesOrderAcrossMixedKindsAndUpserts)
{
  DocumentStore store;
  store.setDocuments({{"circuits/main.json", "main"},
                      {"circuits/adder.json", "adder"},
                      {"code/adder.v", "module adder; endmodule"},
                      {"bin/firmware", std::string("\0\xff", 2)},
                      {"circuits/control.json", "control"}});

  store.upsertDocument({"circuits/adder.json", "updated"});
  ASSERT_EQ(store.getDocuments().size(), 5);
  EXPECT_EQ(store.getDocuments()[1].getPath(), "circuits/adder.json");
  EXPECT_EQ(store.getDocuments()[1].getContents(), "updated");

  const auto& allDocuments = store.getDocuments();
  const auto  circuitCount = std::ranges::count_if(
      allDocuments,
      [](const auto& d) { return d.getType() == DocumentType::Circuit; });
  ASSERT_EQ(circuitCount, 3);
  EXPECT_TRUE(store.contains(DocumentType::Circuit));

  const auto codeIt = std::ranges::find_if(
      allDocuments,
      [](const auto& d) { return d.getType() == DocumentType::Verilog; });
  ASSERT_NE(codeIt, allDocuments.end());
  EXPECT_EQ(codeIt->getContents(), "module adder; endmodule");
  EXPECT_EQ(documentTypeForPath(codeIt->getPath()), DocumentType::Verilog);

  const auto binaryIt = std::ranges::find_if(
      allDocuments,
      [](const auto& d) { return d.getType() == DocumentType::RawBinary; });
  ASSERT_NE(binaryIt, allDocuments.end());
  EXPECT_EQ(binaryIt->getContents(), std::string("\0\xff", 2));

  store.removeDocument("circuits/adder.json");
  EXPECT_FALSE(store.contains("circuits/adder.json"));
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
