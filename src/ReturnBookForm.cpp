#include "ReturnBookForm.h"
#include "CoverPreview.h"
#include "ReaderPicker.h"
#include "ui_ReturnBookForm.h"

#include "Library.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QMessageBox>
#include <QVBoxLayout>

ReturnBookForm::ReturnBookForm(QWidget *Parent)
    : QWidget(Parent), UI(new Ui::ReturnBookForm) {
  UI->setupUi(this);
  connect(UI->ReaderNumberPushButton, &QPushButton::clicked, this,
          &ReturnBookForm::handleReaderNumberPushButtonClicked);
  connect(UI->SubmitButton, &QPushButton::clicked, this,
          &ReturnBookForm::handleSubmitButtonClicked);

  connect(UI->ReaderNumberLineEdit, &QLineEdit::returnPressed, this,
          &ReturnBookForm::handleReaderNumberPushButtonClicked);

  UI->BorrowingTableWidget->setColumnCount(7);
  UI->BorrowingTableWidget->setHorizontalHeaderLabels(
      {"封面", "条码号", "书名", "借书日期", "应还日期", "归还", "续借"});

  UI->BorrowingTableWidget->setColumnWidth(0, 100);
  UI->BorrowingTableWidget->setColumnWidth(1, 150);
  UI->BorrowingTableWidget->setColumnWidth(2, 200);
  UI->BorrowingTableWidget->horizontalHeader()->setSectionResizeMode(
      QHeaderView::Stretch);

  UI->BorrowingTableWidget->verticalHeader()->setDefaultSectionSize(100);
}

ReturnBookForm::~ReturnBookForm() { delete UI; }

void ReturnBookForm::handleReaderNumberPushButtonClicked() {
  QString Input = UI->ReaderNumberLineEdit->text().trimmed();
  if (Input.isEmpty())
    return;

  // 1) 先按读者卡号查询
  auto ReaderErrOr = LibrarySystem::getInstance().getReaderByCardNumber(Input);
  if (ReaderErrOr) {
    loadReaderBorrowings(ReaderErrOr.getValue());
    return;
  }

  // 2) 手机号精确匹配（家庭共用号码可能多人命中，需人工选择）
  auto PhoneErrOr = LibrarySystem::getInstance().searchReadersByPhone(Input);
  if (PhoneErrOr && !PhoneErrOr.getValue().isEmpty()) {
    QVector<Reader> Matches = PhoneErrOr.getValue();
    Reader Target = Matches.first();
    if (Matches.size() > 1) {
      auto Picked = pickReader(this, Matches);
      if (!Picked)
        return;
      Target = *Picked;
    }
    loadReaderBorrowings(Target);
    // 回填卡号，提交操作后的自动刷新仍按卡号命中
    UI->ReaderNumberLineEdit->setText(Target.CardNumber);
    return;
  }

  // 3) 再按书籍条码查询（扫码还书）
  auto DetailErrOr =
      LibrarySystem::getInstance().getBorrowingDetailByBarcode(Input);
  if (DetailErrOr) {
    std::pair<BorrowDetailType, Reader> Pair = DetailErrOr.getValue();
    // 加载该读者的在借列表（保留已有勾选），并勾选本书"归还"
    loadReaderBorrowings(Pair.second, true);

    for (int Row = 0; Row < UI->BorrowingTableWidget->rowCount(); ++Row) {
      QTableWidgetItem *Item = UI->BorrowingTableWidget->item(Row, 1);
      if (Item && Item->text() == Input) {
        auto *CBReturn = qobject_cast<QCheckBox *>(
            UI->BorrowingTableWidget->cellWidget(Row, 5));
        if (CBReturn)
          CBReturn->setChecked(true);
        UI->BorrowingTableWidget->scrollToItem(
            Item, QAbstractItemView::PositionAtCenter);
        break;
      }
    }

    // 输入框回填读者卡号，提交操作后的刷新仍按读者查询
    UI->ReaderNumberLineEdit->setText(Pair.second.CardNumber);
    qInfo() << QString("[扫码还书]条码 [%1] 定位到读者 [%2] 并勾选归还")
                   .arg(Input, Pair.second.Name);
    return;
  }

  // 条码存在但不可还（如在馆、遗失），提示具体原因
  if (LibrarySystem::getInstance().getBookDataByBarcode(Input)) {
    QMessageBox::warning(this, "warning", DetailErrOr.getErrMsg());
    return;
  }

  QMessageBox::warning(this, "warning",
                       "未找到该编号对应的读者或条码: " + Input);

}

