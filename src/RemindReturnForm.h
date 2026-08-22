#ifndef REMINDRETURNFORM_H
#define REMINDRETURNFORM_H

#include "Library.h"
#include <QDate>
#include <QTimer>
#include <QVector>
#include <QWidget>

namespace Ui {
class RemindReturnForm;
} // namespace Ui

class RemindReturnForm : public QWidget {
  Q_OBJECT

public:
  explicit RemindReturnForm(QWidget *Parent = nullptr);
  ~RemindReturnForm();

protected:
  // 页面被切换显示时自动刷新，保证催还数据为最新
  void showEvent(QShowEvent *Event) override;

private slots:
  void handleQueryClicked();

private:
  void loadData();
  void clearCards();
  QWidget *createReaderCard(const LibrarySystem::ReaderBorrowInfo &Info);
  QWidget *createBookItem(const BorrowDetailType &Book, const QDate &Today,
                          bool IsOther = false);

  Ui::RemindReturnForm *UI;
  QTimer *SpinTimer = nullptr; // 天数调节防抖，避免长按连续重建卡片
  QVector<LibrarySystem::ReaderBorrowInfo> ReaderInfos;
};

#endif // REMINDRETURNFORM_H
