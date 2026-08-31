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

  [[nodiscard]] bool isValidSlugPath(const std::string_view     path,
                                     const DocumentTypeInfo& info)
  {
    if (!path.starts_with(info.root) || !path.ends_with(info.suffix))
      return false;

    if (path.size() < info.root.size() + info.suffix.size())
      return false;

    return isValidDocumentSlug(path.substr(
        info.root.size(), path.size() - info.root.size() - info.suffix.size()));
  }

  void validateDocumentState(const DocumentType                type,
                             const std::optional<std::string>& coreCircuitJson)
  {
    if (!documentTypeInfo(type).isGraphical && coreCircuitJson)
      throw std::invalid_argument(
          "Non-graphical documents cannot contain core circuit JSON");
  }

}  // namespace

std::optional<DocumentType> documentTypeForPath(const std::string_view path)
{
  for (const auto& info : DOCUMENT_TYPE_INFO) {
    if (!path.starts_with(info.root))
      continue;

    if (info.usesSlug ? isValidSlugPath(path, info) : codeFileTypeForPath(path).has_value())
      return info.type;
  }

  return std::nullopt;
}

std::optional<std::string> documentSlugForPath(const std::string_view path)
{
  const auto type = documentTypeForPath(path);
  if (!type)
    return std::nullopt;

  const auto& info = documentTypeInfo(*type);
  if (!info.usesSlug)
    return std::nullopt;

  return std::string(path.substr(info.root.size(),
                                 path.size() - info.root.size() - info.suffix.size()));
}

bool isValidDocumentSlug(const std::string_view slug)
{
  return !slug.empty() && slug != "." && slug != ".." && !slug.contains('/')
         && !slug.contains('\\') && !containsControlCharacter(slug);
}

std::string documentPathForSlug(const DocumentType type, const std::string_view slug)
{
  const auto& info = documentTypeInfo(type);
  if (!info.usesSlug)
    throw std::invalid_argument("Document type does not use slugs");
  if (!isValidDocumentSlug(slug))
    throw std::invalid_argument("Invalid document slug");

  return std::format("{}{}{}", info.root, slug, info.suffix);
}

Document::Document(std::string path, std::string contents,
                   std::optional<std::string> coreCircuitJson)
  : path(std::move(path)),
    contents(std::move(contents)),
    coreCircuitJson(std::move(coreCircuitJson))
{
  const auto type = documentTypeForPath(this->path);
  if (!type)
    throw std::invalid_argument("Document path is invalid");
  validateDocumentState(*type, this->coreCircuitJson);
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
  listeners.notify(
      DocumentChange{.kind = DocumentChangeKind::Reset, .path = std::nullopt});
}

void DocumentStore::upsertDocument(Document document)
{
  const auto path = document.getPath();
  if (const auto index = indexOf(path)) {
    documents[*index] = std::move(document);
    listeners.notify(DocumentChange{.kind = DocumentChangeKind::Updated, .path = path});
    return;
  }

  documents.push_back(std::move(document));
  listeners.notify(DocumentChange{.kind = DocumentChangeKind::Added, .path = path});
}

void DocumentStore::insertDocument(Document document, const std::size_t index)
{
  const auto path = document.getPath();
  if (contains(path))
    throw std::invalid_argument(
        std::format("Duplicate project document path: {}", path));

  documents.insert(documents.begin() + std::min(index, documents.size()),
                   std::move(document));
  listeners.notify(DocumentChange{.kind = DocumentChangeKind::Added, .path = path});
}

void DocumentStore::removeDocument(const std::string_view documentPath)
{
  const auto oldSize = documents.size();
  std::erase_if(documents,
                [&](const Document& document) { return document.getPath() == documentPath; });

  if (documents.size() != oldSize)
    listeners.notify(DocumentChange{.kind = DocumentChangeKind::Removed,
                                    .path = std::string(documentPath)});
}

void DocumentStore::clear()
{
  documents.clear();
  listeners.notify(
      DocumentChange{.kind = DocumentChangeKind::Reset, .path = std::nullopt});
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

bool DocumentStore::contains(const DocumentType type) const noexcept
{
  return std::ranges::any_of(documents,
                             [type](const Document& document) {
                               return document.getType() == type;
                             });
}

const std::vector<Document>& DocumentStore::getDocuments() const noexcept
{
  return documents;
}

std::optional<std::size_t> DocumentStore::indexOf(const std::string_view documentPath) const
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
