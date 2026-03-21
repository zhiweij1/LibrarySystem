#include "QueryBookForm.h"
#include "CoverPreview.h"
#include "ui_QueryBookForm.h"

#include "Library.h"

#include <QCoreApplication>
#include <QMessageBox>
#include <QPushButton>
#include <QHBoxLayout>
#include <QHeaderView>

QueryBookForm::QueryBookForm(QWidget *Parent)
    : QWidget(Parent), UI(new Ui::QueryBookForm) {
  UI->setupUi(this);

  // 初始化状态筛选下拉框
  UI->StatusFilterComboBox->addItem("全部", -1);
  UI->StatusFilterComboBox->addItem("在馆", BookCopy::BS_InLibrary);
  UI->StatusFilterComboBox->addItem("借出", BookCopy::BS_Borrowed);
  UI->StatusFilterComboBox->addItem("遗失", BookCopy::BS_Lost);

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
  connect(UI->StatusFilterComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &QueryBookForm::handleStatusFilterChanged);
  connect(UI->ResultTableWidget, &QTableWidget::cellClicked, this,
          &QueryBookForm::handleCellClicked);
  connect(UI->PrevPageButton, &QPushButton::clicked, this,
          &QueryBookForm::handlePrevPageClicked);
  connect(UI->NextPageButton, &QPushButton::clicked, this,
          &QueryBookForm::handleNextPageClicked);

  // 初始化表格列（只执行一次）
  initTable();

  // 初始加载所有数据
  loadData();
}

QueryBookForm::~QueryBookForm() { delete UI; }

void QueryBookForm::loadData() {
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

  AllResults = ResultsErrOr.getValue();
  CurrentPage = 1;
  handleStatusFilterChanged(UI->StatusFilterComboBox->currentIndex());
}

void QueryBookForm::handleStatusFilterChanged(int Index) {
  Q_UNUSED(Index);
  int StatusFilter = UI->StatusFilterComboBox->currentData().toInt();

  FilteredResults.clear();
  if (StatusFilter == -1) {
    FilteredResults = AllResults;
  } else {
    for (const auto &Item : std::as_const(AllResults)) {
      if (Item.first.Copy.Status == StatusFilter) {
        FilteredResults.append(Item);
      }
    }
  }

  CurrentPage = 1;
  updateTable();
  updatePageInfo();
}

void QueryBookForm::initTable() {
  // 设置列数和表头标签
  UI->ResultTableWidget->setColumnCount(8);
  UI->ResultTableWidget->setHorizontalHeaderLabels(
      {"封面", "条码", "书名", "作者", "出版社", "状态", "借出日期", "预计归还日期"});

  UI->ResultTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
  UI->ResultTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
  UI->ResultTableWidget->verticalHeader()->setDefaultSectionSize(80);

  // 封面(0): 固定（图片大小固定）
  UI->ResultTableWidget->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::Fixed);
  UI->ResultTableWidget->setColumnWidth(0, 80);
  // 条码(1): 固定
  UI->ResultTableWidget->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::Fixed);
  UI->ResultTableWidget->setColumnWidth(1, 110);
  // 书名(2): 可拖动，给较大初始宽度
  UI->ResultTableWidget->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::Interactive);
  UI->ResultTableWidget->setColumnWidth(2, 300);
  // 作者(3): 可拖动
  UI->ResultTableWidget->horizontalHeader()->setSectionResizeMode(
      3, QHeaderView::Interactive);
  UI->ResultTableWidget->setColumnWidth(3, 150);
  // 出版社(4): 可拖动
  UI->ResultTableWidget->horizontalHeader()->setSectionResizeMode(
      4, QHeaderView::Interactive);
  UI->ResultTableWidget->setColumnWidth(4, 300);
  // 状态(5): 固定
  UI->ResultTableWidget->horizontalHeader()->setSectionResizeMode(
      5, QHeaderView::Fixed);
  UI->ResultTableWidget->setColumnWidth(5, 100);
  // 借出日期(6): 固定
  UI->ResultTableWidget->horizontalHeader()->setSectionResizeMode(
      6, QHeaderView::Fixed);
  UI->ResultTableWidget->setColumnWidth(6, 150);
  // 预计归还日期(7): 固定
  UI->ResultTableWidget->horizontalHeader()->setSectionResizeMode(
      7, QHeaderView::Fixed);
  UI->ResultTableWidget->setColumnWidth(7, 150);
}

