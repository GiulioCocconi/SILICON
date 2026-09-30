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

#include <cstddef>
#include <string_view>
#include <vector>

#include <core/circuitDependencyGraph.hpp>
#include <core/projectDocument.hpp>

namespace SILICON::project {

/** Project-owned document state and its derived Circuit dependency index. */
class ProjectContext {
public:
  [[nodiscard]] const DocumentStore&          documents() const noexcept;
  [[nodiscard]] const CircuitDependencyGraph& circuitDependencies() const noexcept;

  void setDocuments(std::vector<Document> documents);
  void upsertDocument(Document document);
  void insertDocument(Document document, std::size_t index);
  /** Renames a document and rewrites project document references to its slug. */
  void renameDocument(std::string_view oldPath, std::string_view newPath);
  void removeDocument(std::string_view documentPath);

private:
  DocumentStore          documents_;
  CircuitDependencyGraph circuitDependencies_;

  void commit(std::vector<Document> documents, CircuitDependencyGraph dependencies,
              const DocumentChange& change) noexcept;
};

}  // namespace SILICON::project