void ReturnBookForm::loadReaderBorrowings(const Reader &R,
                                          bool PreserveChecks) {
  UI->ReaderInfoLabel->setText(QString("姓名：%1\n卡号：%2\n电话：%3")
                                   .arg(R.Name, R.CardNumber, R.PhoneNumber));

  auto DetailsErrOr =
      LibrarySystem::getInstance().getBorrowingDetailsByReader(R.ID);
  if (!DetailsErrOr) {
    QMessageBox::warning(this, "warning",
                         "查询借阅记录失败: " + DetailsErrOr.getErrMsg());
    return;
  }
  QVector<BorrowDetailType> Details = DetailsErrOr.getValue();

  // 记录当前勾选状态（同一读者连续扫码还书时保留已勾选项）
  QHash<QString, int> OldChecks; // 条码 -> 1 归还 / 2 续借
  if (PreserveChecks) {
    for (int Row = 0; Row < UI->BorrowingTableWidget->rowCount(); ++Row) {
      QTableWidgetItem *Item = UI->BorrowingTableWidget->item(Row, 1);
      if (!Item)
        continue;
      auto *CBReturn = qobject_cast<QCheckBox *>(
          UI->BorrowingTableWidget->cellWidget(Row, 5));
      auto *CBRenew = qobject_cast<QCheckBox *>(
          UI->BorrowingTableWidget->cellWidget(Row, 6));
      if (CBReturn && CBReturn->isChecked())
        OldChecks[Item->text()] = 1;
      else if (CBRenew && CBRenew->isChecked())
        OldChecks[Item->text()] = 2;
    }
  }

  // 清理旧的 cellWidget 和 QButtonGroup（避免内存泄漏）
  // 注意：QTableWidget::setRowCount(0) 不会删除 cellWidget，必须手动删除
  for (int Row = 0; Row < UI->BorrowingTableWidget->rowCount(); ++Row) {
    delete UI->BorrowingTableWidget->cellWidget(Row,
                                                0); // 封面 CoverPreviewLabel
    delete UI->BorrowingTableWidget->cellWidget(Row, 5); // 归还 QCheckBox
    delete UI->BorrowingTableWidget->cellWidget(Row, 6); // 续借 QCheckBox
  }
  UI->BorrowingTableWidget->setRowCount(0);
  qDeleteAll(RowGroups);
  RowGroups.clear();

  for (const auto &Detail : std::as_const(Details)) {
    int Row = UI->BorrowingTableWidget->rowCount();
    UI->BorrowingTableWidget->insertRow(Row);

    CoverPreviewLabel *ImgLabel =
        new CoverPreviewLabel(Detail.Info.CoverPath, 60, 80);
    UI->BorrowingTableWidget->setCellWidget(Row, 0, ImgLabel);

    QTableWidgetItem *BarcodeItem = new QTableWidgetItem(Detail.Copy.Barcode);
    BarcodeItem->setData(Qt::UserRole, Detail.Record.ID);   // 借阅记录 ID
    BarcodeItem->setData(Qt::UserRole + 1, Detail.Copy.ID); // 副本 ID
    UI->BorrowingTableWidget->setItem(Row, 1, BarcodeItem);

    UI->BorrowingTableWidget->setItem(Row, 2,
                                      new QTableWidgetItem(Detail.Info.Title));
    UI->BorrowingTableWidget->setItem(
        Row, 3,
        new QTableWidgetItem(Detail.Record.BorrowDate.toString("yyyy-MM-dd")));
    UI->BorrowingTableWidget->setItem(
        Row, 4,
        new QTableWidgetItem(Detail.Record.DueDate.toString("yyyy-MM-dd")));

    if (Detail.Record.DueDate.date() < QDate::currentDate()) {
      UI->BorrowingTableWidget->item(Row, 4)->setForeground(Qt::red);
      UI->BorrowingTableWidget->item(Row, 4)->setText(
          Detail.Record.DueDate.toString("yyyy-MM-dd") + " [已逾期]");
    }

    QCheckBox *CBReturn = new QCheckBox("归还");
    QCheckBox *CBRenew = new QCheckBox("续借");

    QButtonGroup *Group = new QButtonGroup(this);
    Group->addButton(CBReturn);
    Group->addButton(CBRenew);
    Group->setExclusive(false);

    // 互斥：勾选一个时取消另一个，但允许再次点击取消勾选
    connect(CBReturn, &QCheckBox::toggled, [CBRenew](bool Checked) {
      if (Checked)
        CBRenew->setChecked(false);
    });
    connect(CBRenew, &QCheckBox::toggled, [CBReturn](bool Checked) {
      if (Checked)
        CBReturn->setChecked(false);
    });

    UI->BorrowingTableWidget->setCellWidget(Row, 5, CBReturn);
    UI->BorrowingTableWidget->setCellWidget(Row, 6, CBRenew);

    RowGroups.insert(Row, Group);
  }

  // 恢复勾选状态
  if (PreserveChecks) {
    for (int Row = 0; Row < UI->BorrowingTableWidget->rowCount(); ++Row) {
      int State = OldChecks.value(
          UI->BorrowingTableWidget->item(Row, 1)->text(), 0);
      if (State == 0)
        continue;
      auto *CB = qobject_cast<QCheckBox *>(
          UI->BorrowingTableWidget->cellWidget(Row, State == 1 ? 5 : 6));
      if (CB)
        CB->setChecked(true);
    }
  }
}

