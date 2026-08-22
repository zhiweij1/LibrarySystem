#include "Library.h"
#include "MainWindow.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>

namespace {
QFile *OutFilePtr = nullptr;
void messageHandler(QtMsgType Type, const QMessageLogContext &Context,
                    const QString &Msg) {
  if (!OutFilePtr)
    return;
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

  QString DataDir =
      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
      "/LibrarySystemData";
  QDir().mkpath(DataDir);

  // 日志文件
  QFile *OutFile = new QFile(DataDir + "/log.txt");
  if (!OutFile->open(QIODevice::WriteOnly | QIODevice::Append)) {
    delete OutFile;
    QMessageBox::critical(nullptr, "", "错误：日志文件无法打开，程序将退出。");
    exit(-1);
  }
  OutFilePtr = OutFile;
  qInstallMessageHandler(messageHandler);

  // 数据文件：从 QSettings 读取上次路径，找不到则让用户选择
  QSettings Settings(DataDir + "/settings.ini", QSettings::IniFormat);
  QString XlsxPath = Settings.value("data/xlsxPath").toString();

  if (XlsxPath.isEmpty() || !QFile::exists(XlsxPath)) {
    XlsxPath = QFileDialog::getOpenFileName(
        nullptr, "选择数据文件",
        XlsxPath.isEmpty() ? QDir::homePath()
                           : QFileInfo(XlsxPath).absolutePath(),
        "Excel 文件 (*.xlsx)");
    if (XlsxPath.isEmpty()) {
      delete OutFile;
      exit(0); // 用户取消
    }
    Settings.setValue("data/xlsxPath", XlsxPath);
    Settings.sync();
  }

  // 借阅规则配置：借期天数 / 每人最多可借本数
  // （读回并写入默认值，方便用户直接在 ini 中查看和修改）
  bool OkVal = false;
  int BorrowDays = Settings.value("borrow/days", 30).toInt(&OkVal);
  if (!OkVal || BorrowDays < 1)
    BorrowDays = 30;
  int MaxBooks = Settings.value("borrow/maxBooks", 3).toInt(&OkVal);
  if (!OkVal || MaxBooks < 1)
    MaxBooks = 3;
  Settings.setValue("borrow/days", BorrowDays);
  Settings.setValue("borrow/maxBooks", MaxBooks);
  Settings.sync();

  if (!LibrarySystem::getInstance().init(XlsxPath, BorrowDays, MaxBooks)) {
    QMessageBox::critical(
        nullptr, "", "错误：无法打开数据文件，程序将退出。\n路径: " + XlsxPath);
    exit(-2);
  }

  // 字体设置
  QFont Font;
  QStringList PreferredFonts = {"Microsoft YaHei",  "微软雅黑", "PingFang SC",
                                "Noto Sans CJK SC", "SimHei",   "黑体"};
  for (const auto &Family : PreferredFonts) {
    if (QFontDatabase::hasFamily(Family)) {
      Font.setFamily(Family);
      break;
    }
  }
  Font.setPointSize(16);
  App.setFont(Font);

  QFile StyleFile(":/styles/app.qss");
  if (StyleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    App.setStyleSheet(QString::fromUtf8(StyleFile.readAll()));
    qInfo() << "样式表加载成功";
  } else {
    qWarning() << "样式表加载失败，使用默认样式"
               << "，路径:" << StyleFile.fileName()
               << "，错误:" << StyleFile.errorString();
  }

  qInfo() << "程序开始运行，数据文件:" << XlsxPath;

  MainWindow W;
  W.show();
  int Ret = App.exec();
  qInstallMessageHandler(nullptr);
  OutFilePtr = nullptr;
  delete OutFile;
  return Ret;
}
