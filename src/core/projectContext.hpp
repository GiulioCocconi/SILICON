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
  DocumentStore          documents;
  CircuitDependencyGraph circuitDependencies;

  void setDocuments(std::vector<Document> documents);
  void upsertDocument(Document document);
  void insertDocument(Document document, std::size_t index);
  void removeDocument(std::string_view documentPath);

private:
  void commit(std::vector<Document> documents, CircuitDependencyGraph dependencies,
              const DocumentChange& change);
};

}  // namespace SILICON::project
