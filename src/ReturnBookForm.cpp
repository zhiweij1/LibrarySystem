#include "ReturnBookForm.h"
#include "CoverPreview.h"
#include "ui_ReturnBookForm.h"

#include "Library.h"

#include <QButtonGroup>
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

  connect(UI->ReaderNumerLineEdit, &QLineEdit::returnPressed, this,
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
  QString CardNumber = UI->ReaderNumerLineEdit->text().trimmed();
  auto ReaderErrOr =
      LibrarySystem::getInstance().getReaderByCardNumber(CardNumber);

  if (!ReaderErrOr) {
    QMessageBox::warning(this, "warning",
                         "查询读者号失败: " + ReaderErrOr.getErrMsg());
    return;
  }

  auto CurrentReader = ReaderErrOr.getValue();
  UI->ReaderInfoLabel->setText(QString("姓名：%1\n卡号：%2\n电话：%3")
                                   .arg(CurrentReader.Name,
                                        CurrentReader.CardNumber,
                                        CurrentReader.PhoneNumber));

  auto DetailsErrOr = LibrarySystem::getInstance().getBorrowingDetailsByReader(
      CurrentReader.ID);
  if (!DetailsErrOr) {
    QMessageBox::warning(this, "warning",
                         "查询借阅记录失败: " + DetailsErrOr.getErrMsg());
    return;
  }
  QVector<BorrowDetailType> Details = DetailsErrOr.getValue();

  UI->BorrowingTableWidget->setRowCount(0);
  RowGroups.clear();

  for (const auto &Detail : std::as_const(Details)) {
    int Row = UI->BorrowingTableWidget->rowCount();
    UI->BorrowingTableWidget->insertRow(Row);

    CoverPreviewLabel *ImgLabel =
        new CoverPreviewLabel(Detail.Info.CoverPath, 60, 80);
    UI->BorrowingTableWidget->setCellWidget(Row, 0, ImgLabel);

    QTableWidgetItem *BarcodeItem = new QTableWidgetItem(Detail.Copy.Barcode);
    BarcodeItem->setData(Qt::UserRole, Detail.Record.ID);   // recod ID
    BarcodeItem->setData(Qt::UserRole + 1, Detail.Copy.ID); // copy ID
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
    connect(CBReturn, &QCheckBox::clicked, [CBReturn, CBRenew](bool Checked) {
      if (Checked) {
        CBRenew->setChecked(false);
      }
    });
    connect(CBRenew, &QCheckBox::clicked, [CBReturn, CBRenew](bool Checked) {
      if (Checked) {
        CBReturn->setChecked(false);
      }
    });

    UI->BorrowingTableWidget->setCellWidget(Row, 5, CBReturn);
    UI->BorrowingTableWidget->setCellWidget(Row, 6, CBRenew);

    RowGroups.insert(Row, Group);
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

    // 2. 执行批量归还
    if (!ReturnIds.isEmpty()) {
      auto Err = LibrarySystem::getInstance().returnBooks(ReturnIds);
      if (!Err)
        TotalErrors.append(Err.getErrMsg());
    }

    // 3. 执行批量续借
    if (!RenewIds.isEmpty()) {
      auto Err = LibrarySystem::getInstance().renewBooks(RenewIds);
      if (!Err)
        TotalErrors.append(Err.getErrMsg());
    }

    // 4. 反馈结果
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

    // 5. 刷新界面
    handleReaderNumberPushButtonClicked();
  }
}
