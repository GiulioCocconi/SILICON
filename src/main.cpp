/*
  Copyright (c) 2025. Giulio Cocconi

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

#include <QApplication>
#include <QCommandLineParser>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QLabel>
#include <QMainWindow>
#include <QSplashScreen>

#include <core/serialization/component_registration.hpp>
#include <ui/common/icons.hpp>
#include <ui/logiFlow/logiFlowWindow.hpp>
#include <ui/serialization/gui_component_registration.hpp>

int siliconMain(int argc, char** argv)
{
  const QApplication app(argc, argv);
  QApplication::setApplicationName("SILICON");
  QApplication::setStyle("Fusion");
  QApplication::setApplicationVersion(SILICON_VERSION);

  // LOAD THE FONTS
  QFontDatabase::addApplicationFont(":/fonts/Chango.ttf");
  QFontDatabase::addApplicationFont(":/fonts/Quicksand.ttf");
  QFontDatabase::addApplicationFont(":/fonts/NovaMono.ttf");

  QApplication::setFont(QFont("Quicksand", app.font().pointSize() * 1.2, QFont::Medium));

  QApplication::setWindowIcon(Icon("silicon", {QSize(32, 32), QSize(128, 128)}));

  // Command Line Parser

  QCommandLineParser parser;
  parser.setApplicationDescription("SILICON: Simulation of Interconnected Logical\
  Inputs, Circuits, and Output Nodes");
  parser.addHelpOption();
  parser.addVersionOption();
  parser.process(app);

  // Splash screen
  QSplashScreen splashScreen(QPixmap(":/splash.jpg"));
  splashScreen.show();
  splashScreen.showMessage("Loading...", Qt::AlignBottom | Qt::AlignHCenter, Qt::white);

  // Force processing of events to show the splash screen immediately
  QApplication::processEvents();

  // Register all components explicitly
  ComponentRegistry::instance();
  registerAllComponents(ComponentRegistry::instance());

  static GUIComponentFactory& guiFactory = GUIComponentFactory::instance();
  registerAllGUIComponents(guiFactory);

  LogiFlowWindow lfWin{};
  lfWin.resize(QGuiApplication::primaryScreen()->size() * 0.6);
  lfWin.show();

  splashScreen.finish(&lfWin);
  return QApplication::exec();
}

int main(int argc, char** argv)
{
  return siliconMain(argc, argv);
}
