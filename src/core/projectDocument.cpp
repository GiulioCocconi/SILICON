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

#include <nlohmann/json.hpp>

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

  [[nodiscard]] std::string_view leafFileName(const std::string_view path)
  {
    const auto separator = path.find_last_of("/\\");
    return separator == std::string_view::npos ? path : path.substr(separator + 1);
  }

  [[nodiscard]] std::string lowerCaseExtension(const std::string_view fileName)
  {
    const auto dot = fileName.find_last_of('.');
    if (dot == std::string_view::npos || dot == 0)
      return {};

    auto extension = std::string(fileName.substr(dot));
    std::ranges::transform(extension, extension.begin(), [](const unsigned char value) {
      return static_cast<char>(std::tolower(value));
    });
    return extension;
  }

  [[nodiscard]] std::string fileStem(const std::string_view fileName)
  {
    const auto dot = fileName.find_last_of('.');
    if (dot == std::string_view::npos || dot == 0)
      return std::string(fileName);
    return std::string(fileName.substr(0, dot));
  }

  [[nodiscard]] bool isCircuitContents(const std::string_view contents)
  {
    const auto json = nlohmann::json::parse(contents, nullptr, false);
    if (!json.is_object())
      return false;

    const auto circuit = json.find("circuit");
    const auto visual  = json.find("visual");
    return (circuit != json.end() && circuit->is_object())
           || (visual != json.end() && visual->is_object());
  }

  [[nodiscard]] bool isTextContents(const std::string_view contents)
  {
    for (std::size_t index = 0; index < contents.size();) {
      const auto byte = static_cast<unsigned char>(contents[index]);
      if (byte < 0x80) {
        if ((byte < 0x20 && byte != '\t' && byte != '\n' && byte != '\r') || byte == 0x7f)
          return false;
        ++index;
        continue;
      }

      std::size_t   length;
      std::uint32_t codePoint;
      std::uint32_t minimumCodePoint;
      if (byte >= 0xc2 && byte <= 0xdf) {
        length           = 2;
        codePoint        = byte & 0x1f;
        minimumCodePoint = 0x80;
      } else if (byte >= 0xe0 && byte <= 0xef) {
        length           = 3;
        codePoint        = byte & 0x0f;
        minimumCodePoint = 0x800;
      } else if (byte >= 0xf0 && byte <= 0xf4) {
        length           = 4;
        codePoint        = byte & 0x07;
        minimumCodePoint = 0x10000;
      } else {
        return false;
      }

      if (index + length > contents.size())
        return false;
      for (std::size_t offset = 1; offset < length; ++offset) {
        const auto continuation = static_cast<unsigned char>(contents[index + offset]);
        if ((continuation & 0xc0) != 0x80)
          return false;
        codePoint = (codePoint << 6) | (continuation & 0x3f);
      }

      if (codePoint < minimumCodePoint
          || (length == 3 && codePoint >= 0xd800 && codePoint <= 0xdfff)
          || (length == 4 && codePoint > 0x10ffff))
        return false;
      index += length;
    }
    return true;
  }

  [[nodiscard]] std::optional<DocumentType>
  documentTypeForFile(const std::string_view fileName, const std::string_view contents)
  {
    const auto extension = lowerCaseExtension(fileName);
    if (extension == ".v")
      return DocumentType::Verilog;
    if (extension == ".bin")
      return DocumentType::RawBinary;

    if (isCircuitContents(contents))
      return DocumentType::Circuit;
    if (!contents.empty() && !isTextContents(contents))
      return DocumentType::RawBinary;
    return std::nullopt;
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

Document importDocument(const std::string_view sourcePath, std::string contents)
{
  const auto fileName = leafFileName(sourcePath);
  if (fileName.empty() || fileName == "." || fileName == "..")
    throw std::invalid_argument("Document file name is invalid");

  const auto type = documentTypeForFile(fileName, contents);
  if (!type)
    throw std::invalid_argument(std::format("Unsupported document file: {}", fileName));

  const auto slug =
      *type == DocumentType::RawBinary ? std::string(fileName) : fileStem(fileName);
  if (!isValidDocumentSlug(slug))
    throw std::invalid_argument("Document file name is invalid");

  return Document(documentPathForSlug(*type, slug), std::move(contents));
}

std::string documentFileName(const Document& document)
{
  return std::string(leafFileName(document.getPath()));
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

std::shared_ptr<const std::string> binaryContentsSnapshot(const DocumentStore& documents,
                                                          const std::string_view slug)
{
  if (!isValidDocumentSlug(slug))
    return nullptr;

  const auto* document =
      documents.find(documentPathForSlug(DocumentType::RawBinary, slug));
  if (!document)
    return nullptr;

  return std::make_shared<const std::string>(document->getContents());
}

}  // namespace SILICON::project
