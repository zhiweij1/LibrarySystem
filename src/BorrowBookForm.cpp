#include "BorrowBookForm.h"
#include "CoverPreview.h"
#include "ui_BorrowBookForm.h"

#include <QMessageBox>
#include <QVBoxLayout>

BorrowBookForm::BorrowBookForm(QWidget *Parent)
    : QWidget(Parent), UI(new Ui::BorrowBookForm) {
  UI->setupUi(this);
  connect(UI->ReaderNumButton, &QPushButton::clicked, this,
          &BorrowBookForm::handleReaderNumberButtonClicked);
  connect(UI->SubmitButton, &QPushButton::clicked, this,
          &BorrowBookForm::handleSubmitButtonClicked);
  connect(UI->BookNumAddButton, &QPushButton::clicked, this,
          &BorrowBookForm::handleBookAddButtonClicked);

  connect(UI->ReaderNumLineEdit, &QLineEdit::returnPressed, this,
          &BorrowBookForm::handleReaderNumberButtonClicked);
  connect(UI->BookNumLineEdit, &QLineEdit::returnPressed, this,
          &BorrowBookForm::handleBookAddButtonClicked);

  UI->BookListTableWidget->setColumnCount(7);
  UI->BookListTableWidget->setHorizontalHeaderLabels(
      {"封面", "条码号", "书名", "作者", "出版社", "分类号", "操作"});
  UI->BookListTableWidget->setColumnWidth(0, 100); // 封面
  UI->BookListTableWidget->setColumnWidth(1, 200); // 条码号
  UI->BookListTableWidget->setColumnWidth(2, 200); // 书名
  UI->BookListTableWidget->setColumnWidth(3, 200); // 作者
  UI->BookListTableWidget->setColumnWidth(4, 200); // 出版社
  UI->BookListTableWidget->setColumnWidth(5, 150); // 分类号
  UI->BookListTableWidget->horizontalHeader()->setSectionResizeMode(
      6, QHeaderView::Stretch);
  UI->BookListTableWidget->verticalHeader()->setDefaultSectionSize(100);
}

BorrowBookForm::~BorrowBookForm() { delete UI; }

