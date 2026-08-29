/*
  Copyright (c) 2026. Giulio Cocconi

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.

 */

#include "projectDocument.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <ranges>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace SILICON::project {
namespace {

  [[nodiscard]] bool containsControlCharacter(const std::string_view value)
  {
    return std::ranges::any_of(value, [](const unsigned char character) {
      return character < 0x20 || character == 0x7f;
    });
  }

  [[nodiscard]] bool hasSafePathSegments(const std::string_view path)
  {
    std::size_t segmentStart = 0;
    while (segmentStart < path.size()) {
      const auto separator = path.find('/', segmentStart);
      const auto segment = path.substr(segmentStart,
                                       separator == std::string_view::npos
                                           ? path.size() - segmentStart
                                           : separator - segmentStart);
      if (segment.empty() || segment == "." || segment == "..")
        return false;
      if (separator == std::string_view::npos)
        break;
      segmentStart = separator + 1;
    }
    return true;
  }

  std::optional<DocumentType> classifyFlatJsonPath(const std::string_view path,
                                                   const std::string_view prefix,
                                                   const DocumentType     type)
  {
    constexpr std::string_view suffix = ".json";
    if (!path.starts_with(prefix) || !path.ends_with(suffix) || path.contains('\\')
        || containsControlCharacter(path) || !hasSafePathSegments(path))
      return std::nullopt;
    const auto fileName = path.substr(prefix.size());
    if (fileName.size() <= suffix.size() || fileName.contains('/'))
      return std::nullopt;
    const auto stem = fileName.substr(0, fileName.size() - suffix.size());
    if (stem == "." || stem == "..")
      return std::nullopt;
    return type;
  }

  void requireValidPath(const std::string_view path)
  {
    if (!documentTypeForPath(path))
      throw std::invalid_argument("Document path is invalid");
  }

  void validateDocumentState(
      const DocumentType type,
      const std::optional<std::string>& coreCircuitJson)
  {
    if (type == DocumentType::Code && coreCircuitJson)
      throw std::invalid_argument(
          "Code documents cannot contain core circuit JSON");
  }

}  // namespace

std::optional<DocumentType> documentTypeForPath(const std::string_view path)
{
  if (auto type = classifyFlatJsonPath(path, "circuits/", DocumentType::Circuit))
    return type;
  if (auto type =
          classifyFlatJsonPath(path, "subcircuits/", DocumentType::Subcircuit))
    return type;
  if (codeFileTypeForPath(path))
    return DocumentType::Code;
  return std::nullopt;
}

std::optional<std::string> subcircuitSlugForPath(const std::string_view path)
{
  if (documentTypeForPath(path) != DocumentType::Subcircuit)
    return std::nullopt;
  constexpr std::string_view prefix = "subcircuits/";
  constexpr std::string_view suffix = ".json";
  return std::string(
      path.substr(prefix.size(), path.size() - prefix.size() - suffix.size()));
}

bool isValidSubcircuitSlug(const std::string_view slug)
{
  return !slug.empty() && slug != "." && slug != ".." && !slug.contains('/')
         && !slug.contains('\\') && !containsControlCharacter(slug);
}

std::string subcircuitPathForSlug(const std::string_view slug)
{
  if (!isValidSubcircuitSlug(slug))
    throw std::invalid_argument("Invalid subcircuit slug");
  return std::format("subcircuits/{}.json", slug);
}

bool isValidProjectAssetPath(const std::string_view path)
{
  if (path.empty() || path.front() == '/' || path.back() == '/' || path.contains('\\')
      || containsControlCharacter(path) || !hasSafePathSegments(path)
      || (path.size() >= 3
          && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':'
          && path[2] == '/')
      || path == "mimetype" || path == "metadata.json" || path == "project.json")
    return false;

  const auto firstSeparator = path.find('/');
  const auto root = path.substr(0, firstSeparator);
  return root != "circuits" && root != "subcircuits" && root != "code";
}

