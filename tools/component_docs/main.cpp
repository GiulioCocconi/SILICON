/*
  Copyright (c) 2026. Giulio Cocconi
  SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "reference.hpp"

#include <core/serialization/component_registration.hpp>
#include <ui/serialization/gui_component_registration.hpp>

#include <QApplication>
#include <QFontDatabase>

#include <iostream>
#include <stdexcept>

int main(int argc, char** argv)
{
  if (argc != 3 || std::string_view(argv[1]) != "--output") {
    std::cerr << "Usage: silicon_component_docs --output DIRECTORY\n";
    return 1;
  }

  // This tool renders graphics without opening a window, including on CI hosts.
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication app(argc, argv);
  try {
    for (const auto* font :
         {":/fonts/Quicksand.ttf", ":/fonts/Chango.ttf", ":/fonts/NovaMono.ttf"})
      if (QFontDatabase::addApplicationFont(font) < 0)
        throw std::runtime_error(std::string("Cannot load bundled font: ") + font);
    QFont regular("Quicksand");
    regular.setPixelSize(14);
    QApplication::setFont(regular);

    auto& registry = SILICON::core::ComponentRegistry::instance();
    SILICON::core::registerAllComponents(registry);
    auto& factory = SILICON::ui::GUIComponentFactory::instance();
    SILICON::ui::registerAllGUIComponents(factory);
    SILICON::docs::generateReference(argv[2], registry, factory, app);
    std::cout << "Generated component reference in " << argv[2] << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Component documentation: " << error.what() << '\n';
    return 1;
  }
}
