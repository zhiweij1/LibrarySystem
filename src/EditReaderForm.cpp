#include "EditReaderForm.h"
#include "ui_EditReaderForm.h"

#include <QHeaderView>
#include <QMessageBox>

EditReaderForm::EditReaderForm(QWidget *Parent)
    : QWidget(Parent), UI(new Ui::EditReaderForm) {
  UI->setupUi(this);

  SearchTimer = new QTimer(this);
  SearchTimer->setSingleShot(true);
  connect(SearchTimer, &QTimer::timeout, this, &EditReaderForm::performSearch);

  connect(UI->ReaderSearchLineEdit, &QLineEdit::textChanged, this,
          [this]() { SearchTimer->start(200); });
  connect(UI->AddButton, &QPushButton::clicked, this,
          &EditReaderForm::handleAddButtonClicked);
  connect(UI->SaveButton, &QPushButton::clicked, this,
          &EditReaderForm::handleSaveButtonClicked);
  connect(UI->DeleteButton, &QPushButton::clicked, this,
          &EditReaderForm::handleDeleteButtonClicked);

  connect(
      UI->ReaderTableWidget, &QTableWidget::cellClicked, this, [this](int Row) {
        CurrentReaderID =
            UI->ReaderTableWidget->item(Row, 0)->data(Qt::UserRole).toInt();
        UI->NameLineEdit->setText(UI->ReaderTableWidget->item(Row, 1)->text());
        UI->IDLineEdit->setText(UI->ReaderTableWidget->item(Row, 2)->text());
        UI->PhoneLineEdit->setText(UI->ReaderTableWidget->item(Row, 3)->text());
        // 加载注销状态
        for (const auto &r : std::as_const(AllReaders)) {
          if (r.ID == CurrentReaderID) {
            UI->InactiveCheckBox->setChecked(r.IsInactive);
            break;
          }
        }
        UI->groupBox->setEnabled(true);
      });

  UI->ReaderTableWidget->setColumnCount(5);
  UI->ReaderTableWidget->setHorizontalHeaderLabels(
      {"ID", "姓名", "卡号", "电话", "状态"});
  UI->ReaderTableWidget->setColumnHidden(0, true);
  UI->ReaderTableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
  UI->ReaderTableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
  UI->ReaderTableWidget->verticalHeader()->setVisible(false);
  UI->ReaderTableWidget->horizontalHeader()->setSectionResizeMode(
      QHeaderView::Stretch);
  UI->groupBox->setEnabled(false);

  refreshTable();
}

EditReaderForm::~EditReaderForm() { delete UI; }

void EditReaderForm::refreshTable() {
  auto res = LibrarySystem::getInstance().getAllReaders();
  if (!res)
    return;
  AllReaders = res.getValue();

  QString search = UI->ReaderSearchLineEdit->text().trimmed();
  QVector<Reader> filtered;
  if (search.isEmpty()) {
    filtered = AllReaders;
  } else {
    for (const auto &r : std::as_const(AllReaders)) {
      if (r.Name.contains(search, Qt::CaseInsensitive) ||
          r.CardNumber.contains(search, Qt::CaseInsensitive) ||
          r.PhoneNumber.contains(search, Qt::CaseInsensitive))
        filtered.append(r);
    }
  }

  UI->ReaderTableWidget->setRowCount(0);
  for (const auto &r : std::as_const(filtered)) {
    int row = UI->ReaderTableWidget->rowCount();
    UI->ReaderTableWidget->insertRow(row);
    QTableWidgetItem *idItem = new QTableWidgetItem(QString::number(r.ID));
    idItem->setData(Qt::UserRole, r.ID);
    UI->ReaderTableWidget->setItem(row, 0, idItem);
    UI->ReaderTableWidget->setItem(row, 1, new QTableWidgetItem(r.Name));
    UI->ReaderTableWidget->setItem(row, 2, new QTableWidgetItem(r.CardNumber));
    UI->ReaderTableWidget->setItem(row, 3, new QTableWidgetItem(r.PhoneNumber));
    QTableWidgetItem *statusItem =
        new QTableWidgetItem(r.IsInactive ? "已注销" : "正常");
    UI->ReaderTableWidget->setItem(row, 4, statusItem);
    if (r.IsInactive) {
      for (int c = 0; c <= 4; ++c) {
        if (auto *item = UI->ReaderTableWidget->item(row, c))
          item->setForeground(Qt::red);
      }
    }
  }

  CurrentReaderID = -1;
  UI->NameLineEdit->clear();
  UI->IDLineEdit->clear();
  UI->PhoneLineEdit->clear();
  UI->groupBox->setEnabled(false);
}

void EditReaderForm::performSearch() { refreshTable(); }

void EditReaderForm::handleAddButtonClicked() {
  auto cardRes = LibrarySystem::getInstance().getNewReaderCardID();
  if (!cardRes) {
    QMessageBox::critical(this, "错误",
                          "生成读者编号失败: " + cardRes.getErrMsg());
    return;
  }
  CurrentReaderID = -1;
  UI->NameLineEdit->clear();
  UI->IDLineEdit->setText(cardRes.getValue());
  UI->PhoneLineEdit->clear();
  UI->InactiveCheckBox->setChecked(false);
  UI->groupBox->setEnabled(true);
  UI->NameLineEdit->setFocus();
}

void EditReaderForm::handleSaveButtonClicked() {
  QString name = UI->NameLineEdit->text().trimmed();
  QString cardNumber = UI->IDLineEdit->text().trimmed();
  QString phone = UI->PhoneLineEdit->text().trimmed();

  if (name.isEmpty()) {
    QMessageBox::warning(this, "提示", "读者姓名不能为空");
    return;
  }
  if (cardNumber.isEmpty()) {
    QMessageBox::warning(this, "提示", "读者编号不能为空");
    return;
  }

  auto &lib = LibrarySystem::getInstance();
  ErrorOr<void> res;

  if (CurrentReaderID < 0) {
    // 新增
    res = lib.addReader(name, cardNumber, phone);
  } else {
    // 修改
    res = lib.updateReader(CurrentReaderID, name, cardNumber, phone,
                           UI->InactiveCheckBox->isChecked());
  }

  if (!res) {
    QMessageBox::critical(this, "错误", "保存失败: " + res.getErrMsg());
    return;
  }

  QMessageBox::information(this, "提示", "读者信息已保存");
  refreshTable();
}

void EditReaderForm::handleDeleteButtonClicked() {
  if (CurrentReaderID < 0) {
    QMessageBox::warning(this, "提示", "请先在表格中选择要删除的读者");
    return;
  }

  auto ret = QMessageBox::question(
      this, "确认删除",
      QString("确定要删除该读者吗？此操作不可撤销。\n\n读者姓名：%1\n卡号：%2")
          .arg(UI->NameLineEdit->text(), UI->IDLineEdit->text()),
      QMessageBox::Yes | QMessageBox::No);
  if (ret != QMessageBox::Yes)
    return;

  auto res = LibrarySystem::getInstance().deleteReader(CurrentReaderID);
  if (!res) {
    QMessageBox::critical(this, "错误", "删除失败: " + res.getErrMsg());
    return;
  }

  QMessageBox::information(this, "提示", "读者已删除");
  refreshTable();
}
