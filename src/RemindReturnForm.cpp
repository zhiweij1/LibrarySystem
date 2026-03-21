#include "RemindReturnForm.h"
#include "ui_RemindReturnForm.h"

#include "Library.h"

#include <QCoreApplication>
#include <QDate>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QToolButton>

RemindReturnForm::RemindReturnForm(QWidget *Parent)
    : QWidget(Parent), UI(new Ui::RemindReturnForm) {
  UI->setupUi(this);

  connect(UI->QueryButton, &QPushButton::clicked, this,
          &RemindReturnForm::handleQueryClicked);
  connect(UI->DaysSpinBox, &QSpinBox::editingFinished, this,
          &RemindReturnForm::handleQueryClicked);

  // 初始加载
  loadData();
}

RemindReturnForm::~RemindReturnForm() { delete UI; }

void RemindReturnForm::handleQueryClicked() { loadData(); }

void RemindReturnForm::loadData() {
  int Days = UI->DaysSpinBox->value();

  auto Result = LibrarySystem::getInstance().getRemindBorrowings(Days);
  if (!Result) {
    QMessageBox::warning(this, "错误", "查询失败: " + Result.getErrMsg());
    return;
  }

  ReaderInfos = Result.getValue();
  clearCards();

  int UrgentCount = 0;
  for (const auto &Info : std::as_const(ReaderInfos)) {
    UrgentCount += Info.urgentBooks.size();
    QWidget *Card = createReaderCard(Info);
    UI->cardLayout->insertWidget(UI->cardLayout->count() - 1, Card);
  }

  UI->SummaryLabel->setText(
      QString("共 %1 位读者，%2 本待催还").arg(ReaderInfos.size()).arg(UrgentCount));
}

void RemindReturnForm::clearCards() {
  QLayout *Layout = UI->cardLayout;
  while (Layout->count() > 1) {
    QLayoutItem *Item = Layout->takeAt(0);
    if (QWidget *W = Item->widget()) {
      W->setParent(nullptr);  // 先从布局移除
      W->deleteLater();       // 延迟删除，让事件队列处理完
    }
    delete Item;
  }
}

QWidget *RemindReturnForm::createReaderCard(const LibrarySystem::ReaderBorrowInfo &Info) {
  QFrame *Card = new QFrame();
  Card->setFrameShape(QFrame::StyledPanel);
  Card->setLineWidth(1);
  Card->setStyleSheet("QFrame { background-color: #FFFFFF; border: 1px solid #E0E0E0; border-radius: 6px; }");

  QVBoxLayout *CardLayout = new QVBoxLayout(Card);
  CardLayout->setSpacing(8);

  // 顶部：读者信息
  QHBoxLayout *HeaderLayout = new QHBoxLayout();

  QLabel *ReaderLabel = new QLabel(
      QString("读者：%1  |  电话：%2")
          .arg(Info.reader.Name)
          .arg(Info.reader.PhoneNumber));
  ReaderLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
  HeaderLayout->addWidget(ReaderLabel);

  HeaderLayout->addStretch();

  CardLayout->addLayout(HeaderLayout);

  // 分隔线
  QFrame *Line = new QFrame();
  Line->setFrameShape(QFrame::HLine);
  Line->setStyleSheet("background-color: #E0E0E0;");
  Line->setFixedHeight(1);
  CardLayout->addWidget(Line);

  // 紧急借书列表
  QDate Today = QDate::currentDate();
  for (const auto &Book : Info.urgentBooks) {
    QWidget *BookWidget = createBookItem(Book, Today);
    CardLayout->addWidget(BookWidget);
  }

  // 其他借书（可折叠）
  if (!Info.otherBooks.isEmpty()) {
    QHBoxLayout *ToggleLayout = new QHBoxLayout();

    QToolButton *ToggleBtn = new QToolButton();
    ToggleBtn->setText(QString("▶ 其他借书（%1 本）").arg(Info.otherBooks.size()));
    ToggleBtn->setStyleSheet("QToolButton { border: none; color: #666; font-size: 12px; }");
    ToggleBtn->setCheckable(true);
    ToggleLayout->addWidget(ToggleBtn);

    CardLayout->addLayout(ToggleLayout);

    // 其他借书容器（初始隐藏）
    QWidget *OtherBooksWidget = new QWidget();
    QVBoxLayout *OtherBooksLayout = new QVBoxLayout(OtherBooksWidget);
    OtherBooksLayout->setContentsMargins(20, 0, 0, 0);

    for (const auto &Book : Info.otherBooks) {
      QWidget *BookWidget = createBookItem(Book, Today, true);
      OtherBooksLayout->addWidget(BookWidget);
    }

    OtherBooksWidget->setVisible(false);
    CardLayout->addWidget(OtherBooksWidget);

    int OtherBooksCount = Info.otherBooks.size();
    connect(ToggleBtn, &QToolButton::toggled, this, [ToggleBtn, OtherBooksWidget, OtherBooksCount](bool Checked) {
      OtherBooksWidget->setVisible(Checked);
      ToggleBtn->setText(Checked
                             ? QString("▼ 其他借书")
                             : QString("▶ 其他借书（%1 本）").arg(OtherBooksCount));
    });
  }

  CardLayout->addStretch();
  return Card;
}

