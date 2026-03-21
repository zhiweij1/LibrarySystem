#include "EditReaderForm.h"
#include "ui_EditReaderForm.h"

#include "Library.h"

#include <QMessageBox>
#include <QSqlError>

EditReaderForm::EditReaderForm(QWidget *Parent)
    : QWidget(Parent), UI(new Ui::EditReaderForm) {
  UI->setupUi(this);

  SearchTimer = new QTimer(this);
  SearchTimer->setSingleShot(true); // 设置为单次触发

  // 连接信号：计时器结束时执行搜索
  connect(SearchTimer, &QTimer::timeout, this, &EditReaderForm::performSearch);

  // 原有的 textChanged 信号改为触发计时器逻辑
  connect(UI->ReaderSearchLineEdit, &QLineEdit::textChanged, this, [this]() {
    if (UI->ReaderSearchLineEdit->text().isEmpty()) {
      SearchTimer->stop();   // 如果清空了输入框，直接停掉计时器
      performSearch();       // 立即清空表格
    } else {
      SearchTimer->start(200); // 否则启动/重置计时器
    }
  });

  connect(UI->AddButton, &QPushButton::clicked, this,
          &EditReaderForm::handleAddButtonClicked);
  connect(UI->SaveButton, &QPushButton::clicked, this,
          &EditReaderForm::handleSaveButtonClicked);
  connect(UI->DeactivateButton, &QPushButton::clicked, this,
          &EditReaderForm::handleDeactivateButtonClicked);

  Model = new QSqlTableModel(this);
  Model->setTable("reader");
  Model->setEditStrategy(QSqlTableModel::OnManualSubmit);
  Model->select();

  UI->ReaderTableView->setModel(Model);
  UI->ReaderTableView->setSelectionBehavior(QAbstractItemView::SelectRows);
  UI->ReaderTableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  UI->ReaderTableView->setColumnHidden(0, true);  // hide ID
  UI->ReaderTableView->setColumnHidden(1, false); // show name
  UI->ReaderTableView->setColumnHidden(2, false); // show card number
  UI->ReaderTableView->setColumnHidden(3, false); // show phone number
  UI->ReaderTableView->setColumnHidden(4, true);  // hide status
  UI->ReaderTableView->verticalHeader()->setVisible(false);
  Model->setHeaderData(1, Qt::Horizontal, "姓名");
  Model->setHeaderData(2, Qt::Horizontal, "卡号");
  Model->setHeaderData(3, Qt::Horizontal, "联系电话");

  UI->ReaderTableView->horizontalHeader()->setSectionResizeMode(
      QHeaderView::Stretch);

  Mapper = new QDataWidgetMapper(this);
  Mapper->setModel(Model);
  Mapper->addMapping(UI->NameLineEdit, 1);
  Mapper->addMapping(UI->IDLineEdit, 2);
  Mapper->addMapping(UI->PhoneLineEdit, 3);

  connect(
      UI->ReaderTableView->selectionModel(),
      &QItemSelectionModel::selectionChanged,
      [this](const QItemSelection &, const QItemSelection &) {
        bool HasSelection =
            !UI->ReaderTableView->selectionModel()->selectedIndexes().isEmpty();

        UI->groupBox->setEnabled(HasSelection);

        if (HasSelection) {
          QModelIndex Index =
              UI->ReaderTableView->selectionModel()->currentIndex();
          Mapper->setCurrentModelIndex(Index);
          refreshStatusDisplay();
        } else {
          UI->NameLineEdit->clear();
          UI->IDLineEdit->clear();
          UI->PhoneLineEdit->clear();
          UI->StatusLabel->setText("未选择");
        }
      });

  UI->groupBox->setEnabled(false);
  UI->StatusLabel->setText("未选择");
}

EditReaderForm::~EditReaderForm() { delete UI; }

void EditReaderForm::performSearch() {
  const QString Text = UI->ReaderSearchLineEdit->text();
  QString Filter = QString("(name LIKE '%%1%') OR (card_number LIKE '%%1%') OR "
                           "(phone LIKE '%%1%')")
                       .arg(Text);
  Model->setFilter(Filter);
  Model->select();
}

void EditReaderForm::handleAddButtonClicked() {
  auto ErrOrID = LibrarySystem::getInstance().getNewReaderCardID();
  if (!ErrOrID) {
    QMessageBox::critical(this, "critical",
                          "创建新读者号失败: " + ErrOrID.getErrMsg());
    qCritical() << "创建新读者号失败: " + ErrOrID.getErrMsg();
    return;
  }
  int Row = Model->rowCount();
  Model->insertRow(Row);
  Model->setData(Model->index(Row, 4), RS_Active);
  Model->setData(Model->index(Row, 2), ErrOrID.getValue());
  UI->ReaderTableView->selectRow(Row);
  UI->NameLineEdit->setFocus();
}

void EditReaderForm::handleSaveButtonClicked() {
  if (UI->NameLineEdit->text().trimmed().isEmpty()) {
    QMessageBox::warning(this, "warning", "读者姓名不能为空");
    return;
  }

  QVariant CardID = Model->index(Mapper->currentIndex(), 2).data();

  Mapper->submit();
  if (Model->submitAll()) {
    for (int Idx = 0; Idx < Model->rowCount(); ++Idx) {
      if (Model->index(Idx, 2).data() == CardID) {
        UI->ReaderTableView->selectRow(Idx);
        break;
      }
    }
    QMessageBox::information(this, "information", "读者信息已保存");
    qInfo() << "读者信息已保存";
  } else {
    QMessageBox::critical(this, "critical",
                          "读者信息保存失败: " + Model->lastError().text());
    qCritical() << "读者信息保存失败: " + Model->lastError().text();
    Model->revertAll();
  }
}

void EditReaderForm::handleDeactivateButtonClicked() {
  int Row = Mapper->currentIndex();
  if (Row < 0)
    return;

  QVariant CardID = Model->index(Row, 2).data();

  ReaderStatus IsActive =
      static_cast<ReaderStatus>(Model->index(Row, 4).data().toInt());
  Model->setData(Model->index(Row, 4),
                 IsActive == RS_Active ? RS_InActive : RS_Active);

  if (Model->submitAll()) {
    refreshStatusDisplay();
  }
  for (int Idx = 0; Idx < Model->rowCount(); ++Idx) {
    if (Model->index(Idx, 2).data() == CardID) {
      UI->ReaderTableView->selectRow(Idx);
      break;
    }
  }
}

void EditReaderForm::refreshStatusDisplay() {
  int Row = Mapper->currentIndex();
  if (Row < 0)
    return;

  int RawStatus = Model->index(Row, 4).data().toInt();
  if (RawStatus != RS_Active && RawStatus != RS_InActive) {
    UI->StatusLabel->setText("未知状态");
    UI->StatusLabel->setStyleSheet("background:transparent;color:gray;");
    return;
  }

  ReaderStatus IsActive =
      static_cast<ReaderStatus>(Model->index(Row, 4).data().toInt());
  if (RS_Active == IsActive) {
    UI->StatusLabel->setText("正常");
    UI->StatusLabel->setStyleSheet("background:transparent;color:green;");
    UI->DeactivateButton->setText("注销");
  } else {
    UI->StatusLabel->setText("已注销");
    UI->StatusLabel->setStyleSheet("background:transparent;color:red;");
    UI->DeactivateButton->setText("激活");
  }
}
