/*
 Copyright (c) 2026. Giulio Cocconi

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <QByteArray>
#include <QWidget>

#include <utils/num_formatting.hpp>

class QLineEdit;
class QUndoStack;

namespace SILICON::ui {

class BinaryHexView;

/** Fixed-size binary document editor with a byte-oriented hexadecimal caret. */
class BinaryEditor : public QWidget {
public:
  explicit BinaryEditor(QWidget* parent = nullptr);
  ~BinaryEditor() override;

  void                            setData(QByteArray data);
  [[nodiscard]] const QByteArray& data() const noexcept;

  [[nodiscard]] std::size_t byteOffset() const noexcept;
  bool                      setByteOffset(std::size_t offset);
  bool                      goToByteOffset(std::size_t offset);

  [[nodiscard]] bool                         canReadValue(std::size_t bitWidth) const;
  [[nodiscard]] std::optional<std::uint64_t> readValue(std::size_t bitWidth) const;
  bool overwriteValue(std::uint64_t value, std::size_t bitWidth,
                      const QString& undoText = {});
  bool overwriteHexDigit(unsigned digit);

  [[nodiscard]] QUndoStack* history() const noexcept;
  [[nodiscard]] bool        isModified() const;
  void                      setModified(bool modified);

  void refreshInspectors();

private:
  friend class BinaryHexView;
  friend class BinaryNibbleEditCommand;

  struct Inspector {
    QLineEdit*                    edit;
    SILICON::core::BusValueFormat format;
    std::size_t                   width;
  };

  [[nodiscard]] unsigned nibbleAt(std::size_t offset) const;
  void applyNibbles(std::size_t offset, const std::vector<unsigned>& values);
  void pushNibbleEdit(std::size_t offset, std::vector<unsigned> values,
                      const QString& text);
  void commitInspector(Inspector& inspector);
  void commitOffset();
  void refreshOffset();
  void updateEditorState();

  QByteArray             bytes;
  std::size_t            selectedByte        = 0;
  bool                   highHexDigitEntered = false;
  BinaryHexView*         hexView;
  QLineEdit*             offsetEdit;
  QUndoStack*            undoHistory;
  std::vector<Inspector> inspectors;
};

}  // namespace SILICON::ui
