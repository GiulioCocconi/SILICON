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

  [[nodiscard]] bool isValidDocumentPath(const std::string_view  path,
                                         const DocumentTypeInfo& info)
  {
    if (!path.starts_with(info.root) || !path.ends_with(info.suffix))
      return false;

    if (path.size() < info.root.size() + info.suffix.size())
      return false;

    return isValidDocumentSlug(path.substr(
        info.root.size(), path.size() - info.root.size() - info.suffix.size()));
  }

}  // namespace

std::optional<DocumentType> documentTypeForPath(const std::string_view path)
{
  const auto it =
      std::ranges::find_if(DOCUMENT_TYPE_INFO, [path](const DocumentTypeInfo& info) {
        return isValidDocumentPath(path, info);
      });
  return it == DOCUMENT_TYPE_INFO.end() ? std::nullopt : std::optional(it->type);
}

std::optional<std::string> documentSlugForPath(const std::string_view path)
{
  const auto type = documentTypeForPath(path);
  if (!type)
    return std::nullopt;

  const auto& info = documentTypeInfo(*type);
  return std::string(
      path.substr(info.root.size(), path.size() - info.root.size() - info.suffix.size()));
}

bool isValidDocumentSlug(const std::string_view slug)
{
  return !slug.empty() && slug != "." && slug != ".." && !slug.contains('/')
         && !slug.contains('\\') && !containsControlCharacter(slug);
}

std::string documentPathForSlug(const DocumentType type, const std::string_view slug)
{
  const auto& info = documentTypeInfo(type);
  if (!isValidDocumentSlug(slug))
    throw std::invalid_argument("Invalid document slug");

  return std::format("{}{}{}", info.root, slug, info.suffix);
}

Document::Document(std::string path, std::string contents)
  : path(std::move(path)), contents(std::move(contents))
{
  const auto type = documentTypeForPath(this->path);
  if (!type)
    throw std::invalid_argument("Document path is invalid");
}

const std::string& Document::getPath() const noexcept
{
  return path;
}

const std::string& Document::getContents() const noexcept
{
  return contents;
}

DocumentType Document::getType() const noexcept
{
  return *documentTypeForPath(path);
}

void Document::setContents(std::string contents)
{
  this->contents = std::move(contents);
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
  return std::ranges::any_of(
      documents, [type](const Document& document) { return document.getType() == type; });
}

const std::vector<Document>& DocumentStore::getDocuments() const noexcept
{
  return documents;
}

std::optional<std::size_t>
DocumentStore::indexOf(const std::string_view documentPath) const
{
  const auto it = std::ranges::find(documents, documentPath, &Document::getPath);
  if (it == documents.end())
    return std::nullopt;
  return static_cast<std::size_t>(std::distance(documents.begin(), it));
}

std::uint64_t DocumentStore::addListener(Listener listener) const
{
  return listeners.add(std::move(listener));
}

void DocumentStore::removeListener(const std::uint64_t id) const
{
  listeners.remove(id);
}

void DocumentStore::commitDocuments(std::vector<Document> documents,
                                    const DocumentChange& change) noexcept
{
  this->documents.swap(documents);
  listeners.notify(change);
}

}  // namespace SILICON::project
