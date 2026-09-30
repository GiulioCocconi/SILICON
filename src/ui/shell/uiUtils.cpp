#include "uiUtils.hpp"

#include <string_view>

#include <QAction>

#include <ui/circuit/diagram/scene/diagramScene.hpp>
#include <ui/shell/icons.hpp>

namespace SILICON::ui {

QString interactionModeName(const InteractionMode mode)
{
  switch (mode) {
    case InteractionMode::NORMAL_MODE: return QStringLiteral("NORMAL");
    case InteractionMode::COMPONENT_PLACING_MODE:
      return QStringLiteral("COMPONENT PLACING");
    case InteractionMode::WIRE_CREATION_MODE: return QStringLiteral("WIRE CREATION");
    case InteractionMode::PAN_MODE: return QStringLiteral("PAN");
    case InteractionMode::SIMULATION_MODE: return QStringLiteral("SIMULATION");
  }

  throw std::logic_error("Unhandled InteractionMode in interactionModeName");
}

QIcon categoryIcon(const SILICON::project::DocumentType type)
{
  std::string_view iconName;
  switch (SILICON::project::categoryOf(type)) {
    case SILICON::project::DocumentCategory::Diagram: iconName = "circuit-board"; break;
    case SILICON::project::DocumentCategory::Code: iconName = "code"; break;
    case SILICON::project::DocumentCategory::Architecture: iconName = "cpu"; break;
    case SILICON::project::DocumentCategory::Binary: iconName = "file"; break;
  }
  return Icon(QString::fromUtf8(iconName.data(), static_cast<qsizetype>(iconName.size())));
}

QAction* makeAction(QObject* parent, const QIcon& icon, const QString& text,
                    const QString& statusTip)
{
  auto* action = new QAction(icon, text, parent);
  if (!statusTip.isEmpty())
    action->setStatusTip(statusTip);
  return action;
}

QAction* makeAction(QObject* parent, const QString& text, const QString& statusTip)
{
  auto* action = new QAction(text, parent);
  if (!statusTip.isEmpty())
    action->setStatusTip(statusTip);
  return action;
}

}  // namespace SILICON::ui
