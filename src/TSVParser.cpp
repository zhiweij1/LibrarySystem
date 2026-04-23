#include "TSVParser.h"

#include <QList>
#include <QMap>
#include <QSet>
#include <QString>

ErrorOr<void> TSVParser::parse(const QString &Path) {
  QFile File(Path);
  if (!File.open(QIODevice::ReadOnly)) {
    return {ErrorCode::InternalError, QString("无法打开文件: %1").arg(Path)};
  }

  QString Content = QString::fromLocal8Bit(File.readAll());
  QTextStream In(&Content, QIODeviceBase::ReadOnly);

  int LineNumber = 0;
  Results.clear();

  QStringList ErrorMessages;        // 收集所有错误
  QSet<QString> SeenInFile;         // 用于检测文件内重复
  QSet<QString> AllBarcodesToQuery; // 用于批量查库

  struct TempRecord {
    int Line;
    RawData Record;
    QStringList Barcodes;
  };
  QList<TempRecord> PendingRecords;

  // 1. 处理表头
  if (!In.atEnd()) {
    In.readLine();
    LineNumber++;
  }

  // 每行一个副本，相同条码前缀（去掉最后两位副本编号）的多行聚合为一条 RawData
  QMap<QString, TempRecord> Aggregated;

  while (!In.atEnd()) {
    QString Line = In.readLine();
    LineNumber++;
    if (Line.trimmed().isEmpty())
      continue;

    QStringList Fields = Line.split("\t");
    if (Fields.size() < 6) {
      return {ErrorCode::ValidationError,
              QString("第 %1 行错误：字段不足").arg(LineNumber)};
    }

    // 字段：书名 作者 出版社 数量 图片名 条码号
    QString ImageName = Fields[4].trimmed();
    if (ImageName.isEmpty()) {
      return {ErrorCode::ValidationError,
              QString("第 %1 行错误：图片名为空").arg(LineNumber)};
    }
    if (ImageName.contains('/') || ImageName.contains('\\') ||
        ImageName.contains("..")) {
      return {ErrorCode::ValidationError,
              QString("第 %1 行错误：图片名包含非法字符").arg(LineNumber)};
    }

    bool OK;
    int ExpectedCount = Fields[3].toInt(&OK);
    if (!OK || ExpectedCount < 0) {
      return {ErrorCode::ValidationError,
              QString("第 %1 行错误：册数格式无效").arg(LineNumber)};
    }

    QString Barcode = Fields[5].trimmed();
    if (Barcode.isEmpty()) {
      return {ErrorCode::ValidationError,
              QString("第 %1 行错误：条码号为空").arg(LineNumber)};
    }

    // 文件内自查：条码重复
    if (SeenInFile.contains(Barcode)) {
      ErrorMessages.append(QString("第 %1 行：条码 [%2] 在文件中重复")
                               .arg(LineNumber)
                               .arg(Barcode));
      continue;
    }
    SeenInFile.insert(Barcode);
    AllBarcodesToQuery.insert(Barcode);

    // 以条码的前缀（去掉最后两位副本编号）作为聚合 key
    if (Barcode.size() < 2) {
      return {ErrorCode::ValidationError,
              QString("第 %1 行错误：条码号长度不足").arg(LineNumber)};
    }
    QString AggKey = Barcode.left(Barcode.size() - 2) + "xx";

    if (!Aggregated.contains(AggKey)) {
      Aggregated.insert(AggKey,
                        {LineNumber,
                         RawData(ImageName, Fields[0].trimmed(),
                                 Fields[1].trimmed(), Fields[2].trimmed(),
                                 ExpectedCount, {Barcode}),
                         {Barcode}});
    } else {
      auto It = Aggregated.find(AggKey);
      // 校验同一条码前缀下的书目字段是否与首行一致
      auto &Existing = It.value().Record;
      if (Existing.Title != Fields[0].trimmed() ||
          Existing.Author != Fields[1].trimmed() ||
          Existing.Publisher != Fields[2].trimmed() ||
          Existing.Count != ExpectedCount ||
          Existing.ImageName != ImageName) {
        ErrorMessages.append(
            QString("第 %1 行错误：与第 %2 行条码前缀相同，但书目信息或册数不一致")
                .arg(LineNumber)
                .arg(It.value().Line));
        continue;
      }
      It.value().Barcodes.append(Barcode);
      Existing.Barcodes.append(Barcode);
    }
  }

  // 校验聚合后的条码数量是否与册数一致
  for (auto It = Aggregated.begin(); It != Aggregated.end(); ++It) {
    if (It.value().Barcodes.size() != It.value().Record.Count) {
      ErrorMessages.append(
          QString("第 %1 行书籍 [%2] 条码数(%3)与册数(%4)不符")
              .arg(It.value().Line)
              .arg(It.value().Record.Title)
              .arg(It.value().Barcodes.size())
              .arg(It.value().Record.Count));
    }
  }

  // 将无文件内错误的记录加入待定区
  for (const auto &Temp : Aggregated) {
    if (Temp.Barcodes.size() == Temp.Record.Count) {
      PendingRecords.append(Temp);
    }
  }

  // 2. 批量查询数据库
  QSet<QString> ExistingInDB;
  const int BatchSize = 100;
  QList<QString> BarcodeList = AllBarcodesToQuery.values();
  for (int Idx = 0; Idx < BarcodeList.size(); Idx += BatchSize) {
    int Remaining = BarcodeList.size() - Idx;
    int CurrentBatchCount = qMin(BatchSize, Remaining);
    QSet<QString> BatchSet;
    for (int Jdx = 0; Jdx < CurrentBatchCount; ++Jdx) {
      BatchSet.insert(BarcodeList[Idx + Jdx]);
    }
    auto DBCheckRes =
        LibrarySystem::getInstance().checkExistingBarcodes(BatchSet);
    if (!DBCheckRes) {
      return {ErrorCode::DatabaseError,
              QString("批量检查条码失败 (批次 %1): %2")
                  .arg(Idx / BatchSize + 1)
                  .arg(DBCheckRes.getErrMsg())};
    }
    ExistingInDB.unite(DBCheckRes.getValue());
  }

  // 3. 最终过滤：只有那些条码完全不在数据库里的记录，才加入真正的 Results
  for (const auto &Temp : PendingRecords) {
    bool HasDatabaseConflict = false;
    for (const QString &Code : Temp.Barcodes) {
      if (ExistingInDB.contains(Code.trimmed())) {
        ErrorMessages.append(QString("第 %1 行书籍 [%2]：条码 [%3] 已在系统库中存在")
                                 .arg(Temp.Line)
                                 .arg(Temp.Record.Title)
                                 .arg(Code.trimmed()));
        HasDatabaseConflict = true;
      }
    }

    if (!HasDatabaseConflict) {
      Results.append(Temp.Record);
    }
  }

  if (!ErrorMessages.isEmpty()) {
    Results.clear(); // 出错时清空结果，避免残留脏数据
    return {ErrorCode::ValidationError, ErrorMessages.join("\n")};
  }

  return {};
}
