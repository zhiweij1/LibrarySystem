#ifndef EDITBOOKFORM_H
#define EDITBOOKFORM_H

#include <QWidget>
#include "Library.h"

namespace Ui {
class EditBookForm;
} // namespace Ui

class EditBookForm : public QWidget {
  Q_OBJECT

public:
  explicit EditBookForm(QWidget *Parent = nullptr);
  ~EditBookForm();

private slots:
  void handleSelectCSVFileButtonClicked();
  void handleLoadFromCSVButtonClicked();
  void handleQueryButtonClicked();
  void handleModifyStatusButtonClicked();

private:
  void updateBookInfoDisplay(const BookInfo &Info, const BookCopy &Copy);
  void clearBookInfoDisplay();

  Ui::EditBookForm *UI;
  QString CurrentBarcode;
};

#endif // EDITBOOKFORM_H