void QueryBookForm::updateTable() {
  // 只清空数据，保留表头和列宽设置
  UI->ResultTableWidget->clearContents();
  UI->ResultTableWidget->setRowCount(0);

  int StartIndex = (CurrentPage - 1) * PageSize;
  int EndIndex = qMin(StartIndex + PageSize, FilteredResults.size());

  for (int i = StartIndex; i < EndIndex; ++i) {
    const auto &Item = FilteredResults[i];
    int Row = UI->ResultTableWidget->rowCount();
    UI->ResultTableWidget->insertRow(Row);

    CoverPreviewLabel *ImgLabel =
        new CoverPreviewLabel(Item.first.Info.CoverPath, 60, 80);
    UI->ResultTableWidget->setCellWidget(Row, 0, ImgLabel);
    UI->ResultTableWidget->setItem(
        Row, 1, new QTableWidgetItem(Item.first.Copy.Barcode));
    UI->ResultTableWidget->setItem(Row, 2,
                                   new QTableWidgetItem(Item.first.Info.Title));
    UI->ResultTableWidget->setItem(
        Row, 3, new QTableWidgetItem(Item.first.Info.Author));
    UI->ResultTableWidget->setItem(
        Row, 4, new QTableWidgetItem(Item.first.Info.Publisher));

    // 状态列使用按钮
    QWidget *StatusWidget = new QWidget();
    QHBoxLayout *StatusLayout = new QHBoxLayout(StatusWidget);
    StatusLayout->setContentsMargins(4, 4, 4, 4);
    QPushButton *StatusBtn = new QPushButton();
    StatusBtn->setMinimumHeight(30);

    if (Item.first.Copy.Status == BookCopy::BookStatus::BS_InLibrary) {
      StatusBtn->setText("在馆");
      StatusBtn->setStyleSheet("color: white; background-color: #4CAF50; border-radius: 4px;");
      StatusBtn->setEnabled(false);
    } else if (Item.first.Copy.Status == BookCopy::BookStatus::BS_Borrowed) {
      StatusBtn->setText("借出");
      StatusBtn->setStyleSheet("color: black; background-color: #FFC107; border-radius: 4px;");
      StatusBtn->setProperty("readerName", Item.second.Name);
      StatusBtn->setProperty("readerCard", Item.second.CardNumber);
      StatusBtn->setProperty("readerPhone", Item.second.PhoneNumber);
      connect(StatusBtn, &QPushButton::clicked, [this, StatusBtn]() {
        QString Info = QString("读者姓名：%1\n读者证号：%2\n联系电话：%3")
                           .arg(StatusBtn->property("readerName").toString())
                           .arg(StatusBtn->property("readerCard").toString())
                           .arg(StatusBtn->property("readerPhone").toString());
        QMessageBox::information(this, "读者信息", Info);
      });
    } else {
      StatusBtn->setText("遗失");
      StatusBtn->setStyleSheet("color: white; background-color: #F44336; border-radius: 4px;");
      StatusBtn->setEnabled(false);
    }

    StatusLayout->addWidget(StatusBtn);
    UI->ResultTableWidget->setCellWidget(Row, 5, StatusWidget);

    if (Item.first.Copy.Status == BookCopy::BookStatus::BS_Borrowed) {
      UI->ResultTableWidget->setItem(
          Row, 6,
          new QTableWidgetItem(
              Item.first.Record.BorrowDate.toString("yyyy-MM-dd")));
      UI->ResultTableWidget->setItem(
          Row, 7,
          new QTableWidgetItem(
              Item.first.Record.DueDate.toString("yyyy-MM-dd")));
    } else {
      UI->ResultTableWidget->setItem(Row, 6, new QTableWidgetItem("-"));
      UI->ResultTableWidget->setItem(Row, 7, new QTableWidgetItem("-"));
    }
  }
}

void QueryBookForm::updatePageInfo() {
  int TotalPages = qMax(1, (FilteredResults.size() + PageSize - 1) / PageSize);
  UI->PageInfoLabel->setText(QString("第 %1 页 / 共 %2 页").arg(CurrentPage).arg(TotalPages));
  UI->PrevPageButton->setEnabled(CurrentPage > 1);
  UI->NextPageButton->setEnabled(CurrentPage < TotalPages);
}

void QueryBookForm::handleSearchButtonClicked() {
  loadData();
}

void QueryBookForm::handleCellClicked(int Row, int Column) {
  Q_UNUSED(Row);
  Q_UNUSED(Column);
  // 点击行时不再处理，因为状态按钮已经可以处理点击事件
}

void QueryBookForm::handlePrevPageClicked() {
  if (CurrentPage > 1) {
    CurrentPage--;
    updateTable();
    updatePageInfo();
  }
}

void QueryBookForm::handleNextPageClicked() {
  int TotalPages = qMax(1, (FilteredResults.size() + PageSize - 1) / PageSize);
  if (CurrentPage < TotalPages) {
    CurrentPage++;
    updateTable();
    updatePageInfo();
  }
}
