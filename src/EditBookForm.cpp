#include "EditBookForm.h"
#include "ui_EditBookForm.h"

#include "Library.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QProgressDialog>

EditBookForm::EditBookForm(QWidget *Parent)
    : QWidget(Parent), UI(new Ui::EditBookForm) {
  UI->setupUi(this);
  UI->MarkLostButton->setEnabled(false);

  connect(UI->SelectCSVFileButton, &QPushButton::clicked, this,
          &EditBookForm::handleSelectCSVFileButtonClicked);
  connect(UI->LoadFromCSVButton, &QPushButton::clicked, this,
          &EditBookForm::handleLoadFromCSVButtonClicked);
  connect(UI->QueryButton, &QPushButton::clicked, this,
          &EditBookForm::handleQueryButtonClicked);
  connect(UI->BarcodeLineEdit, &QLineEdit::returnPressed, this,
          &EditBookForm::handleQueryButtonClicked);
  connect(UI->MarkLostButton, &QPushButton::clicked, this,
          &EditBookForm::handleMarkLostButtonClicked);
}

EditBookForm::~EditBookForm() { delete UI; }

void EditBookForm::handleSelectCSVFileButtonClicked() {
  QString File =
      QFileDialog::getOpenFileName(this, "打开书目清单", "", "TSV文件 (*.txt)");
  if (!File.isEmpty()) {
    UI->CSVFilePathLabel->setText(File);
  }
}

void EditBookForm::handleLoadFromCSVButtonClicked() {
  if (UI->CSVFilePathLabel->text().isEmpty()) {
    QMessageBox::warning(this, "warning", "请先选择文件");
    return;
  }

  QProgressDialog Progress("正在导入书籍...", QString(), 0, 0, this);
  Progress.setWindowModality(Qt::WindowModal);
  Progress.setMinimumDuration(0);
  Progress.setAutoClose(false);
  Progress.setAutoReset(false);
  Progress.setCancelButton(nullptr);  // 不显示取消按钮（未实现取消功能）

  connect(&LibrarySystem::getInstance(), &LibrarySystem::importProgress,
          &Progress, [&Progress](int Current, int Total) {
            Progress.setMaximum(Total);
            Progress.setValue(Current);
          });

  auto ResultErrOr =
      LibrarySystem::getInstance().importFromCSV(UI->CSVFilePathLabel->text());

  Progress.close();

  if (!ResultErrOr) {
    QMessageBox::critical(this, "critical",
                          "导入失败: " + ResultErrOr.getErrMsg());
    qCritical() << "导入失败: " + ResultErrOr.getErrMsg();
  } else {
    QMessageBox::information(this, "information", "数据已成功导入至数据库");
    qInfo() << "数据已成功导入至数据库";
  }
}

void EditBookForm::handleQueryButtonClicked() {
  QString Barcode = UI->BarcodeLineEdit->text().trimmed();
  if (Barcode.isEmpty()) {
    QMessageBox::warning(this, "提示", "请输入条形码");
    return;
  }

  auto Result = LibrarySystem::getInstance().getBookDataByBarcode(Barcode);
  if (!Result) {
    QMessageBox::warning(this, "查询失败", Result.getErrMsg());
    clearBookInfoDisplay();
    return;
  }

  CurrentBarcode = Barcode;
  const auto &[Info, Copy] = Result.getValue();
  updateBookInfoDisplay(Info, Copy);

  // 只有"在馆"或"借出"状态才允许标记遗失
  UI->MarkLostButton->setEnabled(
      Copy.Status == BookCopy::BS_InLibrary ||
      Copy.Status == BookCopy::BS_Borrowed);
}

void EditBookForm::handleMarkLostButtonClicked() {
  if (CurrentBarcode.isEmpty()) {
    QMessageBox::warning(this, "提示", "请先查询图书");
    return;
  }

  auto Result = LibrarySystem::getInstance().modifyBookStatusByBarcode(
      CurrentBarcode, BookCopy::BS_Lost);
  if (!Result) {
    QMessageBox::warning(this, "标记失败", Result.getErrMsg());
    return;
  }

  QMessageBox::information(this, "成功", "图书已标记为遗失");

  // 重新查询以刷新显示
  handleQueryButtonClicked();
}

void EditBookForm::updateBookInfoDisplay(const BookInfo &Info,
                                         const BookCopy &Copy) {
  UI->BookTitleLabel->setText(Info.Title);
  UI->BookAuthorLabel->setText(Info.Author);
  UI->BookPublisherLabel->setText(Info.Publisher);

  QString StatusText;
  switch (Copy.Status) {
  case BookCopy::BS_InLibrary:
    StatusText = "在馆";
    break;
  case BookCopy::BS_Borrowed:
    StatusText = "借出";
    break;
  case BookCopy::BS_Lost:
    StatusText = "遗失";
    break;
  default:
    StatusText = "未知";
    break;
  }
  UI->BookStatusLabel->setText(StatusText);
}

void EditBookForm::clearBookInfoDisplay() {
  CurrentBarcode.clear();
  UI->BookTitleLabel->setText("-");
  UI->BookAuthorLabel->setText("-");
  UI->BookPublisherLabel->setText("-");
  UI->BookStatusLabel->setText("-");
  UI->MarkLostButton->setEnabled(false);
}
