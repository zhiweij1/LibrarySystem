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

  // 全局样式：从 Qt 资源中的 QSS 文件加载，便于维护和复用
  QFile StyleFile(":/styles/app.qss");
  if (StyleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    App.setStyleSheet(QString::fromUtf8(StyleFile.readAll()));
    qInfo() << "样式表加载成功";
  } else {
    qWarning() << "样式表加载失败，使用默认样式"
               << "，路径:" << StyleFile.fileName()
               << "，错误:" << StyleFile.errorString();
  }

  qInfo() << "程序开始运行";
  MainWindow W;
  W.show();
  return App.exec();
}
