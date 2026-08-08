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

#include <nlohmann/json.hpp>

namespace SILICON::project {
namespace {

  std::optional<DocumentKind> classifyFlatJsonPath(const std::string_view path,
                                                   const std::string_view prefix,
                                                   const DocumentKind     kind)
  {
    constexpr std::string_view suffix = ".json";
    if (!path.starts_with(prefix) || !path.ends_with(suffix)
        || path.find("..") != std::string_view::npos)
      return std::nullopt;
    const auto fileName = path.substr(prefix.size());
    if (fileName.size() <= suffix.size() || fileName.contains('/'))
      return std::nullopt;
    return kind;
  }

  void requireValidPath(const std::string_view path)
  {
    if (!classifyDocumentPath(path))
      throw std::invalid_argument(
          "Document path must reference a valid project JSON entry");
  }

}  // namespace

std::optional<DocumentKind> classifyDocumentPath(const std::string_view path)
{
  if (auto kind = classifyFlatJsonPath(path, "circuits/", DocumentKind::Circuit))
    return kind;
  return classifyFlatJsonPath(path, "subcircuits/", DocumentKind::Subcircuit);
}

std::optional<std::string> subcircuitSlugForPath(const std::string_view path)
{
  if (classifyDocumentPath(path) != DocumentKind::Subcircuit)
    return std::nullopt;
  constexpr std::string_view prefix = "subcircuits/";
  constexpr std::string_view suffix = ".json";
  return std::string(
      path.substr(prefix.size(), path.size() - prefix.size() - suffix.size()));
}

std::string subcircuitPathForSlug(const std::string_view slug)
{
  return std::format("subcircuits/{}.json", slug);
}

bool isValidProjectAssetPath(const std::string_view path)
{
  if (path.empty() || path.front() == '/' || path.back() == '/' || path.contains('\\')
      || path == "mimetype" || path == "metadata.json" || path == "project.json"
      || classifyDocumentPath(path)) {
    return false;
  }
  if (std::ranges::any_of(path, [](const unsigned char character) {
        return character < 0x20 || character == 0x7f;
      })) {
    return false;
  }

  std::size_t segmentStart = 0;
  while (segmentStart < path.size()) {
    const auto separator = path.find('/', segmentStart);
    const auto segment   = path.substr(segmentStart, separator == std::string_view::npos
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

std::optional<HdlDescriptor> parseHdlDescriptor(const std::string_view sceneJson)
{
  nlohmann::json json;
  try {
    json = nlohmann::json::parse(sceneJson);
  } catch (const nlohmann::json::parse_error&) {
    throw std::runtime_error("Subcircuit document is not valid JSON");
  }

  const auto hdl = json.find("hdl");
  if (hdl == json.end())
    return std::nullopt;
  if (!hdl->is_object())
    throw std::runtime_error("Subcircuit hdl must be an object");
  if (hdl->size() != 2 || !hdl->contains("type") || !hdl->contains("path")
      || !(*hdl)["type"].is_string() || !(*hdl)["path"].is_string()) {
    throw std::runtime_error(
        "Subcircuit hdl must contain only string fields 'type' and 'path'");
  }

  HdlDescriptor result{.type = (*hdl)["type"].get<std::string>(),
                       .path = (*hdl)["path"].get<std::string>()};
  if (result.type != "verilog")
    throw std::runtime_error(
        std::format("Unsupported subcircuit HDL type '{}'", result.type));
  if (!isValidProjectAssetPath(result.path))
    throw std::runtime_error("Subcircuit hdl.path must be a normalized project-relative "
                             "asset path");
  return result;
}

Document::Document(std::string path, std::string sceneJson,
                   std::optional<std::string> coreCircuitJson)
  : path(std::move(path)),
    sceneJson(std::move(sceneJson)),
    coreCircuitJson(std::move(coreCircuitJson))
{
  requireValidPath(this->path);
}

const std::string& Document::getPath() const
{
  return path;
}

const std::string& Document::getSceneJson() const
{
  return sceneJson;
}

const std::optional<std::string>& Document::getCoreCircuitJson() const
{
  return coreCircuitJson;
}

DocumentKind Document::kind() const
{
  return *classifyDocumentPath(path);
}

std::optional<std::string> Document::subcircuitSlug() const
{
  return subcircuitSlugForPath(path);
}

void Document::setSceneJson(std::string                sceneJson,
                            std::optional<std::string> coreCircuitJson)
{
  this->sceneJson       = std::move(sceneJson);
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
  listeners.notify({});
}

void DocumentStore::upsertDocument(Document document)
{
  const auto path = document.getPath();
  if (auto index = indexOf(document.getPath())) {
    documents[*index] = std::move(document);
    listeners.notify(path);
    return;
  }
  documents.push_back(std::move(document));
  listeners.notify(path);
}

void DocumentStore::insertDocument(Document document, const std::size_t index)
{
  const auto path = document.getPath();
  if (contains(document.getPath()))
    throw std::invalid_argument(
        std::format("Duplicate project document path: {}", document.getPath()));
  const auto offset = std::min(index, documents.size());
  documents.insert(documents.begin() + offset, std::move(document));
  listeners.notify(path);
}

void DocumentStore::removeDocument(const std::string_view documentPath)
{
  const auto path    = std::string(documentPath);
  const auto oldSize = documents.size();
  std::erase_if(documents, [&](const Document& document) {
    return document.getPath() == documentPath;
  });
  if (documents.size() != oldSize)
    listeners.notify(path);
}

void DocumentStore::clear()
{
  documents.clear();
  listeners.notify({});
}

const Document* DocumentStore::find(const std::string_view documentPath) const
{
  const auto it = std::ranges::find(documents, documentPath, &Document::getPath);
  return it == documents.end() ? nullptr : &*it;
}

bool DocumentStore::contains(const std::string_view documentPath) const
{
  return find(documentPath) != nullptr;
}

std::vector<Document> DocumentStore::getDocuments() const
{
  return documents;
}

std::vector<Document> DocumentStore::getDocuments(const DocumentKind kind) const
{
  return documents | std::views::filter([kind](const Document& document) {
           return document.kind() == kind;
         })
         | std::ranges::to<std::vector>();
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