void BorrowBookForm::handleBookAddButtonClicked() {
  QString Barcode = UI->BookNumLineEdit->text().trimmed();
  auto BookDataErrOr =
      LibrarySystem::getInstance().getBookDataByBarcode(Barcode);

  if (!BookDataErrOr) {
    QMessageBox::warning(this, "warning", "未找到该编号的书籍");
    return;
  }
  const auto &BookData = BookDataErrOr.getValue();

  if (BookData.second.Status == BookCopy::BookStatus::BS_Borrowed) {
    QMessageBox::warning(
        this, "warning",
        QString("条码 [%1] 对应的书籍目前处于'借出'状态").arg(Barcode));
    return;
  }
  if (BookData.second.Status == BookCopy::BookStatus::BS_Lost) {
    QMessageBox::warning(
        this, "warning",
        QString("条码 [%1] 对应的书籍目前处于'遗失'状态").arg(Barcode));
    return;
  }
  if (BookData.second.Status == BookCopy::BookStatus::BS_NonLendable) {
    QMessageBox::warning(
        this, "warning",
        QString("条码 [%1] 对应的书籍目前处于'非外借书'状态").arg(Barcode));
    return;
  }
  if (BookData.second.Status == BookCopy::BookStatus::BS_Unkown_Status) {
    QMessageBox::warning(
        this, "warning",
        QString("条码 [%1] 对应的书籍目前处于'未知状态'").arg(Barcode));
    return;
  }
  for (int Idx = 0; Idx < UI->BookListTableWidget->rowCount(); ++Idx) {
    if (UI->BookListTableWidget->item(Idx, 1)->text() == Barcode) {
      QMessageBox::warning(
          this, "warning",
          QString("条码 [%1] 对应的书籍已在待借列表中").arg(Barcode));
      return;
    }
  }

  // 借书数量上限：已选读者时提前拦截（提交时后端仍会校验）
  if (RdrOpt && !RdrOpt->IsInactive) {
    int MaxBooks = LibrarySystem::getInstance().getMaxBooks();
    int Active = LibrarySystem::getInstance().getActiveBorrowCount(RdrOpt->ID);
    if (Active + UI->BookListTableWidget->rowCount() + 1 > MaxBooks) {
      QMessageBox::warning(
          this, "warning",
          QString("超出借书数量上限：每人最多可借 %1 本，该读者当前已借 %2 本")
              .arg(MaxBooks)
              .arg(Active));
      return;
    }
  }

  int Row = UI->BookListTableWidget->rowCount();
  UI->BookListTableWidget->insertRow(Row);

  // 添加书籍封面
  CoverPreviewLabel *ImgLabel =
      new CoverPreviewLabel(BookData.first.CoverPath, 60, 80);
  UI->BookListTableWidget->setCellWidget(Row, 0, ImgLabel);

  // 添加书籍信息
  QTableWidgetItem *BarcodeItem = new QTableWidgetItem(Barcode);
  BarcodeItem->setData(Qt::UserRole, BookData.second.ID);
  UI->BookListTableWidget->setItem(Row, 1, BarcodeItem);
  UI->BookListTableWidget->setItem(Row, 2,
                                   new QTableWidgetItem(BookData.first.Title));
  UI->BookListTableWidget->setItem(Row, 3,
                                   new QTableWidgetItem(BookData.first.Author));
  UI->BookListTableWidget->setItem(
      Row, 4, new QTableWidgetItem(BookData.first.Publisher));
  UI->BookListTableWidget->setItem(Row, 5,
                                   new QTableWidgetItem(BookData.first.CLCID));

  // 添加删除按钮
  QPushButton *DelBtn = new QPushButton("删除");
  DelBtn->setStyleSheet(
      "QPushButton{color:#D32F2F;background-color:#FFEBEE;border:1px solid "
      "#FFCDD2;"
      "border-radius:4px;padding:5px 15px;}"
      "QPushButton:hover{background-color:#FFCDD2;border-color:#D32F2F;}"
      "QPushButton:pressed{background-color:#EF9A9A;border-color:#C62828;}"
      "QPushButton:disabled{color:#BDBDBD;background-color:#FFEBEE;border:1px "
      "solid #FFCDD2;}");
  UI->BookListTableWidget->setCellWidget(Row, 6, DelBtn);

  // 绑定删除操作：通过遍历 cellWidget 找到按钮所在行
  connect(DelBtn, &QPushButton::clicked, [this, DelBtn]() {
    for (int Row = 0; Row < UI->BookListTableWidget->rowCount(); ++Row) {
      if (UI->BookListTableWidget->cellWidget(Row, 6) == DelBtn) {
        delete UI->BookListTableWidget->cellWidget(Row, 0);
        UI->BookListTableWidget->removeRow(Row);
        DelBtn->deleteLater();
        break;
      }
    }
  });

  UI->BookNumLineEdit->clear();
  qInfo() << QString("添加条码 [%1] 对应的书籍成功").arg(Barcode);
}

void BorrowBookForm::handleReaderNumberButtonClicked() {
  QString CardNumber = UI->ReaderNumLineEdit->text().trimmed();
  if (CardNumber.isEmpty())
    return;

  auto ReaderErrOr =
      LibrarySystem::getInstance().getReaderByCardNumber(CardNumber);

  if (!ReaderErrOr) {
    QMessageBox::warning(this, "warning", "未找到该编号的读者");
    UI->ReaderInfoLabel->setText("未选择读者");
    UI->ReaderInfoLabel->setStyleSheet("background:transparent;color:red;");
    return;
  }

  RdrOpt = ReaderErrOr.getValue();
  UI->ReaderInfoLabel->setText(
      QString("姓名：%1\n卡号：%2\n电话：%3")
          .arg(RdrOpt->Name, RdrOpt->CardNumber, RdrOpt->PhoneNumber));

  if (RdrOpt->IsInactive) {
    UI->ReaderInfoLabel->setStyleSheet("background:transparent;color:red;");
    QMessageBox::warning(this, "warning", "该读者已注销，无法借书");
  } else {
    UI->ReaderInfoLabel->setStyleSheet("background:transparent;color:black;");

    // 待借清单 + 已借数量超出上限时提醒（提交时后端会拒绝）
    int MaxBooks = LibrarySystem::getInstance().getMaxBooks();
    int Active = LibrarySystem::getInstance().getActiveBorrowCount(RdrOpt->ID);
    int Pending = UI->BookListTableWidget->rowCount();
    if (Active + Pending > MaxBooks)
      QMessageBox::warning(
          this, "warning",
          QString("超出借书数量上限：每人最多可借 %1 本，该读者当前已借 %2 "
                  "本，待借清单中还有 %3 本，提交时将被拒绝")
              .arg(MaxBooks)
              .arg(Active)
              .arg(Pending));
  }
}

