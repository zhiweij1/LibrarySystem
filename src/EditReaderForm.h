#ifndef EDITREADERFORM_H
#define EDITREADERFORM_H

#include <QDataWidgetMapper>
#include <QSqlTableModel>
#include <QTimer>
#include <QWidget>

namespace Ui {
class EditReaderForm;
} // namespace Ui

class EditReaderForm : public QWidget {
  Q_OBJECT

public:
  explicit EditReaderForm(QWidget *Parent = nullptr);
  ~EditReaderForm();

private:
  void performSearch();
  void handleAddButtonClicked();
  void handleSaveButtonClicked();
  void handleDeactivateButtonClicked();
  void refreshStatusDisplay();

  Ui::EditReaderForm *UI;
  QSqlTableModel *Model;
  QDataWidgetMapper *Mapper;

  QTimer *SearchTimer;
};

#endif // EDITREADERFORM_H
