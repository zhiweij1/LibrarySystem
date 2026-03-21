#include "Library.h"
#include "MainWindow.h"

#include <QApplication>
#include <QFile>
#include <QMessageBox>

namespace {
QFile *OutFilePtr = nullptr;
void messageHandler(QtMsgType Type, const QMessageLogContext &Context,
                    const QString &Msg) {
  QString Txt;
  QString Time = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");

  switch (Type) {
  case QtDebugMsg:
    Txt = QString("Debug: %1").arg(Msg);
    break;
  case QtWarningMsg:
    Txt = QString("Warning: %1").arg(Msg);
    break;
  case QtCriticalMsg:
    Txt = QString("Critical: %1").arg(Msg);
    break;
  case QtFatalMsg:
    Txt = QString("Fatal: %1").arg(Msg);
    break;
  case QtInfoMsg:
    Txt = QString("Info: %1").arg(Msg);
    break;
  }
  QTextStream TS(OutFilePtr);
  TS << Time << " " << Txt << " (" << Context.file << ":" << Context.line << ")"
     << Qt::endl;
}
} // namespace

int main(int argc, char *argv[]) {
  QApplication App(argc, argv);

  App.setApplicationVersion(GIT_REVISION);
  qDebug() << "App Version:" << qApp->applicationVersion();

  QFile OutFile("log.txt");
  if (!OutFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
    QMessageBox::critical(nullptr, "", "错误：日志文件无法打开，程序将退出。");
    exit(-1);
  }
  OutFilePtr = &OutFile;
  qInstallMessageHandler(messageHandler);

  if (!LibrarySystem::getInstance().init("library.db")) {
    QMessageBox::critical(nullptr, "", "错误：数据库无法启动，程序将退出。");
    exit(-2);
  }

  QFont Font;
  Font.setFamily("Microsoft YaHei");
  Font.setPointSize(16);
  App.setFont(Font);

  // 全局样式：统一所有窗口为卡片式现代风格
  App.setStyleSheet(R"(
/* 全局背景 - 仅作用于顶级容器，不影响 QScrollArea 内部 QWidget */
QWidget#ContentFrame,
QFrame#frame {
    background-color: #F5F5F5;
}

/* 按钮 */
QPushButton {
    background-color: #FFFFFF;
    border: 1px solid #E0E0E0;
    border-radius: 4px;
    padding: 5px 15px;
    min-height: 30px;
    color: #333333;
}
QPushButton:hover {
    background-color: #F0F0F0;
    border-color: #CCCCCC;
}
QPushButton:pressed {
    background-color: #E0E0E0;
}
QPushButton:disabled {
    background-color: #F5F5F5;
    color: #AAAAAA;
    border-color: #EEEEEE;
}

/* 输入框 */
QLineEdit {
    background-color: #FFFFFF;
    border: 1px solid #E0E0E0;
    border-radius: 4px;
    padding: 5px 10px;
    min-height: 30px;
    color: #333333;
    selection-background-color: #BBDEFB;
}
QLineEdit:focus {
    border-color: #90CAF9;
}

/* 下拉框 */
QComboBox {
    background-color: #FFFFFF;
    border: 1px solid #E0E0E0;
    border-radius: 4px;
    padding: 5px 10px;
    min-height: 30px;
    color: #333333;
}
QComboBox:hover {
    border-color: #CCCCCC;
}
QComboBox:focus {
    border-color: #90CAF9;
}
QComboBox::drop-down {
    border: none;
    width: 20px;
}
QComboBox QAbstractItemView {
    background-color: #FFFFFF;
    border: 1px solid #E0E0E0;
    border-radius: 4px;
    selection-background-color: #E3F2FD;
    selection-color: #333333;
}

/* 数值输入框 */
QSpinBox {
    background-color: #FFFFFF;
    border: 1px solid #E0E0E0;
    border-radius: 4px;
    padding: 5px 10px;
    min-height: 30px;
    color: #333333;
}
QSpinBox:focus {
    border-color: #90CAF9;
}

/* 表格 */
QTableWidget, QTableView {
    background-color: #FFFFFF;
    border: 1px solid #E0E0E0;
    border-radius: 6px;
    gridline-color: #EEEEEE;
    color: #333333;
    alternate-background-color: #FAFAFA;
    font-size: 20px;
}
QTableWidget::item, QTableView::item {
    padding: 4px;
    font-size: 20px;
}
QTableWidget::item:selected, QTableView::item:selected {
    background-color: #E3F2FD;
    color: #333333;
}
QHeaderView::section {
    background-color: #FAFAFA;
    border: none;
    border-bottom: 1px solid #E0E0E0;
    border-right: 1px solid #EEEEEE;
    padding: 6px;
    font-weight: bold;
    color: #333333;
}

/* 滚动区域 */
QScrollArea {
    border: none;
    background: transparent;
}

/* 分组框 */
QGroupBox {
    border: 1px solid #E0E0E0;
    border-radius: 6px;
    margin-top: 12px;
    padding: 15px 10px 10px 10px;
    background-color: #FFFFFF;
    font-weight: bold;
    color: #333333;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 12px;
    padding: 0 5px;
    color: #333333;
}

/* 滚动条 - 垂直 */
QScrollBar:vertical {
    border: none;
    background: transparent;
    width: 8px;
}
QScrollBar::handle:vertical {
    background: #C0C0C0;
    border-radius: 4px;
    min-height: 20px;
}
QScrollBar::handle:vertical:hover {
    background: #A0A0A0;
}
QScrollBar::add-line:vertical,
QScrollBar::sub-line:vertical {
    height: 0;
}
QScrollBar::add-page:vertical,
QScrollBar::sub-page:vertical {
    background: none;
}

/* 滚动条 - 水平 */
QScrollBar:horizontal {
    border: none;
    background: transparent;
    height: 8px;
}
QScrollBar::handle:horizontal {
    background: #C0C0C0;
    border-radius: 4px;
    min-width: 20px;
}
QScrollBar::handle:horizontal:hover {
    background: #A0A0A0;
}
QScrollBar::add-line:horizontal,
QScrollBar::sub-line:horizontal {
    width: 0;
}
QScrollBar::add-page:horizontal,
QScrollBar::sub-page:horizontal {
    background: none;
}

/* 复选框 */
QCheckBox {
    color: #333333;
    spacing: 5px;
}
QCheckBox::indicator {
    width: 16px;
    height: 16px;
    border: 1px solid #E0E0E0;
    border-radius: 3px;
    background-color: #FFFFFF;
}
QCheckBox::indicator:checked {
    background-color: #90CAF9;
    border-color: #90CAF9;
}

/* 菜单栏 */
QMenuBar {
    background-color: #FAFAFA;
    border-bottom: 1px solid #E0E0E0;
    color: #333333;
}
QMenuBar::item:selected {
    background-color: #E3F2FD;
}

/* 菜单 */
QMenu {
    background-color: #FFFFFF;
    border: 1px solid #E0E0E0;
    border-radius: 4px;
    color: #333333;
}
QMenu::item:selected {
    background-color: #E3F2FD;
}

/* 状态栏 */
QStatusBar {
    background-color: #FAFAFA;
    border-top: 1px solid #E0E0E0;
    color: #666666;
}
)");

  qInfo() << "程序开始运行";
  MainWindow W;
  W.show();
  return App.exec();
}
