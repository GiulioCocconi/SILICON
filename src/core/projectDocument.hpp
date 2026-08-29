/*
 Copyright (c) 2026. Giulio Cocconi
 ...
 */

#pragma once

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

enum class DocumentType { Circuit, Subcircuit, Code };

[[nodiscard]] std::optional<DocumentType> documentTypeForPath(std::string_view path);
[[nodiscard]] std::optional<std::string>  subcircuitSlugForPath(std::string_view path);
[[nodiscard]] std::string                 subcircuitPathForSlug(std::string_view slug);
class Document {
public:
  Document(std::string path, std::string contents);

  [[nodiscard]] const std::string&         getPath() const noexcept;
  [[nodiscard]] const std::string&         getContents() const noexcept;
  [[nodiscard]] DocumentType               getType() const noexcept;
  [[nodiscard]] std::optional<std::string>        subcircuitSlug() const;

  void setContents(std::string contents);

private:
  std::string                path;
  std::string                contents;
};

class DocumentStore {
public:
  using Listener = std::function<void(std::string_view)>;

  void setDocuments(std::vector<Document> documents);
  void upsertDocument(Document document);
  void insertDocument(Document document, std::size_t index);
  void removeDocument(std::string_view documentPath);
  void clear();

  [[nodiscard]] const Document*            find(std::string_view documentPath) const;
  [[nodiscard]] bool                       contains(std::string_view documentPath) const;
  [[nodiscard]] std::vector<Document>      getDocuments() const;
  [[nodiscard]] std::vector<Document>      getDocuments(DocumentType type) const;
  [[nodiscard]] std::optional<std::size_t> indexOf(std::string_view documentPath) const;

  std::uint64_t addListener(Listener listener);
  void          removeListener(std::uint64_t id);

private:
  std::vector<Document>                             documents;
  SILICON::core::CallbackRegistry<std::string_view> listeners;
};

/** Project-owned mutable state shared by the project's editors and workflows. */
class ProjectContext {
public:
  DocumentStore documents;
};

}  // namespace SILICON::project
