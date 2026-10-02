#include "projectDocumentPolicy.hpp"

#include <cctype>
#include <format>
#include <stdexcept>

#include <QFileInfo>
#include <nlohmann/json.hpp>

#include <ui/circuit/components/subcircuit/metadata.hpp>

namespace SILICON::ui::projectDocumentPolicy {

std::string defaultCircuitPath()
{
  return std::string(SILICON::project::DEFAULT_CIRCUIT_PATH);
}

SILICON::project::Document defaultCircuitDocument()
{
  return {defaultCircuitPath(), ""};
}

SILICON::project::ProjectInfo defaultProjectInfo(const QString& fileName)
{
  const QString baseName = QFileInfo(fileName).baseName();
  return {.name = (baseName.isEmpty() ? QStringLiteral("Untitled Project") : baseName)
                      .toStdString(),
          .description = ""};
}

void ensureProjectDocuments(SILICON::project::ProjectContext& context)
{
  if (!context.documents().contains(SILICON::project::DocumentType::Circuit)) {
    context.upsertDocument(
        {defaultCircuitPath(),
         emptyGraphicalDocumentJson(SILICON::project::DocumentType::Circuit)});
  }
}

std::string emptyGraphicalDocumentJson(const SILICON::project::DocumentType type)
{
  nlohmann::ordered_json scene;
  scene["circuit"] = nlohmann::ordered_json{
      {"version", SILICON_VERSION}, {"components", nlohmann::ordered_json::array()}};
  scene["visual"]["components"] = nlohmann::ordered_json::array();
  scene["visual"]["wires"]      = nlohmann::ordered_json::array();

  if (type == SILICON::project::DocumentType::Circuit) {
    scene["graphicalComponent"] =
        nlohmann::ordered_json{{"shape",
                                {{"type", "rectangle"},
                                 {"width", GRAPHICAL_SUBCIRCUIT_DEFAULT_SIZE},
                                 {"height", GRAPHICAL_SUBCIRCUIT_DEFAULT_SIZE}}},
                               {"inputs", nlohmann::ordered_json::array()},
                               {"outputs", nlohmann::ordered_json::array()}};
  }

  return scene.dump(2);
}

std::string uniqueDocumentPath(const SILICON::project::ProjectContext& context,
                               const SILICON::project::DocumentType    type,
                               const QString&                          requestedName)
{
  if (SILICON::project::categoryOf(type) != SILICON::project::DocumentCategory::Diagram)
    throw std::invalid_argument("Only graphical documents use generated slugs");

  const auto fallback = std::string("circuit");
  auto       slug     = requestedName.trimmed().toStdString();
  if (slug.empty())
    slug = fallback;

  for (char& ch : slug) {
    const auto byte = static_cast<unsigned char>(ch);
    if (std::isalnum(byte))
      ch = static_cast<char>(std::tolower(byte));
    else if (ch != '-' && ch != '_')
      ch = '_';
  }

  if (const auto first = slug.find_first_not_of('_'); first == std::string::npos) {
    slug = fallback;
  } else {
    slug = slug.substr(first, slug.find_last_not_of('_') - first + 1);
  }

  const auto& store     = context.documents();
  auto        candidate = SILICON::project::documentPathForSlug(type, slug);
  for (int suffix = 2; store.contains(candidate); ++suffix)
    candidate =
        SILICON::project::documentPathForSlug(type, std::format("{}-{}", slug, suffix));

  return candidate;
}

}  // namespace SILICON::ui::projectDocumentPolicy
