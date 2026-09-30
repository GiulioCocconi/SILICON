#pragma once

#include <string>

#include <QString>

#include <core/projectContext.hpp>
#include <core/projectDocument.hpp>
#include <core/serialization/projectFile.hpp>

namespace SILICON::ui::projectDocumentPolicy {

[[nodiscard]] std::string                   defaultCircuitPath();
[[nodiscard]] SILICON::project::Document    defaultCircuitDocument();
[[nodiscard]] SILICON::project::ProjectInfo defaultProjectInfo(const QString& fileName);
void ensureProjectDocuments(SILICON::project::ProjectContext& context);
[[nodiscard]] std::string emptyGraphicalDocumentJson(SILICON::project::DocumentType type);
[[nodiscard]] std::string
uniqueDocumentPath(const SILICON::project::ProjectContext& context,
                   SILICON::project::DocumentType type, const QString& requestedName);

}  // namespace SILICON::ui::projectDocumentPolicy
