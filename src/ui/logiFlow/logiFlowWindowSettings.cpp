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

#include "logiFlowWindow.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeySequence>

#ifdef __EMSCRIPTEN__
  #include <emscripten/emscripten.h>
#endif

#include <core/simulator.hpp>
#include <ui/common/settingsWindow.hpp>
#include <ui/common/theme.hpp>

namespace SILICON {
namespace ui {

  namespace {
    ShortcutSetting shortcut(const QString& key, const QString& label, QAction* action,
                             const QKeySequence& defaultShortcut)
    {
      return {
          .setting =
              {
                  .name         = key,
                  .defaultValue = QVariant::fromValue(defaultShortcut),
              },
          .label  = label,
          .action = action,
      };
    }

  }  // namespace

  void LogiFlowWindow::syncWasmShortcutCapture()
  {
#ifdef __EMSCRIPTEN__
    const QVector<ShortcutSetting> shortcuts = shortcutSettings();
    QJsonArray                     shortcutSequences;
    for (const ShortcutSetting& shortcut : shortcuts) {
      if (!shortcut.action)
        continue;

      for (const QKeySequence& sequence : shortcut.action->shortcuts()) {
        if (sequence.isEmpty())
          continue;

        for (int index = 0; index < sequence.count(); ++index)
          shortcutSequences.append(
              QKeySequence(sequence[index]).toString(QKeySequence::PortableText));
      }
    }

    const QByteArray shortcutsJson =
        QJsonDocument(shortcutSequences).toJson(QJsonDocument::Compact);

    // clang-format off
EM_ASM(
    {
      const shortcuts = JSON.parse(UTF8ToString($0));
      globalThis.__siliconShortcutSequences =
          shortcuts.map(sequence => sequence.trim()
                                         .split('+')
                                         .map(part => part.trim().toLowerCase())
                                         .filter(Boolean));

      const isEditableTarget = target => target instanceof HTMLInputElement || target
          instanceof HTMLTextAreaElement || target
          instanceof HTMLSelectElement || target?.isContentEditable;

      const normalizedKeyToken = token => ({
                                           esc : 'escape',
                                           del : 'delete',
                                           ins : 'insert',
                                           return : 'enter',
                                           enter : 'enter',
                                           backtab : 'tab',
                                           space : ' ',
                                           pgup : 'pageup',
                                           pgdown : 'pagedown',
                                           plus : '+',
                                           comma : ','
                                         })[token]
          ?? token;

      const eventMatchesShortcut = (event, shortcut) =>
      {
        if (shortcut.length === 0)
          return false;

        const key        = event.key.toLowerCase();
        const keyToken   = normalizedKeyToken(shortcut[shortcut.length - 1]);
        const wantsCtrl  = shortcut.includes('ctrl') || shortcut.includes('control');
        const wantsMeta  = shortcut.includes('meta');
        const wantsAlt   = shortcut.includes('alt');
        const wantsShift = shortcut.includes('shift');
        const usesCommandModifier = wantsCtrl || wantsMeta || wantsAlt;

        if (!usesCommandModifier && isEditableTarget(event.target))
          return false;

        if (wantsCtrl && !(event.ctrlKey || event.metaKey))
          return false;
        if (wantsMeta && !event.metaKey)
          return false;
        if (wantsAlt !== event.altKey)
          return false;
        if (wantsShift !== event.shiftKey)
          return false;

        return key === keyToken || event.code.toLowerCase() === keyToken;
      };

      globalThis.__siliconShouldCaptureShortcut = event =>
          globalThis.__siliconShortcutSequences.some(
              shortcut => eventMatchesShortcut(event, shortcut));

      if (globalThis.__siliconShortcutCaptureInstalled)
        return;

      globalThis.__siliconShortcutCaptureInstalled = true;
      document.addEventListener(
      'keydown',
      event => {
        if (globalThis.__siliconShouldCaptureShortcut?.(event))
          event.preventDefault();
      },
      true);
    },
    shortcutsJson.constData());
    // clang-format on
#endif
  }

