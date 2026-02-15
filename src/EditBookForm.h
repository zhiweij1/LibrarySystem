#ifndef EDITBOOKFORM_H
#define EDITBOOKFORM_H

#include <QWidget>

namespace Ui {
class EditBookForm;
} // namespace Ui

class EditBookForm : public QWidget {
  Q_OBJECT

public:
  explicit EditBookForm(QWidget *Parent = nullptr);
  ~EditBookForm();

private:
  void handleSelectCSVFileButtonClicked();
  void handleLoadFromCSVButtonClicked();

  Ui::EditBookForm *UI;
};

#endif // EDITBOOKFORM_H
