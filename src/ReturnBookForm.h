#ifndef RETURNBOOKFORM_H
#define RETURNBOOKFORM_H

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

  Ui::ReturnBookForm *UI;
  QMap<int, QButtonGroup *> RowGroups;
};

#endif // RETURNBOOKFORM_H
