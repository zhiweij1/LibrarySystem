#ifndef RETURNBOOKFORM_H
#define RETURNBOOKFORM_H

#include "Library.h"

#include <QButtonGroup>
#include <QWidget>

namespace Ui {
class ReturnBookForm;
} // namespace Ui

class ReturnBookForm : public QWidget {
  Q_OBJECT

public:
  explicit ReturnBookForm(QWidget *Parent = nullptr);
  ~ReturnBookForm();

private:
  void handleReaderNumberPushButtonClicked();
  void handleSubmitButtonClicked();
  void loadReaderBorrowings(const Reader &R, bool PreserveChecks = false);

  Ui::ReturnBookForm *UI;
  QMap<int, QButtonGroup *> RowGroups;
};

#endif // RETURNBOOKFORM_H
