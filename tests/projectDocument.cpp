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
using SILICON::project::DocumentType;
using SILICON::project::DocumentStore;

TEST(ProjectDocumentTest, ClassifiesCanonicalFlatPaths)
{
  EXPECT_EQ(SILICON::project::documentTypeForPath("circuits/main.json"),
            DocumentType::Circuit);
  EXPECT_EQ(SILICON::project::documentTypeForPath("subcircuits/adder.json"),
            DocumentType::Subcircuit);
  EXPECT_EQ(SILICON::project::documentTypeForPath("code/adder.v"),
            DocumentType::Code);
  EXPECT_FALSE(SILICON::project::documentTypeForPath(""));
  EXPECT_FALSE(SILICON::project::documentTypeForPath("circuits/nested/main.json"));
  EXPECT_FALSE(SILICON::project::documentTypeForPath("circuits/main.txt"));
  EXPECT_EQ(SILICON::project::subcircuitSlugForPath("subcircuits/adder.json"), "adder");
  EXPECT_FALSE(SILICON::project::subcircuitSlugForPath("circuits/adder.json"));
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
                      {"circuits/control.json", "control"}});

  store.upsertDocument({"subcircuits/adder.json", "updated"});
  ASSERT_EQ(store.getDocuments().size(), 4);
  EXPECT_EQ(store.getDocuments()[1].getPath(), "subcircuits/adder.json");
  EXPECT_EQ(store.getDocuments()[1].getContents(), "updated");

  const auto circuits = store.getDocuments(DocumentType::Circuit);
  ASSERT_EQ(circuits.size(), 2);
  EXPECT_EQ(circuits[0].getPath(), "circuits/main.json");
  EXPECT_EQ(circuits[1].getPath(), "circuits/control.json");

  const auto codeFiles = store.getDocuments(DocumentType::Code);
  ASSERT_EQ(codeFiles.size(), 1);
  EXPECT_EQ(codeFiles.front().getContents(), "module adder; endmodule");
  EXPECT_EQ(codeFileTypeForPath(codeFiles.front().getPath()), CodeFileType::Verilog);

  store.removeDocument("subcircuits/adder.json");
  EXPECT_FALSE(store.contains("subcircuits/adder.json"));
  EXPECT_EQ(store.indexOf("circuits/control.json"), 2);
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
  std::vector<std::string> notifications;
  std::uint64_t            selfRemovingId = 0;
  std::uint64_t            addedId        = 0;

  selfRemovingId = store.addListener([&](const std::string_view path) {
    notifications.emplace_back(path);
    store.removeListener(selfRemovingId);
    if (addedId == 0) {
      addedId = store.addListener([&](const std::string_view nextPath) {
        notifications.emplace_back("late:" + std::string(nextPath));
      });
    }
  });

  store.upsertDocument({"circuits/main.json", "{}"});
  EXPECT_EQ(notifications, (std::vector<std::string>{"circuits/main.json"}));

  store.clear();
  EXPECT_EQ(notifications, (std::vector<std::string>{"circuits/main.json", "late:"}));
  store.removeListener(addedId);
}
