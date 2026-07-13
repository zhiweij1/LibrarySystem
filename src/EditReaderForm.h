#ifndef EDITREADERFORM_H
#define EDITREADERFORM_H

#include "Library.h"

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
  void refreshTable();
  void handleAddButtonClicked();
  void handleSaveButtonClicked();
  void handleDeleteButtonClicked();

  Ui::EditReaderForm *UI;
  QVector<Reader> AllReaders;
  int CurrentReaderID = -1;
  QTimer *SearchTimer;
};

#endif // EDITREADERFORM_H
