#ifndef RETURNBOOKFORM_H
#define RETURNBOOKFORM_H

#include "Library.h"

#include <QWidget>

#include <optional>

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
  std::optional<Reader> CurrentReader; // 最近加载的读者，提交后刷新不依赖输入框
};

#endif // RETURNBOOKFORM_H
