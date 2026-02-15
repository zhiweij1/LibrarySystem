#ifndef QUERYBOOKFORM_H
#define QUERYBOOKFORM_H

#include <QWidget>

namespace Ui {
class QueryBookForm;
} // namespace Ui

class QueryBookForm : public QWidget {
  Q_OBJECT

public:
  explicit QueryBookForm(QWidget *Parent = nullptr);
  ~QueryBookForm();

private:
  void handleSearchButtonClicked();

  Ui::QueryBookForm *UI;
};

#endif // QUERYBOOKFORM_H