QWidget *RemindReturnForm::createBookItem(const BorrowDetailType &Book,
                                          const QDate &Today, bool IsOther) {
  QFrame *Item = new QFrame();
  Item->setStyleSheet("QFrame { background-color: #FAFAFA; border-radius: 4px; }");
  Item->setMinimumHeight(90);

  QHBoxLayout *Layout = new QHBoxLayout(Item);
  Layout->setContentsMargins(8, 8, 8, 8);
  Layout->setSpacing(10);

  // 封面图片
  QLabel *ImgLabel = new QLabel();
  ImgLabel->setFixedSize(60, 80);
  QString FullPath = QCoreApplication::applicationDirPath() + "/" +
                     Book.Info.CoverPath;
  QPixmap Pix(FullPath);
  if (Pix.isNull()) {
    ImgLabel->setText("无封面");
    ImgLabel->setAlignment(Qt::AlignCenter);
    ImgLabel->setStyleSheet("background-color: #E0E0E0; color: #999; font-size: 11px;");
  } else {
    ImgLabel->setPixmap(
        Pix.scaled(60, 80, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ImgLabel->setAlignment(Qt::AlignCenter);
  }
  Layout->addWidget(ImgLabel);

  // 书籍信息
  QWidget *InfoWidget = new QWidget();
  QVBoxLayout *InfoLayout = new QVBoxLayout(InfoWidget);
  InfoLayout->setContentsMargins(0, 0, 0, 0);
  InfoLayout->setSpacing(2);

  QLabel *TitleLabel = new QLabel(Book.Info.Title);
  TitleLabel->setStyleSheet(IsOther ? "font-size: 13px; color: #333;" : "font-size: 13px; font-weight: bold; color: #333;");
  InfoLayout->addWidget(TitleLabel);

  QLabel *AuthorLabel = new QLabel(QString("作者：%1").arg(Book.Info.Author));
  AuthorLabel->setStyleSheet("font-size: 11px; color: #666;");
  InfoLayout->addWidget(AuthorLabel);

  QLabel *PublisherLabel = new QLabel(QString("出版社：%1").arg(Book.Info.Publisher));
  PublisherLabel->setStyleSheet("font-size: 11px; color: #666;");
  InfoLayout->addWidget(PublisherLabel);

  Layout->addWidget(InfoWidget, 1);

  // 条码号
  QLabel *BarcodeLabel = new QLabel(Book.Copy.Barcode);
  BarcodeLabel->setStyleSheet("font-size: 11px; color: #888; min-width: 80px;");
  BarcodeLabel->setAlignment(Qt::AlignCenter);
  Layout->addWidget(BarcodeLabel);

  // 到期状态
  QDate DueDate = Book.Record.DueDate.date();
  int DaysLeft = Today.daysTo(DueDate);

  QString StatusText;
  QString StatusStyle;
  if (DaysLeft < 0) {
    StatusText = QString("已逾期 %1 天").arg(-DaysLeft);
    StatusStyle = "color: #D32F2F; font-weight: bold;";
  } else if (DaysLeft == 0) {
    StatusText = "今日到期";
    StatusStyle = "color: #D32F2F; font-weight: bold;";
  } else {
    StatusText = QString("还剩 %1 天").arg(DaysLeft);
    StatusStyle = IsOther ? "color: #999;" : "color: #F57C00; font-weight: bold;";
  }

  QLabel *StatusLabel = new QLabel(StatusText);
  StatusLabel->setStyleSheet("font-size: 12px; " + StatusStyle);
  StatusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  StatusLabel->setMinimumWidth(70);
  Layout->addWidget(StatusLabel);

  return Item;
}
