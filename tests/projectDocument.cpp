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

using silicon::project::Document;
using silicon::project::DocumentKind;
using silicon::project::DocumentStore;

TEST(ProjectDocumentTest, ClassifiesCanonicalFlatPaths)
{
  EXPECT_EQ(silicon::project::classifyDocumentPath("circuits/main.json"),
            DocumentKind::Circuit);
  EXPECT_EQ(silicon::project::classifyDocumentPath("subcircuits/adder.json"),
            DocumentKind::Subcircuit);
  EXPECT_FALSE(silicon::project::classifyDocumentPath(""));
  EXPECT_FALSE(silicon::project::classifyDocumentPath("circuits/nested/main.json"));
  EXPECT_FALSE(silicon::project::classifyDocumentPath("circuits/main.txt"));
  EXPECT_EQ(silicon::project::subcircuitSlugForPath("subcircuits/adder.json"),
            "adder");
  EXPECT_FALSE(silicon::project::subcircuitSlugForPath("circuits/adder.json"));
}

TEST(ProjectDocumentTest, SceneReplacementClearsOrReplacesPreparedCoreJson)
{
  Document document("subcircuits/adder.json", "old", "prepared");
  document.setSceneJson("new");
  EXPECT_EQ(document.sceneJson(), "new");
  EXPECT_FALSE(document.coreCircuitJson());

  document.setSceneJson("newer", "new prepared");
  ASSERT_TRUE(document.coreCircuitJson());
  EXPECT_EQ(*document.coreCircuitJson(), "new prepared");
}

TEST(ProjectDocumentStoreTest, PreservesOrderAcrossMixedKindsAndUpserts)
{
  DocumentStore store;
  store.setDocuments({{"circuits/main.json", "main"},
                      {"subcircuits/adder.json", "adder"},
                      {"circuits/control.json", "control"}});

  store.upsertDocument({"subcircuits/adder.json", "updated"});
  ASSERT_EQ(store.documents().size(), 3);
  EXPECT_EQ(store.documents()[1].path(), "subcircuits/adder.json");
  EXPECT_EQ(store.documents()[1].sceneJson(), "updated");

  const auto circuits = store.documents(DocumentKind::Circuit);
  ASSERT_EQ(circuits.size(), 2);
  EXPECT_EQ(circuits[0].path(), "circuits/main.json");
  EXPECT_EQ(circuits[1].path(), "circuits/control.json");

  store.removeDocument("subcircuits/adder.json");
  EXPECT_FALSE(store.contains("subcircuits/adder.json"));
  EXPECT_EQ(store.indexOf("circuits/control.json"), 1);
}

TEST(ProjectDocumentStoreTest, RejectsDuplicatePaths)
{
  DocumentStore store;
  EXPECT_THROW(store.setDocuments({{"circuits/main.json", "one"},
                                   {"circuits/main.json", "two"}}),
               std::invalid_argument);
}

TEST(ProjectDocumentStoreTest, NotificationsUseSnapshotAndCanonicalPaths)
{
  DocumentStore store;
  std::vector<std::string> notifications;
  std::uint64_t selfRemovingId = 0;
  std::uint64_t addedId        = 0;

  selfRemovingId = store.addListener([&](const std::string_view path) {
    notifications.emplace_back(path);
    store.removeListener(selfRemovingId);
    if (addedId == 0) {
      addedId = store.addListener(
          [&](const std::string_view nextPath) {
            notifications.emplace_back("late:" + std::string(nextPath));
          });
    }
  });

  store.upsertDocument({"circuits/main.json", "{}"});
  EXPECT_EQ(notifications, (std::vector<std::string>{"circuits/main.json"}));

  store.clear();
  EXPECT_EQ(notifications,
            (std::vector<std::string>{"circuits/main.json", "late:"}));
  store.removeListener(addedId);
}
