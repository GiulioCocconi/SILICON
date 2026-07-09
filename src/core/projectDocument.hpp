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

namespace silicon::project {

enum class DocumentKind { Circuit, Subcircuit };

[[nodiscard]] std::optional<DocumentKind> classifyDocumentPath(std::string_view path);
[[nodiscard]] std::optional<std::string> subcircuitSlugForPath(std::string_view path);
[[nodiscard]] std::string subcircuitPathForSlug(std::string_view slug);

class Document {
public:
  Document(std::string path, std::string sceneJson,
           std::optional<std::string> coreCircuitJson = std::nullopt);

  [[nodiscard]] const std::string& path() const;
  [[nodiscard]] const std::string& sceneJson() const;
  [[nodiscard]] const std::optional<std::string>& coreCircuitJson() const;
  [[nodiscard]] DocumentKind kind() const;
  [[nodiscard]] std::optional<std::string> subcircuitSlug() const;

  void setSceneJson(std::string sceneJson,
                    std::optional<std::string> coreCircuitJson = std::nullopt);

private:
  std::string                path_;
  std::string                sceneJson_;
  std::optional<std::string> coreCircuitJson_;
};

class DocumentStore {
public:
  using Listener = std::function<void(std::string_view)>;

  static DocumentStore& active();

  void setDocuments(std::vector<Document> documents);
  void upsertDocument(Document document);
  void insertDocument(Document document, std::size_t index);
  void removeDocument(std::string_view documentPath);
  void clear();

  [[nodiscard]] const Document* find(std::string_view documentPath) const;
  [[nodiscard]] bool contains(std::string_view documentPath) const;
  [[nodiscard]] std::vector<Document> documents() const;
  [[nodiscard]] std::vector<Document> documents(DocumentKind kind) const;
  [[nodiscard]] std::optional<std::size_t> indexOf(std::string_view documentPath) const;

  std::uint64_t addListener(Listener listener);
  void removeListener(std::uint64_t id);

private:
  std::vector<Document>                documents_;
  CallbackRegistry<std::string_view> listeners_;
};

}  // namespace silicon::project
