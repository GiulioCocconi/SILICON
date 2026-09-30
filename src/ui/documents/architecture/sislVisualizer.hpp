#pragma once

#include <optional>
#include <string>
#include <vector>

#include <QWidget>

#include <sisl/sisl.hpp>

class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QPushButton;

namespace SILICON::ui {

// Grouped palette colors to avoid redundant palette() queries across layout functions
struct ThemeColors {
  QColor ink;
  QColor base;
  QColor accent;
  QColor shaded;
  QColor highlightedText;
  QColor mid;

  explicit ThemeColors(const QPalette& pal);
};

class SislVisualizer : public QWidget {
public:
  explicit SislVisualizer(QWidget* parent = nullptr);
  void setDescription(sisl::IsaDescription description);

private:
  void setupUI();

  // High-level views
  void showOverview();
  void showFormat(const std::optional<std::string>& formatName);

  // Overview Drawing Helpers
  void drawOverviewTitle();
  void drawAllFormatBoxes(const ThemeColors& theme, qreal& y);
  void drawStandaloneInstructionsBox(const ThemeColors& theme, qreal& y);
  void drawAllEnums(const ThemeColors& theme, qreal& y);

  // Format View Drawing Helpers
  void drawFormatHeader(const std::string& formatName, const ThemeColors& theme, qreal& y);
  void drawInstructionsForFormat(const std::optional<std::string>& formatName, const ThemeColors& theme, qreal& y);
  void drawAliases(const sisl::InstructionDescription& inst, const ThemeColors& theme, qreal& y);

  // Bit Layout Drawing Engine
  void drawLayout(const std::vector<sisl::FieldDescription>& fields,
                  std::size_t width,
                  const std::vector<sisl::FixedFieldDescription>& fixedFields,
                  qreal y,
                  const ThemeColors& theme);

  void drawFieldSlices(const std::vector<sisl::FieldDescription>& fields,
                       std::size_t width,
                       const std::vector<sisl::FixedFieldDescription>& fixedFields,
                       qreal top,
                       const ThemeColors& theme,
                       std::vector<bool>& occupied,
                       std::vector<bool>& indexes);

  void drawUnoccupiedSlices(std::size_t width,
                            qreal top,
                            const ThemeColors& theme,
                            std::vector<bool>& occupied,
                            std::vector<bool>& indexes);

  void drawAxisIndices(std::size_t width, qreal y, const ThemeColors& theme, const std::vector<bool>& indexes);

  // View management
  void resetScene(bool showHint);
  void finalizeScene();

  // State
  sisl::IsaDescription description;
  QGraphicsScene*      scene = nullptr;
  QGraphicsView*       view = nullptr;
  QPushButton*         backButton = nullptr;
  QLabel*              hintLabel = nullptr;
};

} // namespace SILICON::ui
