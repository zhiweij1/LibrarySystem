#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "BorrowBookForm.h"
#include "EditBookForm.h"
#include "EditReaderForm.h"
#include "QueryBookForm.h"
#include "ReturnBookForm.h"

#include <QMainWindow>
#include <QMap>
#include <QPushButton>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
} // namespace Ui
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow(QWidget *Parent = nullptr);
  ~MainWindow();

private:
  void handleBorrowBookButtonClicked();
  void handleReturnBookButtonClicked();
  void handleBookStatusQueryButtonClicked();
  void handleEditBookButtonClicked();
  void handleEditReaderButtonClicked();
  void handleAboutClicked();

  Ui::MainWindow *UI;

  BorrowBookForm *BorrowPage;
  ReturnBookForm *ReturnPage;
  EditBookForm *EditBookPage;
  EditReaderForm *EditReaderPage;
  QueryBookForm *QueryBookPage;

  enum class Theme { red, orange, purple, green, brown };
  const QMap<Theme, QString> ThemeMap = {
      {Theme::red, "#FFB1B1"},    {Theme::orange, "#FFD8A8"},
      {Theme::purple, "#E2D1F9"}, {Theme::green, "#C1E1C1"},
      {Theme::brown, "#A67C52"},
  };
  void cleanTheme();
  void changeTheme(const Theme T, QPushButton *Btn);
};
#endif // MAINWINDOW_H
