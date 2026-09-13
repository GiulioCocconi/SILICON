/*
 Copyright (c) 2026. Giulio Cocconi
 ...
 */

#include "projectContext.hpp"

#include <algorithm>
#include <format>
#include <ranges>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace SILICON::project {
namespace {

  void validateUniquePaths(const std::vector<Document>& documents)
  {
    std::unordered_set<std::string_view> paths;
    for (const auto& document : documents) {
      if (!paths.insert(document.getPath()).second)
        throw std::invalid_argument(
            std::format("Duplicate project document path: {}", document.getPath()));
    }
  }

}  // namespace

const DocumentStore& ProjectContext::documents() const noexcept
{
  return documents_;
}

const CircuitDependencyGraph& ProjectContext::circuitDependencies() const noexcept
{
  return circuitDependencies_;
}

void ProjectContext::setDocuments(std::vector<Document> nextDocuments)
{
  validateUniquePaths(nextDocuments);
  CircuitDependencyGraph nextDependencies;
  nextDependencies.rebuildFromProject(nextDocuments);
  commit(std::move(nextDocuments), std::move(nextDependencies),
         {.kind = DocumentChangeKind::Reset, .path = std::nullopt});
}

void ProjectContext::upsertDocument(Document document)
{
  auto       nextDocuments = documents_.getDocuments();
  const auto existing =
      std::ranges::find(nextDocuments, document.getPath(), &Document::getPath);
  const auto kind = existing == nextDocuments.end() ? DocumentChangeKind::Added
                                                    : DocumentChangeKind::Updated;
  const auto path = document.getPath();
  if (existing == nextDocuments.end())
    nextDocuments.push_back(std::move(document));
  else
    *existing = std::move(document);

  CircuitDependencyGraph nextDependencies;
  nextDependencies.rebuildFromProject(nextDocuments);
  commit(std::move(nextDocuments), std::move(nextDependencies),
         {.kind = kind, .path = path});
}

void ProjectContext::insertDocument(Document document, const std::size_t index)
{
  if (documents_.contains(document.getPath()))
    throw std::invalid_argument(
        std::format("Duplicate project document path: {}", document.getPath()));

  auto       nextDocuments = documents_.getDocuments();
  const auto path          = document.getPath();
  const auto offset        = std::min(index, nextDocuments.size());
  nextDocuments.insert(nextDocuments.begin() + static_cast<std::ptrdiff_t>(offset),
                       std::move(document));

  CircuitDependencyGraph nextDependencies;
  nextDependencies.rebuildFromProject(nextDocuments);
  commit(std::move(nextDocuments), std::move(nextDependencies),
         {.kind = DocumentChangeKind::Added, .path = path});
}

void ProjectContext::removeDocument(const std::string_view documentPath)
{
  if (!documents_.contains(documentPath))
    return;

  circuitDependencies_.validateDocumentRemoval(documentPath);
  auto nextDocuments = documents_.getDocuments();
  std::erase_if(nextDocuments, [&](const Document& document) {
    return document.getPath() == documentPath;
  });

  CircuitDependencyGraph nextDependencies;
  nextDependencies.rebuildFromProject(nextDocuments);
  commit(std::move(nextDocuments), std::move(nextDependencies),
         {.kind = DocumentChangeKind::Removed, .path = std::string(documentPath)});
}

void ProjectContext::commit(std::vector<Document>  nextDocuments,
                            CircuitDependencyGraph nextDependencies,
                            const DocumentChange&  change) noexcept
{
  circuitDependencies_ = std::move(nextDependencies);
  documents_.commitDocuments(std::move(nextDocuments), change);
}

}  // namespace SILICON::project
