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

namespace SILICON::project {

enum class DocumentKind { Circuit, Subcircuit };

/**
 * @brief Reference from a subcircuit scene to its project-local HDL source.
 *
 * The descriptor is stored as the optional top-level scene member
 * `{"hdl":{"type":"verilog","path":"hdl/example.v"}}`. `path` is normalized and
 * relative to the project archive root; it does not refer to the host filesystem.
 * Only Verilog is accepted until another frontend is added explicitly.
 */
struct HdlDescriptor {
  /// HDL frontend identifier. The only currently supported value is `verilog`.
  std::string type;
  /// Normalized project-relative path of the source asset.
  std::string path;

  bool operator==(const HdlDescriptor&) const = default;
};

[[nodiscard]] std::optional<DocumentKind>  classifyDocumentPath(std::string_view path);
[[nodiscard]] std::optional<std::string>   subcircuitSlugForPath(std::string_view path);
[[nodiscard]] std::string                  subcircuitPathForSlug(std::string_view slug);
/** @brief Checks that an asset path is normalized, relative, and outside reserved entries. */
[[nodiscard]] bool                         isValidProjectAssetPath(std::string_view path);
/**
 * @brief Parses and validates the optional HDL descriptor in serialized scene JSON.
 * @return No value when the scene has no `hdl` member.
 * @throws std::runtime_error for malformed JSON, unsupported HDL types, extra descriptor
 * fields, or invalid project asset paths.
 */
[[nodiscard]] std::optional<HdlDescriptor> parseHdlDescriptor(std::string_view sceneJson);

class Document {
public:
  Document(std::string path, std::string sceneJson,
           std::optional<std::string> coreCircuitJson = std::nullopt);

  [[nodiscard]] const std::string&                path() const;
  [[nodiscard]] const std::string&                sceneJson() const;
  [[nodiscard]] const std::optional<std::string>& coreCircuitJson() const;
  [[nodiscard]] DocumentKind                      kind() const;
  [[nodiscard]] std::optional<std::string>        subcircuitSlug() const;

  void setSceneJson(std::string                sceneJson,
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

  [[nodiscard]] const Document*            find(std::string_view documentPath) const;
  [[nodiscard]] bool                       contains(std::string_view documentPath) const;
  [[nodiscard]] std::vector<Document>      documents() const;
  [[nodiscard]] std::vector<Document>      documents(DocumentKind kind) const;
  [[nodiscard]] std::optional<std::size_t> indexOf(std::string_view documentPath) const;

  std::uint64_t addListener(Listener listener);
  void          removeListener(std::uint64_t id);

private:
  std::vector<Document>              documents_;
  SILICON::core::CallbackRegistry<std::string_view> listeners_;
};

}  // namespace SILICON::project
