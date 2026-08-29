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

enum class DocumentChangeKind { Added, Updated, Removed, Reset };

struct DocumentChange {
  DocumentChangeKind         kind;
  std::optional<std::string> path;

  bool operator==(const DocumentChange&) const = default;
};

[[nodiscard]] std::optional<DocumentType> documentTypeForPath(std::string_view path);
[[nodiscard]] std::optional<std::string>  subcircuitSlugForPath(std::string_view path);
[[nodiscard]] bool                        isValidSubcircuitSlug(std::string_view slug);
[[nodiscard]] std::string                 subcircuitPathForSlug(std::string_view slug);
/** Checks that an asset path is normalized, relative, and outside reserved namespaces. */
[[nodiscard]] bool isValidProjectAssetPath(std::string_view path);

/**
 * A document always has a valid, immutable project-relative path. Its type is
 * derived from that path. Code documents never carry graphical core-circuit JSON.
 */
class Document {
public:
  Document(std::string path, std::string contents,
           std::optional<std::string> coreCircuitJson = std::nullopt);

  [[nodiscard]] const std::string& getPath() const noexcept;
  [[nodiscard]] const std::string& getContents() const noexcept;
  [[nodiscard]] const std::optional<std::string>& getCoreCircuitJson() const noexcept;
  [[nodiscard]] DocumentType                      getType() const noexcept;
  [[nodiscard]] std::optional<std::string>         subcircuitSlug() const;

  void setContents(std::string                contents,
                   std::optional<std::string> coreCircuitJson = std::nullopt);

private:
  std::string                path;
  std::string                contents;
  std::optional<std::string> coreCircuitJson;
};

class DocumentStore {
public:
  using DocumentReferences = std::vector<std::reference_wrapper<const Document>>;
  using Listener           = std::function<void(const DocumentChange&)>;

  static DocumentStore& active();

  void setDocuments(std::vector<Document> documents);
  void upsertDocument(Document document);
  void insertDocument(Document document, std::size_t index);
  void removeDocument(std::string_view documentPath);
  void clear();

  /** Returned pointers/references are invalidated by any store mutation. */
  [[nodiscard]] const Document* find(std::string_view documentPath) const noexcept;
  [[nodiscard]] bool contains(std::string_view documentPath) const noexcept;
  [[nodiscard]] const std::vector<Document>& getDocuments() const noexcept;
  [[nodiscard]] DocumentReferences getDocuments(DocumentType type) const;
  [[nodiscard]] std::optional<std::size_t> indexOf(std::string_view documentPath) const;

  std::uint64_t addListener(Listener listener);
  void          removeListener(std::uint64_t id);

private:
  std::vector<Document> documents;
  SILICON::core::CallbackRegistry<const DocumentChange&> listeners;
};

}  // namespace SILICON::project
