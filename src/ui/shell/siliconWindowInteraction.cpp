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

#include <ui/circuit/editor/circuitEditor.hpp>
#include <ui/shell/siliconWindow.hpp>

#include <optional>

#include <QStatusBar>

#include <ui/circuit/diagram/scene/diagramScene.hpp>
#include <ui/project/projectTree.hpp>
#include <ui/shell/uiUtils.hpp>
#include <ui/shell/windowActions.hpp>

namespace SILICON::ui {
using namespace SILICON::core;

void SiliconWindow::updateStatus() const
{
  statusBar()->showMessage(
      tr("Interaction Mode: %1")
          .arg(ui::interactionModeName(circuitEditor->scene()->getInteractionMode())));
}

void SiliconWindow::selectionChanged()
{
  if (!circuitEditor->scene()->selectedItems().empty() && projectTree)
    projectTree->clearDocumentSelection();

  updatePropertyDock();
}

}  // namespace SILICON::ui
