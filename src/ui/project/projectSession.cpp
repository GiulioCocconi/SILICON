#include "projectSession.hpp"

#include <ranges>

namespace SILICON::ui {

std::optional<std::string>
ProjectSession::firstCircuitPath(const std::string_view excludedPath) const
{
  const auto& documents = projectContext.documents().getDocuments();
  const auto  circuit   = std::ranges::find_if(documents, [excludedPath](const auto& d) {
    return d.getType() == SILICON::project::DocumentType::Circuit
           && d.getPath() != excludedPath;
  });
  if (circuit == documents.end())
    return std::nullopt;
  return circuit->getPath();
}

}  // namespace SILICON::ui
