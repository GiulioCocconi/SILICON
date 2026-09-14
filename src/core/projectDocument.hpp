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

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <core/callbackRegistry.hpp>

namespace SILICON::project {

class ProjectContext;

enum class DocumentType { Circuit, Verilog, RawBinary };

enum class DocumentCategory { Diagram, Code, Binary };

struct DocumentTypeInfo {
  DocumentType     type;
  DocumentCategory category;
  std::string_view root;
  std::string_view suffix;
};

inline constexpr std::array<DocumentTypeInfo, 3> DOCUMENT_TYPE_INFO{{
    {.type     = DocumentType::Circuit,
     .category = DocumentCategory::Diagram,
     .root     = "circuits/",
     .suffix   = ".json"},
    {.type     = DocumentType::Verilog,
     .category = DocumentCategory::Code,
     .root     = "code/",
     .suffix   = ".v"},
    {.type     = DocumentType::RawBinary,
     .category = DocumentCategory::Binary,
     .root     = "bin/",
     .suffix   = {}},
}};

[[nodiscard]] constexpr const DocumentTypeInfo& documentTypeInfo(const DocumentType type)
{
  for (const auto& info : DOCUMENT_TYPE_INFO) {
    if (info.type == type)
      return info;
  }
  throw std::invalid_argument("Unknown document type");
}

[[nodiscard]] constexpr DocumentCategory categoryOf(const DocumentType type)
{
  return documentTypeInfo(type).category;
}

enum class DocumentChangeKind { Added, Updated, Removed, Reset };

struct DocumentChange {
  DocumentChangeKind         kind;
  std::optional<std::string> path;

  bool operator==(const DocumentChange&) const = default;
};

[[nodiscard]] std::optional<DocumentType> documentTypeForPath(std::string_view path);
[[nodiscard]] std::optional<std::string>  documentSlugForPath(std::string_view path);
[[nodiscard]] bool                        isValidDocumentSlug(std::string_view slug);
[[nodiscard]] std::string documentPathForSlug(DocumentType type, std::string_view slug);

/**
 * A document always has a valid, immutable project-relative path. Its type is
 * derived from that path. Contents are the only persisted representation.
 */
class Document {
public:
  Document(std::string path, std::string contents);

  [[nodiscard]] const std::string& getPath() const noexcept;
  [[nodiscard]] const std::string& getContents() const noexcept;
  [[nodiscard]] DocumentType       getType() const noexcept;

  void setContents(std::string contents);

private:
  std::string path;
  std::string contents;
};

/**
 * Creates a project document from an external file name and payload.
 *
 * Definitive extensions are preferred; otherwise the payload is inspected for a
 * Silicon circuit or non-text binary data. Verilog is extension-only.
 *
 * @throws std::invalid_argument if the name is invalid or the file type is unsupported.
 */
[[nodiscard]] Document importDocument(std::string_view fileName, std::string contents);

/** Returns the external leaf filename used when exporting a project document. */
[[nodiscard]] std::string documentFileName(const Document& document);

class DocumentStore {
public:
  using Listener = std::function<void(const DocumentChange&)>;

  /** Returned pointers/references are invalidated by any store mutation. */
  [[nodiscard]] const Document* find(std::string_view documentPath) const noexcept;
  [[nodiscard]] bool            contains(std::string_view documentPath) const noexcept;
  [[nodiscard]] bool            contains(DocumentType type) const noexcept;
  [[nodiscard]] const std::vector<Document>& getDocuments() const noexcept;
  [[nodiscard]] std::optional<std::size_t>   indexOf(std::string_view documentPath) const;

  std::uint64_t addListener(Listener listener) const;
  void          removeListener(std::uint64_t id) const;

private:
  friend class ProjectContext;

  void commitDocuments(std::vector<Document> documents,
                       const DocumentChange& change) noexcept;

  std::vector<Document>                                          documents;
  mutable SILICON::core::CallbackRegistry<const DocumentChange&> listeners;
};

/** Returns an immutable copy of a project raw-binary document, or null if absent. */
[[nodiscard]] std::shared_ptr<const std::string>
binaryContentsSnapshot(const DocumentStore& documents, std::string_view slug);

}  // namespace SILICON::project
