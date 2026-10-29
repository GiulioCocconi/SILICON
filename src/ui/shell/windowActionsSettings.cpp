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

#include <ui/shell/windowActions.hpp>

#include <cstdint>

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeySequence>
#include <QMainWindow>

#ifdef __EMSCRIPTEN__
  #include <emscripten/emscripten.h>
#endif

#include <core/simulator.hpp>
#include <ui/shell/settingsWindow.hpp>
#include <ui/shell/theme.hpp>

namespace SILICON::ui {

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

#ifdef __EMSCRIPTEN__
BrowserShortcutCapture::BrowserShortcutCapture()
{
  // clang-format off
    id = EM_ASM_INT({
      globalThis.__siliconShortcutCaptures = globalThis.__siliconShortcutCaptures || new Map();
      const id = globalThis.__siliconNextShortcutCaptureId ?? 0;
      globalThis.__siliconNextShortcutCaptureId = id + 1;
      globalThis.__siliconShortcutCaptures.set(id, {});
      return id;
    });
  // clang-format on
}

BrowserShortcutCapture::~BrowserShortcutCapture() {
  // clang-format off
EM_ASM(
    {
      const captures = globalThis.__siliconShortcutCaptures;
      const capture = captures.get($0);
      for (const [type, handler] of capture.listeners || [])
        document.removeEventListener(type, handler, true);
      if (capture.frame)
        cancelAnimationFrame(capture.frame);
      captures.delete($0);
    }, id);
  // clang-format on
}

void BrowserShortcutCapture::update(const QByteArray& shortcutsJson)
{
  // clang-format off
EM_ASM(
    {
      const captures = globalThis.__siliconShortcutCaptures;
      const capture = captures.get($0);
      const shortcuts = JSON.parse(UTF8ToString($1));
      capture.sequences =
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

        const target     = event.composedPath()[0];
        const key        = event.key.toLowerCase();
        const keyToken   = normalizedKeyToken(shortcut[shortcut.length - 1]);
        const wantsCtrl  = shortcut.includes('ctrl') || shortcut.includes('control');
        const wantsMeta  = shortcut.includes('meta');
        const wantsAlt   = shortcut.includes('alt');
        const wantsShift = shortcut.includes('shift');
        const usesCommandModifier = wantsCtrl || wantsMeta || wantsAlt;

        if (!usesCommandModifier && isEditableTarget(target))
          return false;

        if (wantsCtrl || wantsMeta) {
          if (wantsCtrl && !(event.ctrlKey || event.metaKey))
            return false;
          if (wantsMeta && !event.metaKey)
            return false;
        } else if (event.ctrlKey || event.metaKey) {
          return false;
        }
        if (wantsAlt !== event.altKey)
          return false;
        if (wantsShift !== event.shiftKey)
          return false;

        return key === keyToken || event.code.toLowerCase() === keyToken;
      };

      if (capture.listeners)
        return;

      // Qt 6.11 requestActivateWindow() can leave DOM focus unset when an
      // input context exists. Keep this compatibility workaround here, alongside
      // browser shortcut capture; never change a valid DOM/text-input focus.
      const qtHost = event => event.composedPath().find(
          node => node.id === 'qt-shadow-container' && node.shadowRoot);
      const rememberQt = event => {
        const host = qtHost(event);
        if (!host)
          return false;
        capture.host = host;
        capture.window = event.composedPath().find(
            node => node.classList?.contains('qt-window'));
        return true;
      };
      const scheduleFocus = () => {
        if (capture.frame || !capture.owned)
          return;
        // One frame after Qt and browser default handling, without a timeout or
        // a polling loop. Pointer/focus transitions outside Qt revoke ownership.
        capture.frame = requestAnimationFrame(() => {
          capture.frame = 0;
          if (!capture.owned || !document.hasFocus()
              || document.activeElement !== document.body)
            return;
          const root = capture.host?.shadowRoot;
          if (!capture.host?.isConnected || !root || root.activeElement)
            return;
          const visible = element => element.getClientRects().length > 0;
          // Prefer the helper for the window that received the event. If a Qt
          // popup just disappeared, use a remaining visible window's helper.
          // Both selectors are Qt internals, isolated here for version updates.
          const selector = '.qt-window-focus-helper, .hidden-visually-read-by-screen-reader';
          const local = capture.window?.querySelectorAll(selector) || [];
          const target = Array.from(local).find(visible)
              || Array.from(root.querySelectorAll(selector)).find(visible);
          target?.focus({preventScroll: true});
        });
      };
      const onFocusIn = event => {
        capture.owned = rememberQt(event);
      };
      const onPointerDown = event => {
        capture.owned = rememberQt(event);
        scheduleFocus();
      };
      const onFocusOut = event => {
        if (capture.owned && qtHost(event))
          scheduleFocus();
      };
      const onKeyDown = event => {
        if (!rememberQt(event))
          return;
        // Tab is an intentional focus transition, including out of the app.
        capture.owned = event.key !== 'Tab';
        if (!capture.owned || event.isComposing)
          return;
        scheduleFocus();
        if (capture.sequences.some(
              shortcut => eventMatchesShortcut(event, shortcut)))
          event.preventDefault();
      };
      capture.listeners = ([
        ['keydown', onKeyDown], ['focusin', onFocusIn],
        ['focusout', onFocusOut], ['pointerdown', onPointerDown]
      ]);
      for (const [type, handler] of capture.listeners)
        document.addEventListener(type, handler, true);
    },
    id,
    shortcutsJson.constData());
  // clang-format on
}

