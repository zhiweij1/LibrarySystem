#ifndef BORROWBOOKFORM_H
#define BORROWBOOKFORM_H

#include "Library.h"

#include <QWidget>

namespace Ui {
class BorrowBookForm;
} // namespace Ui

class BorrowBookForm : public QWidget {
  Q_OBJECT

public:
  explicit BorrowBookForm(QWidget *Parent = nullptr);
  ~BorrowBookForm();

private:
  void handleBookAddButtonClicked();
  void handleSubmitButtonClicked();
  void handleReaderNumberButtonClicked();

  Ui::BorrowBookForm *UI;
  std::optional<Reader> RdrOpt = std::nullopt;
};

#endif // BORROWBOOKFORM_H