void BorrowBookForm::handleSubmitButtonClicked() {
  if (!RdrOpt) {
    QMessageBox::warning(this, "warning", "请先选择读者");
    return;
  }
  if (RdrOpt->IsInactive) {
    QMessageBox::warning(this, "warning", "该读者已注销，无法借书");
    return;
  }
  if (UI->BookListTableWidget->rowCount() == 0) {
    QMessageBox::warning(this, "warning", "请填写书目");
    return;
  }

  QDialog ConfirmDlg(this);
  ConfirmDlg.setWindowTitle("确认借书清单");
  ConfirmDlg.setMinimumSize(700, 500);

  QVBoxLayout Layout(&ConfirmDlg);
  Layout.addWidget(new QLabel("<b>确认读者信息：</b>", &ConfirmDlg));
  Layout.addWidget(new QLabel(UI->ReaderInfoLabel->text(), &ConfirmDlg));
  Layout.addWidget(new QLabel("<b>确认书籍列表：</b>", &ConfirmDlg));

  QTableWidget ConfirmTable(&ConfirmDlg);
  ConfirmTable.setColumnCount(6);
  ConfirmTable.setHorizontalHeaderLabels(
      {"封面", "条码号", "书名", "作者", "出版社", "分类号"});
  ConfirmTable.horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  ConfirmTable.verticalHeader()->setDefaultSectionSize(100);

  int RowCount = UI->BookListTableWidget->rowCount();
  ConfirmTable.setRowCount(RowCount);

  // 复制数据
  for (int Row = 0; Row < RowCount; ++Row) {
    if (auto *OldImg = dynamic_cast<CoverPreviewLabel *>(
            UI->BookListTableWidget->cellWidget(Row, 0))) {
      CoverPreviewLabel *NewImg =
          new CoverPreviewLabel(OldImg->getCoverPath(), 60, 80);
      ConfirmTable.setCellWidget(Row, 0, NewImg);
    }

    for (int Col = 1; Col < 6; ++Col) {
      if (auto *OldItem = UI->BookListTableWidget->item(Row, Col)) {
        // 只复制需要被显示的内容，data不需要复制
        ConfirmTable.setItem(Row, Col, new QTableWidgetItem(OldItem->text()));
      }
    }
  }
  Layout.addWidget(&ConfirmTable);

  QDialogButtonBox BtnBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                          &ConfirmDlg);
  Layout.addWidget(&BtnBox);
  connect(&BtnBox, &QDialogButtonBox::accepted, &ConfirmDlg, &QDialog::accept);
  connect(&BtnBox, &QDialogButtonBox::rejected, &ConfirmDlg, &QDialog::reject);

  if (ConfirmDlg.exec() == QDialog::Accepted) {
    QVector<int> CopyIDs;
    CopyIDs.reserve(RowCount);
    for (int Row = 0; Row < RowCount; ++Row) {
      // 直接从原始表格里拿ID
      CopyIDs.append(
          UI->BookListTableWidget->item(Row, 1)->data(Qt::UserRole).toInt());
    }

    auto ErrMsg = LibrarySystem::getInstance().borrowBooks(RdrOpt->ID, CopyIDs);

    if (!ErrMsg) {
      QMessageBox::critical(this, "critical", "失败: " + ErrMsg.getErrMsg());
      qCritical() << "失败: " + ErrMsg.getErrMsg();
    } else {
      QMessageBox::information(this, "information", "借书手续已全部办理完成");
      qInfo() << "借书手续已全部办理完成";
      UI->BookListTableWidget->setRowCount(0);
      UI->ReaderNumLineEdit->clear();
      UI->ReaderInfoLabel->setText("未选择读者");
      UI->ReaderInfoLabel->setStyleSheet("background:transparent;color:black;");
      RdrOpt = std::nullopt;
    }
  }
}
