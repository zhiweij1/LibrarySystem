#include "EditBookForm.h"
#include "ui_EditBookForm.h"

#include "Library.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QProgressDialog>

EditBookForm::EditBookForm(QWidget *Parent)
    : QWidget(Parent), UI(new Ui::EditBookForm) {
  UI->setupUi(this);
  UI->ModifyStatusButton->setEnabled(false);
  UI->TargetStatusComboBox->setEnabled(false);

  connect(UI->SelectCSVFileButton, &QPushButton::clicked, this,
          &EditBookForm::handleSelectCSVFileButtonClicked);
  connect(UI->LoadFromCSVButton, &QPushButton::clicked, this,
          &EditBookForm::handleLoadFromCSVButtonClicked);
  connect(UI->QueryButton, &QPushButton::clicked, this,
          &EditBookForm::handleQueryButtonClicked);
  connect(UI->BarcodeLineEdit, &QLineEdit::returnPressed, this,
          &EditBookForm::handleQueryButtonClicked);
  connect(UI->ModifyStatusButton, &QPushButton::clicked, this,
          &EditBookForm::handleModifyStatusButtonClicked);
}

EditBookForm::~EditBookForm() { delete UI; }

void EditBookForm::handleSelectCSVFileButtonClicked() {
  QString File =
      QFileDialog::getOpenFileName(this, "打开书目清单", "", "TSV文件 (*.tsv *.txt)");
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

  // 使用 QScopedPointer 确保连接在作用域结束时自动断开
  QMetaObject::Connection Conn;
  Conn = connect(&LibrarySystem::getInstance(), &LibrarySystem::importProgress,
                 &Progress, [&Progress](int Current, int Total) {
                   Progress.setMaximum(Total);
                   Progress.setValue(Current);
                 });

  auto ResultErrOr =
      LibrarySystem::getInstance().importFromTSV(UI->CSVFilePathLabel->text());

  // 断开信号连接，防止重复连接
  disconnect(Conn);
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

  // 根据当前状态填充可选的目标状态
  UI->TargetStatusComboBox->clear();
  bool CanModify = false;
  switch (Copy.Status) {
  case BookCopy::BS_InLibrary:
    UI->TargetStatusComboBox->addItem("遗失", BookCopy::BS_Lost);
    UI->TargetStatusComboBox->addItem("非外借书", BookCopy::BS_NonLendable);
    CanModify = true;
    break;
  case BookCopy::BS_Borrowed:
    UI->TargetStatusComboBox->addItem("遗失", BookCopy::BS_Lost);
    UI->TargetStatusComboBox->addItem("非外借书", BookCopy::BS_NonLendable);
    CanModify = true;
    break;
  case BookCopy::BS_Lost:
  case BookCopy::BS_NonLendable:
    UI->TargetStatusComboBox->addItem("在馆", BookCopy::BS_InLibrary);
    CanModify = true;
    break;
  }
  UI->TargetStatusComboBox->setEnabled(CanModify);
  UI->ModifyStatusButton->setEnabled(CanModify);
}

void EditBookForm::handleModifyStatusButtonClicked() {
  if (CurrentBarcode.isEmpty()) {
    QMessageBox::warning(this, "提示", "请先查询图书");
    return;
  }

  int TargetStatus = UI->TargetStatusComboBox->currentData().toInt();
  QString TargetStatusText = UI->TargetStatusComboBox->currentText();

  auto Result = LibrarySystem::getInstance().modifyBookStatusByBarcode(
      CurrentBarcode, TargetStatus);
  if (!Result) {
    QMessageBox::warning(this, "修改失败", Result.getErrMsg());
    return;
  }

  QMessageBox::information(this, "成功",
                           QString("图书状态已修改为'%1'").arg(TargetStatusText));

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
  case BookCopy::BS_NonLendable:
    StatusText = "非外借书";
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
  UI->TargetStatusComboBox->clear();
  UI->TargetStatusComboBox->setEnabled(false);
  UI->ModifyStatusButton->setEnabled(false);
}
