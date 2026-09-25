/*
 Copyright (c) 2026. Giulio Cocconi
 ...
 */

#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include <core/circuitDependencyGraph.hpp>
#include <core/projectDocument.hpp>

namespace SILICON::project {

/** Project-owned document state and its derived Circuit dependency index. */
class ProjectContext {
public:
  [[nodiscard]] const DocumentStore&          documents() const noexcept;
  [[nodiscard]] const CircuitDependencyGraph& circuitDependencies() const noexcept;

  void setDocuments(std::vector<Document> documents);
  void upsertDocument(Document document);
  void insertDocument(Document document, std::size_t index);
  /** Renames a document and rewrites project document references to its slug. */
  void renameDocument(std::string_view oldPath, std::string_view newPath);
  void removeDocument(std::string_view documentPath);

private:
  DocumentStore          documents_;
  CircuitDependencyGraph circuitDependencies_;

  void commit(std::vector<Document> documents, CircuitDependencyGraph dependencies,
              const DocumentChange& change) noexcept;
};

}  // namespace SILICON::project
