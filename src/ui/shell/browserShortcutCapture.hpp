/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
 */
#pragma once

class QByteArray;

namespace SILICON::ui {

/** Owns one browser listener without retaining a pointer into C++ state. */
class BrowserShortcutCapture {
public:
  BrowserShortcutCapture();
  ~BrowserShortcutCapture();
  BrowserShortcutCapture(const BrowserShortcutCapture&)            = delete;
  BrowserShortcutCapture& operator=(const BrowserShortcutCapture&) = delete;

  void update(const QByteArray& shortcutsJson);

private:
  int id;
};

}  // namespace SILICON::ui