void ReturnBookForm::handleSubmitButtonClicked() {
  int RowCount = UI->BorrowingTableWidget->rowCount();
  if (RowCount == 0)
    return;

  struct ActionItem {
    int RecordID;
    int CopyID;
    QString Barcode;
    QString Title;
  };
  QList<ActionItem> ReturnList;
  QList<ActionItem> RenewList;

  for (int Row = 0; Row < RowCount; ++Row) {
    QButtonGroup *Group = RowGroups.value(Row);
    if (!Group)
      continue;

    QAbstractButton *Checked = Group->checkedButton();
    if (!Checked)
      continue;

    QTableWidgetItem *BarcodeItem = UI->BorrowingTableWidget->item(Row, 1);
    ActionItem Item;
    Item.RecordID = BarcodeItem->data(Qt::UserRole).toInt();
    Item.CopyID = BarcodeItem->data(Qt::UserRole + 1).toInt();
    Item.Barcode = BarcodeItem->text();
    Item.Title = UI->BorrowingTableWidget->item(Row, 2)->text();

    if (Checked->text() == "归还") {
      ReturnList.append(Item);
    } else if (Checked->text() == "续借") {
      RenewList.append(Item);
    }
  }

  if (ReturnList.isEmpty() && RenewList.isEmpty()) {
    QMessageBox::information(this, "information", "请先选择归还或续借的操作。");
    return;
  }

  QDialog ConfirmDlg(this);
  ConfirmDlg.setWindowTitle("操作最终确认");
  ConfirmDlg.setMinimumSize(600, 500);
  QVBoxLayout *Layout = new QVBoxLayout(&ConfirmDlg);

  Layout->addWidget(new QLabel("<b>读者：</b>" + UI->ReaderInfoLabel->text()));

  if (!ReturnList.isEmpty()) {
    Layout->addWidget(
        new QLabel("<b style='color:green;'>以下书籍将办理【归还】：</b>"));
    QTableWidget *ReturnTable =
        new QTableWidget(ReturnList.size(), 2, &ConfirmDlg);
    ReturnTable->setHorizontalHeaderLabels({"条码号", "书名"});
    ReturnTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    for (int Idx = 0; Idx < ReturnList.size(); ++Idx) {
      ReturnTable->setItem(Idx, 0,
                           new QTableWidgetItem(ReturnList[Idx].Barcode));
      ReturnTable->setItem(Idx, 1, new QTableWidgetItem(ReturnList[Idx].Title));
    }
    Layout->addWidget(ReturnTable);
  }

  if (!RenewList.isEmpty()) {
    Layout->addWidget(
        new QLabel("<b style='color:blue;'>以下书籍将办理【续借】：</b>"));
    QTableWidget *RenewTable =
        new QTableWidget(RenewList.size(), 2, &ConfirmDlg);
    RenewTable->setHorizontalHeaderLabels({"条码号", "书名"});
    RenewTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    for (int Idx = 0; Idx < RenewList.size(); ++Idx) {
      RenewTable->setItem(Idx, 0, new QTableWidgetItem(RenewList[Idx].Barcode));
      RenewTable->setItem(Idx, 1, new QTableWidgetItem(RenewList[Idx].Title));
    }
    Layout->addWidget(RenewTable);
  }

  QDialogButtonBox *BtnBox =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  Layout->addWidget(BtnBox);
  connect(BtnBox, &QDialogButtonBox::accepted, &ConfirmDlg, &QDialog::accept);
  connect(BtnBox, &QDialogButtonBox::rejected, &ConfirmDlg, &QDialog::reject);

  if (ConfirmDlg.exec() == QDialog::Accepted) {
    // 1. 收集 ID 列表
    QList<int> ReturnIds;
    for (const auto &Item : std::as_const(ReturnList))
      ReturnIds.append(Item.RecordID);

    QList<int> RenewIds;
    for (const auto &Item : std::as_const(RenewList))
      RenewIds.append(Item.RecordID);

    QStringList TotalErrors;

    // 2. 在同一事务中执行批量归还和续借，保证原子性
    auto Err =
        LibrarySystem::getInstance().returnAndRenewBooks(ReturnIds, RenewIds);
    if (!Err) {
      TotalErrors.append(Err.getErrMsg());
    }

    // 3. 反馈结果
    if (TotalErrors.isEmpty()) {
      int TotalCount = ReturnIds.size() + RenewIds.size();
      QMessageBox::information(
          this, "information",
          QString("已成功处理全部 %1 项操作").arg(TotalCount));
      qInfo() << QString("[还书/续借]已成功处理全部 %1 项操作").arg(TotalCount);
    } else {
      QMessageBox::critical(this, "critical",
                            "部分或全部操作失败: " + TotalErrors.join("\n"));
      qCritical() << "[还书/续借]部分或全部操作失败: " + TotalErrors.join("\n");
    }

    // 4. 刷新界面
    handleReaderNumberPushButtonClicked();
  }
}
