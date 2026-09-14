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

#include <nlohmann/json.hpp>

#include <core/memory.hpp>
#include <core/subcircuit.hpp>

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

  void rewriteDocumentReferences(Document& document, const DocumentType renamedType,
                                 const std::string_view oldSlug,
                                 const std::string_view newSlug,
                                 const bool             renamedDocument)
  {
    if (document.getType() != DocumentType::Circuit)
      return;

    auto  json    = nlohmann::ordered_json::parse(document.getContents());
    auto* circuit = &json;
    if (const auto it = json.find("circuit"); it != json.end() && it->is_object())
      circuit = &*it;

    bool changed = false;
    if (renamedDocument && renamedType == DocumentType::Circuit) {
      (*circuit)["name"] = std::string(newSlug);
      changed            = true;
    }

    const auto components = circuit->find("components");
    if (components != circuit->end() && components->is_array()) {
      for (auto& component : *components) {
        if (!component.is_object())
          continue;
        const auto type       = component.find("type");
        const auto properties = component.find("properties");
        if (type == component.end() || !type->is_string() || properties == component.end()
            || !properties->is_object())
          continue;

        const auto propertyName =
            renamedType == DocumentType::Circuit ? "slug" : "binaryContents";
        const auto expectedType = renamedType == DocumentType::Circuit
                                      ? SILICON::core::SubcircuitComponent::Type
                                      : SILICON::core::ROM::Type;
        auto       property     = properties->find(propertyName);
        if (type->get_ref<const std::string&>() == expectedType
            && property != properties->end() && property->is_string()
            && property->get_ref<const std::string&>() == oldSlug) {
          *property = std::string(newSlug);
          changed   = true;
        }
      }
    }

    if (changed)
      document.setContents(json.dump(2));
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

void ProjectContext::renameDocument(const std::string_view oldPath,
                                    const std::string_view newPath)
{
  const auto oldType = documentTypeForPath(oldPath);
  const auto newType = documentTypeForPath(newPath);
  if (!oldType || !newType || *oldType != *newType)
    throw std::invalid_argument("A document rename must preserve its document type");
  if (!documents_.contains(oldPath))
    throw std::invalid_argument(std::format("Unknown project document: {}", oldPath));
  if (oldPath == newPath)
    return;
  if (documents_.contains(newPath))
    throw std::invalid_argument(
        std::format("Project document already exists: {}", newPath));

  const auto oldSlug = documentSlugForPath(oldPath);
  const auto newSlug = documentSlugForPath(newPath);
  if (!oldSlug || !newSlug)
    throw std::invalid_argument("A document rename requires canonical document paths");

  auto nextDocuments = documents_.getDocuments();
  for (auto& document : nextDocuments) {
    const bool renamedDocument = document.getPath() == oldPath;
    if (renamedDocument)
      document = Document(std::string(newPath), document.getContents());
    if (*oldType != DocumentType::Verilog)
      rewriteDocumentReferences(document, *oldType, *oldSlug, *newSlug, renamedDocument);
  }

  CircuitDependencyGraph nextDependencies;
  nextDependencies.rebuildFromProject(nextDocuments);
  commit(std::move(nextDocuments), std::move(nextDependencies),
         {.kind = DocumentChangeKind::Reset, .path = std::nullopt});
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
