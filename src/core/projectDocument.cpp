/*
 Copyright (c) 2026. Giulio Cocconi
 ...
 */

#include "projectDocument.hpp"

#include <algorithm>
#include <format>
#include <ranges>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace silicon::project {
namespace {

  std::optional<DocumentKind> classifyFlatJsonPath(const std::string_view path,
                                                   const std::string_view prefix,
                                                   const DocumentKind kind)
  {
    constexpr std::string_view suffix = ".json";
    if (!path.starts_with(prefix) || !path.ends_with(suffix)
        || path.find("..") != std::string_view::npos)
      return std::nullopt;
    const auto fileName = path.substr(prefix.size());
    if (fileName.size() <= suffix.size() || fileName.contains('/'))
      return std::nullopt;
    return kind;
  }

  void requireValidPath(const std::string_view path)
  {
    if (!classifyDocumentPath(path))
      throw std::invalid_argument("Document path must reference a valid project JSON entry");
  }

}  // namespace

std::optional<DocumentKind> classifyDocumentPath(const std::string_view path)
{
  if (auto kind = classifyFlatJsonPath(path, "circuits/", DocumentKind::Circuit))
    return kind;
  return classifyFlatJsonPath(path, "subcircuits/", DocumentKind::Subcircuit);
}

std::optional<std::string> subcircuitSlugForPath(const std::string_view path)
{
  if (classifyDocumentPath(path) != DocumentKind::Subcircuit)
    return std::nullopt;
  constexpr std::string_view prefix = "subcircuits/";
  constexpr std::string_view suffix = ".json";
  return std::string(path.substr(prefix.size(), path.size() - prefix.size()
                                                   - suffix.size()));
}

std::string subcircuitPathForSlug(const std::string_view slug)
{ return std::format("subcircuits/{}.json", slug); }

Document::Document(std::string path, std::string sceneJson,
                   std::optional<std::string> coreCircuitJson)
    : path_(std::move(path)),
      sceneJson_(std::move(sceneJson)),
      coreCircuitJson_(std::move(coreCircuitJson))
{
  requireValidPath(path_);
}

const std::string& Document::path() const
{ return path_; }

const std::string& Document::sceneJson() const
{ return sceneJson_; }

const std::optional<std::string>& Document::coreCircuitJson() const
{ return coreCircuitJson_; }

DocumentKind Document::kind() const
{ return *classifyDocumentPath(path_); }

std::optional<std::string> Document::subcircuitSlug() const
{ return subcircuitSlugForPath(path_); }

void Document::setSceneJson(std::string sceneJson,
                            std::optional<std::string> coreCircuitJson)
{
  sceneJson_       = std::move(sceneJson);
  coreCircuitJson_ = std::move(coreCircuitJson);
}

DocumentStore& DocumentStore::active()
{
  static DocumentStore store;
  return store;
}

void DocumentStore::setDocuments(std::vector<Document> documents)
{
  std::unordered_set<std::string> paths;
  for (const auto& document : documents) {
    if (!paths.insert(document.path()).second)
      throw std::invalid_argument(
          std::format("Duplicate project document path: {}", document.path()));
  }
  documents_ = std::move(documents);
  listeners_.notify({});
}

void DocumentStore::upsertDocument(Document document)
{
  if (auto index = indexOf(document.path())) {
    documents_[*index] = std::move(document);
    listeners_.notify(documents_[*index].path());
    return;
  }
  documents_.push_back(std::move(document));
  listeners_.notify(documents_.back().path());
}

void DocumentStore::insertDocument(Document document, const std::size_t index)
{
  if (contains(document.path()))
    throw std::invalid_argument(
        std::format("Duplicate project document path: {}", document.path()));
  const auto offset = std::min(index, documents_.size());
  auto       it     = documents_.insert(documents_.begin() + offset, std::move(document));
  listeners_.notify(it->path());
}

void DocumentStore::removeDocument(const std::string_view documentPath)
{
  const auto oldSize = documents_.size();
  std::erase_if(documents_, [&](const Document& document) {
    return document.path() == documentPath;
  });
  if (documents_.size() != oldSize)
    listeners_.notify(documentPath);
}

void DocumentStore::clear()
{
  documents_.clear();
  listeners_.notify({});
}

const Document* DocumentStore::find(const std::string_view documentPath) const
{
  const auto it = std::ranges::find(documents_, documentPath, &Document::path);
  return it == documents_.end() ? nullptr : &*it;
}

bool DocumentStore::contains(const std::string_view documentPath) const
{ return find(documentPath) != nullptr; }

std::vector<Document> DocumentStore::documents() const
{ return documents_; }

std::vector<Document> DocumentStore::documents(const DocumentKind kind) const
{
  return documents_ | std::views::filter([kind](const Document& document) {
           return document.kind() == kind;
         })
         | std::ranges::to<std::vector>();
}

std::optional<std::size_t> DocumentStore::indexOf(
    const std::string_view documentPath) const
{
  const auto it = std::ranges::find(documents_, documentPath, &Document::path);
  if (it == documents_.end())
    return std::nullopt;
  return static_cast<std::size_t>(std::distance(documents_.begin(), it));
}

std::uint64_t DocumentStore::addListener(Listener listener)
{ return listeners_.add(std::move(listener)); }

void DocumentStore::removeListener(const std::uint64_t id)
{ listeners_.remove(id); }

}  // namespace silicon::project