  QVector<ShortcutSetting> LogiFlowWindow::shortcutSettings() const
  {
    return {
        shortcut(QStringLiteral("keybindings/new"), tr("New"), newAct,
                 QKeySequence(QKeySequence::New)),
        shortcut(QStringLiteral("keybindings/open"), tr("Open"), openAct,
                 QKeySequence(QKeySequence::Open)),
        shortcut(QStringLiteral("keybindings/save"), tr("Save"), saveAct,
                 QKeySequence(QKeySequence::Save)),
        shortcut(QStringLiteral("keybindings/exportImage"), tr("Export"), exportImageAct,
                 QKeySequence(QKeySequence::Print)),
        shortcut(QStringLiteral("keybindings/exit"), tr("Exit"), exitAct,
                 QKeySequence(QKeySequence::Quit)),
        shortcut(QStringLiteral("keybindings/undo"), tr("Undo"), undoAct,
                 QKeySequence(QKeySequence::Undo)),
        shortcut(QStringLiteral("keybindings/redo"), tr("Redo"), redoAct,
                 QKeySequence(QKeySequence::Redo)),
        shortcut(QStringLiteral("keybindings/cut"), tr("Cut"), cutAct,
                 QKeySequence(QKeySequence::Cut)),
        shortcut(QStringLiteral("keybindings/copy"), tr("Copy"), copyAct,
                 QKeySequence(QKeySequence::Copy)),
        shortcut(QStringLiteral("keybindings/paste"), tr("Paste"), pasteAct,
                 QKeySequence(QKeySequence::Paste)),
        shortcut(QStringLiteral("keybindings/rotate"), tr("Rotate"), rotateAct,
                 QKeySequence(Qt::AltModifier | Qt::Key_R)),
        shortcut(QStringLiteral("keybindings/autoPlace"), tr("Auto place"), autoPlaceAct,
                 QKeySequence(Qt::AltModifier | Qt::Key_L)),
        shortcut(QStringLiteral("keybindings/delete"), tr("Delete"), deleteAct,
                 QKeySequence(QKeySequence::Delete)),
        shortcut(QStringLiteral("keybindings/normalMode"), tr("Normal mode"),
                 setNormalModeAct, QKeySequence()),
        shortcut(QStringLiteral("keybindings/panMode"), tr("Pan mode"), setPanModeAct,
                 QKeySequence()),
        shortcut(QStringLiteral("keybindings/wireCreationMode"), tr("Wire creation mode"),
                 setWireCreationModeAct, QKeySequence(Qt::AltModifier | Qt::Key_W)),
        shortcut(QStringLiteral("keybindings/simulationMode"), tr("Simulation mode"),
                 setSimulationModeAct,
                 QKeySequence(Qt::AltModifier | Qt::ControlModifier | Qt::Key_S)),
        shortcut(QStringLiteral("keybindings/componentPlacingMode"),
                 tr("Component placing mode"), setComponentPlacingModeAct,
                 QKeySequence(Qt::AltModifier | Qt::Key_A)),
        shortcut(QStringLiteral("keybindings/cancelInteraction"),
                 tr("Cancel current interaction"), cancelInteractionAct,
                 QKeySequence(Qt::Key_Escape)),
        shortcut(QStringLiteral("keybindings/toggleTrace"), tr("Waveform trace"),
                 toggleWaveformViewerAct, QKeySequence()),
        shortcut(QStringLiteral("keybindings/settings"), tr("Settings"), settingsAct,
                 QKeySequence()),
    };
  }

  void LogiFlowWindow::applyStoredSettings()
  {
    SiliconSettings            settings("LogiFlow", this);
    const CommonSettingsValues values = readCommonSettings(settings);

    SILICON::simulation::Simulator::setMaxSimulationSteps(
        static_cast<uint64_t>(values.maxSimulationSteps));
    SILICON::simulation::Simulator::setMaxTransitionsPerDeltaCycle(
        values.maxTransitionsPerDeltaCycle);

    ThemeEngine::apply(*qApp, themeModeFromText(values.theme));

    const auto shortcuts = shortcutSettings();
    for (const ShortcutSetting& shortcut : shortcuts) {
      shortcut.action->setShortcut(
          SILICON::ui::settings::value(settings, shortcut.setting).value<QKeySequence>());
#ifdef __EMSCRIPTEN__
      shortcut.action->setShortcutContext(Qt::ApplicationShortcut);
      if (!actions().contains(shortcut.action))
        addAction(shortcut.action);
#endif
    }

    syncWasmShortcutCapture();
  }

  void LogiFlowWindow::openSettings()
  {
    const auto     shortcuts = shortcutSettings();
    SettingsWindow settingsWindow("LogiFlow", shortcuts, this);
    settingsWindow.exec();
    syncWasmShortcutCapture();
  }

}  // namespace ui
}  // namespace SILICON