#endif

WindowActions::~WindowActions() = default;

void WindowActions::syncWasmShortcutCapture()
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

  if (!browserShortcuts)
    browserShortcuts = std::make_unique<BrowserShortcutCapture>();
  browserShortcuts->update(shortcutsJson);
#endif
}

QVector<ShortcutSetting> WindowActions::shortcutSettings() const
{
  return {
      shortcut(QStringLiteral("keybindings/new"), tr("New"), project.newProject,
               QKeySequence(QKeySequence::New)),
      shortcut(QStringLiteral("keybindings/open"), tr("Open"), project.open,
               QKeySequence(QKeySequence::Open)),
      shortcut(QStringLiteral("keybindings/save"), tr("Save"), project.save,
               QKeySequence(QKeySequence::Save)),
      shortcut(QStringLiteral("keybindings/exportImage"), tr("Export"),
               project.exportImage, QKeySequence(QKeySequence::Print)),
      shortcut(QStringLiteral("keybindings/exit"), tr("Exit"), project.exit,
               QKeySequence(QKeySequence::Quit)),
      shortcut(QStringLiteral("keybindings/undo"), tr("Undo"), edit.undo,
               QKeySequence(QKeySequence::Undo)),
      shortcut(QStringLiteral("keybindings/redo"), tr("Redo"), edit.redo,
               QKeySequence(QKeySequence::Redo)),
      shortcut(QStringLiteral("keybindings/cut"), tr("Cut"), edit.cut,
               QKeySequence(QKeySequence::Cut)),
      shortcut(QStringLiteral("keybindings/copy"), tr("Copy"), edit.copy,
               QKeySequence(QKeySequence::Copy)),
      shortcut(QStringLiteral("keybindings/paste"), tr("Paste"), edit.paste,
               QKeySequence(QKeySequence::Paste)),
      shortcut(QStringLiteral("keybindings/rotate"), tr("Rotate"), circuit.rotate,
               QKeySequence(Qt::AltModifier | Qt::Key_R)),
      shortcut(QStringLiteral("keybindings/autoPlace"), tr("Auto place"),
               circuit.autoPlace, QKeySequence(Qt::AltModifier | Qt::Key_L)),
      shortcut(QStringLiteral("keybindings/centerView"), tr("Center view"),
               circuit.centerView, QKeySequence(Qt::AltModifier | Qt::Key_C)),
      shortcut(QStringLiteral("keybindings/delete"), tr("Delete"), edit.remove,
               QKeySequence(QKeySequence::Delete)),
      shortcut(QStringLiteral("keybindings/normalMode"), tr("Normal mode"),
               circuit.setNormalMode, QKeySequence()),
      shortcut(QStringLiteral("keybindings/panMode"), tr("Pan mode"), circuit.setPanMode,
               QKeySequence()),
      shortcut(QStringLiteral("keybindings/wireCreationMode"), tr("Wire creation mode"),
               circuit.setWireCreationMode, QKeySequence(Qt::AltModifier | Qt::Key_W)),
      shortcut(QStringLiteral("keybindings/simulationMode"), tr("Simulation mode"),
               circuit.setSimulationMode,
               QKeySequence(Qt::AltModifier | Qt::ControlModifier | Qt::Key_S)),
      shortcut(QStringLiteral("keybindings/componentPlacingMode"),
               tr("Component placing mode"), circuit.setComponentPlacingMode,
               QKeySequence(Qt::AltModifier | Qt::Key_A)),
      shortcut(QStringLiteral("keybindings/cancelInteraction"),
               tr("Cancel current interaction"), circuit.cancelInteraction,
               QKeySequence(Qt::Key_Escape)),
      shortcut(QStringLiteral("keybindings/toggleTrace"), tr("Waveform trace"),
               waveform.toggleTrace, QKeySequence()),
      shortcut(QStringLiteral("keybindings/settings"), tr("Settings"), project.settings,
               QKeySequence()),
  };
}

void WindowActions::applyStoredSettings()
{
  SiliconSettings            settings("SILICON", this);
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
    if (!window.actions().contains(shortcut.action))
      window.addAction(shortcut.action);
#endif
  }

  syncWasmShortcutCapture();
}

void WindowActions::openSettings()
{
  const auto shortcuts = shortcutSettings();
#ifdef __EMSCRIPTEN__
  // WASM cannot run the nested event loop used by QDialog::exec().
  auto* settingsWindow = new SettingsWindow("SILICON", shortcuts, &window);
  settingsWindow->setAttribute(Qt::WA_DeleteOnClose);
  connect(settingsWindow, &QDialog::finished, this,
          [this] { syncWasmShortcutCapture(); });
  settingsWindow->open();
#else
  SettingsWindow settingsWindow("SILICON", shortcuts, &window);
  settingsWindow.exec();
  syncWasmShortcutCapture();
#endif
}

}  // namespace SILICON::ui
