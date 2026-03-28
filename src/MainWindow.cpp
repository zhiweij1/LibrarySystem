#include "MainWindow.h"
#include "ui_MainWindow.h"

#include "BorrowBookForm.h"
#include "EditBookForm.h"
#include "EditReaderForm.h"
#include "ReturnBookForm.h"

#include <QMessageBox>

MainWindow::MainWindow(QWidget *Parent)
    : QMainWindow(Parent), UI(new Ui::MainWindow) {
  UI->setupUi(this);

  connect(UI->BorrowBookButton, &QPushButton::clicked, this,
          &MainWindow::handleBorrowBookButtonClicked);
  connect(UI->ReturnBookButton, &QPushButton::clicked, this,
          &MainWindow::handleReturnBookButtonClicked);
  connect(UI->BookStatusQueryButton, &QPushButton::clicked, this,
          &MainWindow::handleBookStatusQueryButtonClicked);
  connect(UI->RemindReturnButton, &QPushButton::clicked, this,
          &MainWindow::handleRemindReturnButtonClicked);
  connect(UI->EditBookButton, &QPushButton::clicked, this,
          &MainWindow::handleEditBookButtonClicked);
  connect(UI->EditReaderButton, &QPushButton::clicked, this,
          &MainWindow::handleEditReaderButtonClicked);
  connect(UI->AboutAction, &QAction::triggered, this,
          &MainWindow::handleAboutClicked);

  BorrowPage = new BorrowBookForm(this);
  ReturnPage = new ReturnBookForm(this);
  EditBookPage = new EditBookForm(this);
  EditReaderPage = new EditReaderForm(this);
  QueryBookPage = new QueryBookForm(this);
  RemindReturnPage = new RemindReturnForm(this);

  UI->stackedWidget->addWidget(BorrowPage);      // 索引 0
  UI->stackedWidget->addWidget(ReturnPage);      // 索引 1
  UI->stackedWidget->addWidget(QueryBookPage);   // 索引 2
  UI->stackedWidget->addWidget(RemindReturnPage);// 索引 3
  UI->stackedWidget->addWidget(EditBookPage);    // 索引 4
  UI->stackedWidget->addWidget(EditReaderPage);  // 索引 5

  UI->stackedWidget->setCurrentWidget(BorrowPage);
  changeTheme(Theme::purple, UI->BorrowBookButton);
}

MainWindow::~MainWindow() { delete UI; }

void MainWindow::handleAboutClicked() {
  QString AboutText =
      QString("图书馆系统\n\n"
              "本项目主体采用 Unlicense 协议发布。\n"
              "你可以自由地复制、修改、发布或销售本项目，且无需署名。\n"
              "本项目包含以下第三方组件，其版权归原作者所有（分发该代码时，请务"
              "必保留文件内部的版权及许可说明）:\n"
              "    .clang-tidy: 采用 Apache-2.0 WITH LLVM-exception\n\n"
              "Built on: %1\n"
              "Based on: Qt %2\n"
              "From revision: %3\n"
              "Built by: %4 @ %5")
          .arg(QString(BUILD_DATE),   // %1
               qVersion(),            // %2
               QString(GIT_REVISION), // %3
               QString(BUILD_USER),   // %4
               QString(BUILD_HOST)    // %5
          );

  QMessageBox MsgBox(this);
  MsgBox.setWindowTitle(tr("关于"));
  MsgBox.setText(AboutText);
  MsgBox.setIcon(QMessageBox::Information);

  MsgBox.setStyleSheet(
      "QLabel{ font-size: 10pt; } QPushButton{ font-size: 10pt; }");
  MsgBox.exec();
}

static const QString SidebarBtnBase =
    "QPushButton{background-color:transparent;border:1px solid rgba(0,0,0,0.1);"
    "border-radius:6px;min-width:120px;min-height:100px;"
    "font-weight:bold;padding:0;}"
    "QPushButton:hover{border:1px solid rgba(0,0,0,0.2);}";

void MainWindow::cleanTheme() {
  UI->BorrowBookButton->setStyleSheet(SidebarBtnBase);
  UI->ReturnBookButton->setStyleSheet(SidebarBtnBase);
  UI->BookStatusQueryButton->setStyleSheet(SidebarBtnBase);
  UI->RemindReturnButton->setStyleSheet(SidebarBtnBase);
  UI->EditBookButton->setStyleSheet(SidebarBtnBase);
  UI->EditReaderButton->setStyleSheet(SidebarBtnBase);

  UI->ContentFrame->setStyleSheet("");
}

void MainWindow::changeTheme(const Theme T, QPushButton *Btn) {
  cleanTheme();

  const QString Color = ThemeMap[T];
  const QString ActiveBtnStyle =
      SidebarBtnBase +
      "QPushButton{background-color:" + Color +
      ";border:1px solid rgba(0,0,0,0.15);}"
      "QPushButton:pressed{background-color:" + Color +
      ";border:1px solid rgba(0,0,0,0.15);}"
      "QPushButton:disabled{background-color:" + Color +
      ";border:1px solid rgba(0,0,0,0.15);}";
  Btn->setStyleSheet(ActiveBtnStyle);
  const QString FrameSS = "QFrame#ContentFrame{border:5px solid " + Color +
                          ";border-radius:8px;background-color:#F5F5F5;}";
  UI->ContentFrame->setStyleSheet(FrameSS);
}

void MainWindow::handleBorrowBookButtonClicked() {
  UI->stackedWidget->setCurrentWidget(BorrowPage);
  changeTheme(Theme::purple, UI->BorrowBookButton);
}

void MainWindow::handleReturnBookButtonClicked() {
  UI->stackedWidget->setCurrentWidget(ReturnPage);
  changeTheme(Theme::green, UI->ReturnBookButton);
}

void MainWindow::handleBookStatusQueryButtonClicked() {
  UI->stackedWidget->setCurrentWidget(QueryBookPage);
  changeTheme(Theme::brown, UI->BookStatusQueryButton);
}

void MainWindow::handleRemindReturnButtonClicked() {
  UI->stackedWidget->setCurrentWidget(RemindReturnPage);
  changeTheme(Theme::blue, UI->RemindReturnButton);
}

void MainWindow::handleEditBookButtonClicked() {
  UI->stackedWidget->setCurrentWidget(EditBookPage);
  changeTheme(Theme::orange, UI->EditBookButton);
}

void MainWindow::handleEditReaderButtonClicked() {
  UI->stackedWidget->setCurrentWidget(EditReaderPage);
  changeTheme(Theme::red, UI->EditReaderButton);
}

