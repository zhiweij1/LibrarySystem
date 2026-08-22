#ifndef READERPICKER_H
#define READERPICKER_H

#include "Library.h"

#include <QAbstractItemView>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

#include <optional>

// 多候选读者选择对话框（手机号共用 / 姓名重名时使用）。
// 返回选中的读者；取消时返回 std::nullopt。
static inline std::optional<Reader> pickReader(QWidget *Parent,
                                               const QVector<Reader> &Candidates) {
  QDialog Dlg(Parent);
  Dlg.setWindowTitle(QString("找到 %1 位读者，请选择").arg(Candidates.size()));
  Dlg.setMinimumSize(500, 300);

  QVBoxLayout *Layout = new QVBoxLayout(&Dlg);
  Layout->addWidget(new QLabel("多位读者匹配，请选择目标读者：", &Dlg));

  QTableWidget *Table = new QTableWidget(Candidates.size(), 4, &Dlg);
  Table->setHorizontalHeaderLabels({"姓名", "卡号", "电话", "状态"});
  Table->setSelectionBehavior(QAbstractItemView::SelectRows);
  Table->setSelectionMode(QAbstractItemView::SingleSelection);
  Table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  Table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  for (int Row = 0; Row < Candidates.size(); ++Row) {
    const Reader &R = Candidates[Row];
    Table->setItem(Row, 0, new QTableWidgetItem(R.Name));
    Table->setItem(Row, 1, new QTableWidgetItem(R.CardNumber));
    Table->setItem(Row, 2, new QTableWidgetItem(R.PhoneNumber));
    auto *StatusItem = new QTableWidgetItem(R.IsInactive ? "已注销" : "正常");
    if (R.IsInactive)
      StatusItem->setForeground(Qt::red);
    Table->setItem(Row, 3, StatusItem);
  }
  Table->selectRow(0);
  Layout->addWidget(Table);

  QDialogButtonBox *BtnBox =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                           &Dlg);
  Layout->addWidget(BtnBox);
  QObject::connect(BtnBox, &QDialogButtonBox::accepted, &Dlg, &QDialog::accept);
  QObject::connect(BtnBox, &QDialogButtonBox::rejected, &Dlg, &QDialog::reject);
  // 双击行直接确认
  QObject::connect(Table, &QTableWidget::cellDoubleClicked, &Dlg,
                   &QDialog::accept);

  if (Dlg.exec() != QDialog::Accepted)
    return std::nullopt;

  int Row = Table->currentRow();
  if (Row < 0 || Row >= Candidates.size())
    return std::nullopt;
  return Candidates[Row];
}

#endif // READERPICKER_H
