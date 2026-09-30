/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */

#pragma once

#include <string>

namespace SILICON::ui {

/** Document navigation needed by scene undo commands. */
class DocumentNavigator {
public:
  virtual ~DocumentNavigator()                                                  = default;
  [[nodiscard]] virtual const std::string& currentDocumentPath() const noexcept = 0;
  virtual bool                             activateDocument(const std::string& path) = 0;
};

}  // namespace SILICON::ui