Document::Document(std::string path, std::string contents,
                   std::optional<std::string> coreCircuitJson)
  : path(std::move(path)),
    contents(std::move(contents)),
    coreCircuitJson(std::move(coreCircuitJson))
{
  requireValidPath(this->path);
  validateDocumentState(getType(), this->coreCircuitJson);
}

const std::string& Document::getPath() const noexcept
{
  return path;
}

const std::string& Document::getContents() const noexcept
{
  return contents;
}

const std::optional<std::string>& Document::getCoreCircuitJson() const noexcept
{
  return coreCircuitJson;
}

DocumentType Document::getType() const noexcept
{
  return *documentTypeForPath(path);
}

std::optional<std::string> Document::subcircuitSlug() const
{
  return subcircuitSlugForPath(path);
}

void Document::setContents(std::string                contents,
                           std::optional<std::string> coreCircuitJson)
{
  validateDocumentState(getType(), coreCircuitJson);
  this->contents        = std::move(contents);
  this->coreCircuitJson = std::move(coreCircuitJson);
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
    if (!paths.insert(document.getPath()).second)
      throw std::invalid_argument(
          std::format("Duplicate project document path: {}", document.getPath()));
  }
  this->documents = std::move(documents);
  listeners.notify(DocumentChange{.kind = DocumentChangeKind::Reset, .path = std::nullopt});
}

void DocumentStore::upsertDocument(Document document)
{
  const auto path = document.getPath();
  if (auto index = indexOf(document.getPath())) {
    documents[*index] = std::move(document);
    listeners.notify(
        DocumentChange{.kind = DocumentChangeKind::Updated, .path = path});
    return;
  }
  documents.push_back(std::move(document));
  listeners.notify(DocumentChange{.kind = DocumentChangeKind::Added, .path = path});
}

void DocumentStore::insertDocument(Document document, const std::size_t index)
{
  const auto path = document.getPath();
  if (contains(document.getPath()))
    throw std::invalid_argument(
        std::format("Duplicate project document path: {}", document.getPath()));
  const auto offset = std::min(index, documents.size());
  documents.insert(documents.begin() + offset, std::move(document));
  listeners.notify(DocumentChange{.kind = DocumentChangeKind::Added, .path = path});
}

void DocumentStore::removeDocument(const std::string_view documentPath)
{
  const auto path    = std::string(documentPath);
  const auto oldSize = documents.size();
  std::erase_if(documents, [&](const Document& document) {
    return document.getPath() == documentPath;
  });
  if (documents.size() != oldSize)
    listeners.notify(DocumentChange{.kind = DocumentChangeKind::Removed, .path = path});
}

void DocumentStore::clear()
{
  documents.clear();
  listeners.notify(DocumentChange{.kind = DocumentChangeKind::Reset, .path = std::nullopt});
}

const Document* DocumentStore::find(const std::string_view documentPath) const noexcept
{
  const auto it = std::ranges::find(documents, documentPath, &Document::getPath);
  return it == documents.end() ? nullptr : &*it;
}

bool DocumentStore::contains(const std::string_view documentPath) const noexcept
{
  return find(documentPath) != nullptr;
}

const std::vector<Document>& DocumentStore::getDocuments() const noexcept
{
  return documents;
}

DocumentStore::DocumentReferences
DocumentStore::getDocuments(const DocumentType type) const
{
  DocumentReferences result;
  result.reserve(documents.size());
  for (const auto& document : documents) {
    if (document.getType() == type)
      result.emplace_back(document);
  }
  return result;
}

std::optional<std::size_t>
DocumentStore::indexOf(const std::string_view documentPath) const
{
  const auto it = std::ranges::find(documents, documentPath, &Document::getPath);
  if (it == documents.end())
    return std::nullopt;
  return static_cast<std::size_t>(std::distance(documents.begin(), it));
}

std::uint64_t DocumentStore::addListener(Listener listener)
{
  return listeners.add(std::move(listener));
}

void DocumentStore::removeListener(const std::uint64_t id)
{
  listeners.remove(id);
}

}  // namespace SILICON::project
