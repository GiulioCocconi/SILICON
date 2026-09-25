#pragma once

#include <functional>
#include <optional>
#include <string>

#include <QWidget>

#include <sisl/sisl.hpp>

class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QPushButton;

namespace SILICON::ui {

class SislVisualizer : public QWidget {
public:
  explicit SislVisualizer(QWidget* parent = nullptr);
  void setDescription(sisl::IsaDescription description);

private:
  void showOverview();
  void showFormat(const std::optional<std::string>& name);
  void drawLayout(const std::vector<sisl::FieldDescription>& fields,
                  std::size_t width,
                  const std::vector<sisl::FixedFieldDescription>& fixed,
                  qreal y);

  sisl::IsaDescription description;
  QGraphicsScene*      scene = nullptr;
  QGraphicsView*       view = nullptr;
  QPushButton*         back = nullptr;
  QLabel*              hint = nullptr;
};

} // namespace SILICON::ui
