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
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <core/callbackRegistry.hpp>
#include <core/codeFile.hpp>

namespace SILICON::project {

enum class DocumentType { Circuit, Subcircuit, Code, Binary };

struct DocumentTypeInfo {
  DocumentType     type;
  std::string_view root;
  std::string_view suffix;
  bool             isGraphical;
  bool             usesSlug;
};

inline constexpr std::array<DocumentTypeInfo, 4> DOCUMENT_TYPE_INFO{{
    {.type        = DocumentType::Circuit,
     .root        = "circuits/",
     .suffix      = ".json",
     .isGraphical = true,
     .usesSlug    = true},
    {.type        = DocumentType::Subcircuit,
     .root        = "subcircuits/",
     .suffix      = ".json",
     .isGraphical = true,
     .usesSlug    = true},
    {.type        = DocumentType::Code,
     .root        = "code/",
     .suffix      = {},
     .isGraphical = false,
     .usesSlug    = false},
    {.type        = DocumentType::Binary,
     .root        = "bin/",
     .suffix      = {},
     .isGraphical = false,
     .usesSlug    = true},
}};

[[nodiscard]] constexpr const DocumentTypeInfo& documentTypeInfo(const DocumentType type)
{
  return DOCUMENT_TYPE_INFO[static_cast<std::size_t>(type)];
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
 * derived from that path. Non-graphical documents never carry graphical
 * core-circuit JSON.
 */
class Document {
public:
  Document(std::string path, std::string contents,
           std::optional<std::string> coreCircuitJson = std::nullopt);

  [[nodiscard]] const std::string& getPath() const noexcept;
  [[nodiscard]] const std::string& getContents() const noexcept;
  [[nodiscard]] const std::optional<std::string>& getCoreCircuitJson() const noexcept;
  [[nodiscard]] DocumentType                      getType() const noexcept;

  void setContents(std::string                contents,
                   std::optional<std::string> coreCircuitJson = std::nullopt);

private:
  std::string                path;
  std::string                contents;
  std::optional<std::string> coreCircuitJson;
};

class DocumentStore {
public:
  using Listener = std::function<void(const DocumentChange&)>;

  static DocumentStore& active();

  void setDocuments(std::vector<Document> documents);
  void upsertDocument(Document document);
  void insertDocument(Document document, std::size_t index);
  void removeDocument(std::string_view documentPath);
  void clear();

  /** Returned pointers/references are invalidated by any store mutation. */
  [[nodiscard]] const Document* find(std::string_view documentPath) const noexcept;
  [[nodiscard]] bool contains(std::string_view documentPath) const noexcept;
  [[nodiscard]] bool contains(DocumentType type) const noexcept;
  [[nodiscard]] const std::vector<Document>& getDocuments() const noexcept;
  [[nodiscard]] std::optional<std::size_t> indexOf(std::string_view documentPath) const;

  std::uint64_t addListener(Listener listener);
  void          removeListener(std::uint64_t id);

private:
  std::vector<Document> documents;
  SILICON::core::CallbackRegistry<const DocumentChange&> listeners;
};

}  // namespace SILICON::project
