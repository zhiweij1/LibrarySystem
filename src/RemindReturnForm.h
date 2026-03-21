#ifndef REMINDRETURNFORM_H
#define REMINDRETURNFORM_H

#include <QDate>
#include <QWidget>
#include <QVector>
#include "Library.h"

namespace Ui {
class RemindReturnForm;
} // namespace Ui

class RemindReturnForm : public QWidget {
  Q_OBJECT

public:
  explicit RemindReturnForm(QWidget *Parent = nullptr);
  ~RemindReturnForm();

private slots:
  void handleQueryClicked();

private:
  void loadData();
  void clearCards();
  QWidget *createReaderCard(const LibrarySystem::ReaderBorrowInfo &Info);
  QWidget *createBookItem(const BorrowDetailType &Book, const QDate &Today, bool IsOther = false);

  Ui::RemindReturnForm *UI;
  QVector<LibrarySystem::ReaderBorrowInfo> ReaderInfos;
};

#endif // REMINDRETURNFORM_H
