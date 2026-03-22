#include "Library.h"
#include "MainWindow.h"

#include <QApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>

namespace {
QFile *OutFilePtr = nullptr;
void messageHandler(QtMsgType Type, const QMessageLogContext &Context,
                    const QString &Msg) {
  if (!OutFilePtr) return;
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

void dailyBackup() {
  auto &Lib = LibrarySystem::getInstance();
  QString DataDir = Lib.getDataDir();

  QSettings Settings(DataDir + "/settings.ini", QSettings::IniFormat);

  QString Today = QDate::currentDate().toString(Qt::ISODate);
  QString LastBackupDate = Settings.value("backup/lastDate").toString();

  if (LastBackupDate == Today) {
    return;  // 今天已经备份过
  }

  QString BackupDir = DataDir + "/backups";
  QDir().mkpath(BackupDir);

  QString BackupPath = BackupDir + "/backup_" +
                       QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") +
                       ".db";

  auto Res = Lib.backupDatabaseTo(BackupPath);
  if (!Res) {
    qCritical() << "每日备份失败:" << Res.getErrMsg();
    QMessageBox::critical(nullptr, "",
                          "错误：数据库备份失败 - " + Res.getErrMsg() +
                              "，程序将退出。");
    exit(-3);
  }

  qInfo() << "每日备份成功:" << BackupPath;
  Settings.setValue("backup/lastDate", Today);

  // 复制到坚果云同步目录（如果已配置）
  QString CloudDir = Settings.value("backup/cloud_dir").toString();
  if (!CloudDir.isEmpty() && QDir(CloudDir).exists()) {
    QString CloudPath = CloudDir + "/" + QFileInfo(BackupPath).fileName();
    if (QFile::copy(BackupPath, CloudPath)) {
      qInfo() << "已复制到云同步目录:" << CloudPath;
    } else {
      qWarning() << "复制到云同步目录失败";
    }
  }
}

int main(int argc, char *argv[]) {
  QApplication App(argc, argv);

  App.setApplicationVersion(GIT_REVISION);
  qDebug() << "App Version:" << qApp->applicationVersion();

  // 数据目录：用户文档目录下的 LibrarySystemData 文件夹
  QString DataDir =
      QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
      "/LibrarySystemData";
  QDir().mkpath(DataDir);

  // 日志文件（堆分配，确保生命周期覆盖整个程序）
  QFile *OutFile = new QFile(DataDir + "/log.txt");
  if (!OutFile->open(QIODevice::WriteOnly | QIODevice::Append)) {
    QMessageBox::critical(nullptr, "", "错误：日志文件无法打开，程序将退出。");
    exit(-1);
  }
  OutFilePtr = OutFile;
  qInstallMessageHandler(messageHandler);

  // 数据库文件
  QString DBPath = DataDir + "/library.db";
  if (!LibrarySystem::getInstance().init(DBPath)) {
    QMessageBox::critical(nullptr, "", "错误：数据库无法启动，程序将退出。");
    exit(-2);
  }

  // 字体设置：尝试使用可用字体
  QFont Font;
  QStringList PreferredFonts = {"Microsoft YaHei", "微软雅黑", "PingFang SC",
                                "Noto Sans CJK SC", "SimHei", "黑体"};
  for (const auto &Family : PreferredFonts) {
    if (QFontDatabase::hasFamily(Family)) {
      Font.setFamily(Family);
      break;
    }
  }
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

  // 每天第一次打开程序时备份（在创建 UI 之前，避免数据库关闭影响模型）
  dailyBackup();

  MainWindow W;
  W.show();
  int Ret = App.exec();
  qInstallMessageHandler(nullptr);
  OutFilePtr = nullptr;
  delete OutFile;
  return Ret;
}
