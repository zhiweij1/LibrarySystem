#include "QueryBookForm.h"
#include "ui_QueryBookForm.h"

#include "Library.h"

#include <QMessageBox>

QueryBookForm::QueryBookForm(QWidget *Parent)
    : QWidget(Parent), UI(new Ui::QueryBookForm) {
  UI->setupUi(this);

  connect(UI->SearchButton, &QPushButton::clicked, this,
          &QueryBookForm::handleSearchButtonClicked);
  connect(UI->BarcodeLineEdit, &QLineEdit::returnPressed, this,
          &QueryBookForm::handleSearchButtonClicked);
  connect(UI->TitleLineEdit, &QLineEdit::returnPressed, this,
          &QueryBookForm::handleSearchButtonClicked);
  connect(UI->AuthorLineEdit, &QLineEdit::returnPressed, this,
          &QueryBookForm::handleSearchButtonClicked);
  connect(UI->PublisherLineEdit, &QLineEdit::returnPressed, this,
          &QueryBookForm::handleSearchButtonClicked);
}

QueryBookForm::~QueryBookForm() { delete UI; }

void QueryBookForm::handleSearchButtonClicked() {
  QString Barcode = UI->BarcodeLineEdit->text().trimmed();
  QString Title = UI->TitleLineEdit->text().trimmed();
  QString Author = UI->AuthorLineEdit->text().trimmed();
  QString Publisher = UI->PublisherLineEdit->text().trimmed();

  auto ResultsErrOr = LibrarySystem::getInstance().queryBooks(
      Barcode, Title, Author, Publisher);
  if (!ResultsErrOr) {
    QMessageBox::warning(this, "warning",
                         "无法查到书籍: " + ResultsErrOr.getErrMsg());
    return;
  }

  QVector<std::pair<BorrowDetailType, Reader>> Results =
      ResultsErrOr.getValue();

  UI->ResultTableWidget->setRowCount(0);
  UI->ResultTableWidget->setColumnCount(10);
  UI->ResultTableWidget->setHorizontalHeaderLabels(
      {"封面", "条码", "书名", "作者", "出版社", "状态", "借出日期",
       "预计归还日期", "读者名字", "读者号"});

  UI->ResultTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
  UI->ResultTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
  UI->ResultTableWidget->verticalHeader()->setDefaultSectionSize(80);

  for (const auto &Item : std::as_const(Results)) {
    int Row = UI->ResultTableWidget->rowCount();
    UI->ResultTableWidget->insertRow(Row);

    QLabel *ImgLabel = new QLabel();
    QString FullPath = QCoreApplication::applicationDirPath() + "/" +
                       Item.first.Info.CoverPath;
    QPixmap Pix(FullPath);
    if (Pix.isNull()) {
      ImgLabel->setText("无封面");
    } else {
      ImgLabel->setPixmap(
          Pix.scaled(60, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    ImgLabel->setAlignment(Qt::AlignCenter);
    UI->ResultTableWidget->setCellWidget(Row, 0, ImgLabel);
    UI->ResultTableWidget->setItem(
        Row, 1, new QTableWidgetItem(Item.first.Copy.Barcode));
    UI->ResultTableWidget->setItem(Row, 2,
                                   new QTableWidgetItem(Item.first.Info.Title));
    UI->ResultTableWidget->setItem(
        Row, 3, new QTableWidgetItem(Item.first.Info.Author));
    UI->ResultTableWidget->setItem(
        Row, 4, new QTableWidgetItem(Item.first.Info.Publisher));

    QTableWidgetItem *StatusItem = new QTableWidgetItem();
    if (Item.first.Copy.Status == BookCopy::BookStatus::BS_InLibrary) {
      StatusItem->setText("在馆");
      StatusItem->setForeground(Qt::darkGreen);
    } else if (Item.first.Copy.Status == BookCopy::BookStatus::BS_Borrowed) {
      StatusItem->setText("借出");
      StatusItem->setForeground(Qt::yellow);
    } else {
      StatusItem->setText("遗失");
      StatusItem->setForeground(Qt::red);
    }
    StatusItem->setTextAlignment(Qt::AlignCenter);
    UI->ResultTableWidget->setItem(Row, 5, StatusItem);

    if (Item.first.Copy.Status == BookCopy::BookStatus::BS_Borrowed) {
      UI->ResultTableWidget->setItem(
          Row, 6,
          new QTableWidgetItem(
              Item.first.Record.BorrowDate.toString("yyyy-MM-dd")));
      UI->ResultTableWidget->setItem(
          Row, 7,
          new QTableWidgetItem(
              Item.first.Record.DueDate.toString("yyyy-MM-dd")));
      UI->ResultTableWidget->setItem(Row, 8,
                                     new QTableWidgetItem(Item.second.Name));
      UI->ResultTableWidget->setItem(
          Row, 9, new QTableWidgetItem(Item.second.CardNumber));
    }
  }

  UI->ResultTableWidget->horizontalHeader()->setSectionResizeMode(
      QHeaderView::Stretch);

  UI->ResultTableWidget->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::Fixed);
  UI->ResultTableWidget->setColumnWidth(0, 80);
}
