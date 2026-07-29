/*
  Copyright (c) 2026. Giulio Cocconi

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
*/

#include "metadataDescriptionEdit.hpp"

#include <QFocusEvent>


namespace SILICON {
namespace ui {

MetadataDescriptionEdit::MetadataDescriptionEdit(QWidget* parent) : QPlainTextEdit(parent)
{
}

void MetadataDescriptionEdit::focusOutEvent(QFocusEvent* event)
{
  if (commit)
    commit();

  QPlainTextEdit::focusOutEvent(event);
}

}  // namespace ui
}  // namespace SILICON
