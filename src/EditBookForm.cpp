#include "EditBookForm.h"
#include "ui_EditBookForm.h"

#include "Library.h"

#include <QFileDialog>
#include <QMessageBox>

EditBookForm::EditBookForm(QWidget *Parent)
    : QWidget(Parent), UI(new Ui::EditBookForm) {
  UI->setupUi(this);
  connect(UI->SelectCSVFileButton, &QPushButton::clicked, this,
          &EditBookForm::handleSelectCSVFileButtonClicked);
  connect(UI->LoadFromCSVButton, &QPushButton::clicked, this,
          &EditBookForm::handleLoadFromCSVButtonClicked);
}

EditBookForm::~EditBookForm() { delete UI; }

void EditBookForm::handleSelectCSVFileButtonClicked() {
  QString File =
      QFileDialog::getOpenFileName(this, "打开书目清单", "", "CSV文件 (*.csv)");
  if (!File.isEmpty()) {
    UI->CSVFilePathLabel->setText(File);
  }
}

void EditBookForm::handleLoadFromCSVButtonClicked() {
  if (UI->CSVFilePathLabel->text().isEmpty()) {
    QMessageBox::warning(this, "warning", "请先选择文件");
    return;
  }
  auto ResultErrOr =
      LibrarySystem::getInstance().importFromCSV(UI->CSVFilePathLabel->text());
  if (!ResultErrOr) {
    QMessageBox::critical(this, "critical",
                          "导入失败: " + ResultErrOr.getErrMsg());
    qCritical() << "导入失败: " + ResultErrOr.getErrMsg();
  } else {
    QMessageBox::information(this, "information", "数据已成功导入至数据库");
    qInfo() << "数据已成功导入至数据库";
  }
}
