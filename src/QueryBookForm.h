#ifndef QUERYBOOKFORM_H
#define QUERYBOOKFORM_H

#include <QWidget>
#include <QVector>
#include "Library.h"

namespace Ui {
class QueryBookForm;
} // namespace Ui

class QueryBookForm : public QWidget {
  Q_OBJECT

public:
  explicit QueryBookForm(QWidget *Parent = nullptr);
  ~QueryBookForm();

private slots:
  void handleSearchButtonClicked();
  void handleStatusFilterChanged(int Index);
  void handleCellClicked(int Row, int Column);
  void handlePrevPageClicked();
  void handleNextPageClicked();
  void handleSectionResized(int LogicalIndex, int OldSize, int NewSize);

private:
  void loadData();
  void setupTableColumns();
  void updateTable();
  void updatePageInfo();

  Ui::QueryBookForm *UI;
  QVector<std::pair<BorrowDetailType, Reader>> AllResults;
  QVector<std::pair<BorrowDetailType, Reader>> FilteredResults;
  int CurrentPage = 1;
  static const int PageSize = 20;
  bool IsResizing = false;        // 防止递归调用
  bool ColumnsInitialized = false; // 列宽是否已初始化
};

#endif // QUERYBOOKFORM_H
