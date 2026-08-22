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

  UI->BorrowingTableWidget->setColumnCount(6);
  UI->BorrowingTableWidget->setHorizontalHeaderLabels(
      {"封面", "条码号", "书名", "借书日期", "应还日期", "归还"});

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

  // 3) 姓名模糊匹配（重名需人工选择）
  auto NameErrOr = LibrarySystem::getInstance().searchReadersByName(Input);
  if (NameErrOr && !NameErrOr.getValue().isEmpty()) {
    QVector<Reader> Matches = NameErrOr.getValue();
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

  // 4) 再按书籍条码查询（扫码还书）
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
  CurrentReader = R;
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
  QSet<QString> OldChecked; // 已勾选归还的条码
  if (PreserveChecks) {
    for (int Row = 0; Row < UI->BorrowingTableWidget->rowCount(); ++Row) {
      QTableWidgetItem *Item = UI->BorrowingTableWidget->item(Row, 1);
      if (!Item)
        continue;
      auto *CBReturn = qobject_cast<QCheckBox *>(
          UI->BorrowingTableWidget->cellWidget(Row, 5));
      if (CBReturn && CBReturn->isChecked())
        OldChecked.insert(Item->text());
    }
  }

  // 清理旧的 cellWidget（避免内存泄漏）
  // 注意：QTableWidget::setRowCount(0) 不会删除 cellWidget，必须手动删除
  for (int Row = 0; Row < UI->BorrowingTableWidget->rowCount(); ++Row) {
    delete UI->BorrowingTableWidget->cellWidget(Row,
                                                0); // 封面 CoverPreviewLabel
    delete UI->BorrowingTableWidget->cellWidget(Row, 5); // 归还 QCheckBox
  }
  UI->BorrowingTableWidget->setRowCount(0);

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
    UI->BorrowingTableWidget->setCellWidget(Row, 5, CBReturn);
  }

  // 恢复勾选状态
  if (PreserveChecks) {
    for (int Row = 0; Row < UI->BorrowingTableWidget->rowCount(); ++Row) {
      if (!OldChecked.contains(
              UI->BorrowingTableWidget->item(Row, 1)->text()))
        continue;
      auto *CBReturn = qobject_cast<QCheckBox *>(
          UI->BorrowingTableWidget->cellWidget(Row, 5));
      if (CBReturn)
        CBReturn->setChecked(true);
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

  for (int Row = 0; Row < RowCount; ++Row) {
    auto *CBReturn = qobject_cast<QCheckBox *>(
        UI->BorrowingTableWidget->cellWidget(Row, 5));
    if (!CBReturn || !CBReturn->isChecked())
      continue;

    QTableWidgetItem *BarcodeItem = UI->BorrowingTableWidget->item(Row, 1);
    ActionItem Item;
    Item.RecordID = BarcodeItem->data(Qt::UserRole).toInt();
    Item.CopyID = BarcodeItem->data(Qt::UserRole + 1).toInt();
    Item.Barcode = BarcodeItem->text();
    Item.Title = UI->BorrowingTableWidget->item(Row, 2)->text();

    ReturnList.append(Item);
  }

  if (ReturnList.isEmpty()) {
    QMessageBox::information(this, "information", "请先勾选需要归还的书籍。");
    return;
  }

  QDialog ConfirmDlg(this);
  ConfirmDlg.setWindowTitle("确认归还清单");
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

    QStringList TotalErrors;

    // 2. 在同一事务中执行批量归还，保证原子性
    auto Err = LibrarySystem::getInstance().returnBooks(ReturnIds);
    if (!Err) {
      TotalErrors.append(Err.getErrMsg());
    }

    // 3. 反馈结果
    if (TotalErrors.isEmpty()) {
      QMessageBox::information(
          this, "information",
          QString("已成功归还全部 %1 项").arg(ReturnIds.size()));
      qInfo() << QString("[还书]已成功归还全部 %1 项").arg(ReturnIds.size());
    } else {
      QMessageBox::critical(this, "critical",
                            "归还失败: " + TotalErrors.join("\n"));
      qCritical() << "[还书]归还失败: " << TotalErrors.join("\n");
    }

    // 4. 刷新界面（按记住的读者刷新，不依赖输入框内容）
    if (CurrentReader)
      loadReaderBorrowings(*CurrentReader);
  }
}
