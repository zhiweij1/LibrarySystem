#include "CSVParser.h"

#include <QSet>
#include <QString>

ErrorOr<void> CSVParser::parse(const QString &Path) {
  QFile File(Path);
  if (!File.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return {ErrorCode::InternalError, QString("无法打开文件: %1").arg(Path)};
  }

  QTextStream In(&File);

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

  while (!In.atEnd()) {
    QString Line = In.readLine();
    LineNumber++;
    if (Line.trimmed().isEmpty())
      continue;

    QStringList Fields = Line.split(",");
    if (Fields.size() < 7) {
      return {ErrorCode::ValidationError,
              QString("第 %1 行错误：字段不足").arg(LineNumber)};
    }

    // 2. 校验册数
    bool OK;
    int ExpectedCount = Fields[4].toInt(&OK);
    if (!OK || ExpectedCount < 0) {
      return {ErrorCode::ValidationError,
              QString("第 %1 行错误：册数格式无效").arg(LineNumber)};
    }

    // 3. 校验条码
    QStringList Barcodes = Fields[6].trimmed().split(':', Qt::SkipEmptyParts);
    if (Barcodes.size() != ExpectedCount) {
      return {ErrorCode::ValidationError,
              QString("第 %1 行错误：条码数(%2)与册数(%3)不符")
                  .arg(LineNumber)
                  .arg(Barcodes.size())
                  .arg(ExpectedCount)};
    }

    // 文件内自查
    bool HasFileError = false;
    for (const QString &Code : std::as_const(Barcodes)) {
      QString TrimmedCode = Code.trimmed();
      if (SeenInFile.contains(TrimmedCode)) {
        ErrorMessages.append(QString("第 %1 行：条码 [%2] 在文件中重复")
                                 .arg(LineNumber)
                                 .arg(TrimmedCode));
        HasFileError = true;
      } else {
        SeenInFile.insert(TrimmedCode);
        AllBarcodesToQuery.insert(TrimmedCode);
      }
    }

    // 如果文件内已经发现重复，这条记录就不进待定区了
    if (!HasFileError) {
      PendingRecords.append(
          {LineNumber,
           RawData(Fields[0].trimmed(), Fields[1].trimmed(),
                   Fields[2].trimmed(), Fields[3].trimmed(), ExpectedCount,
                   Fields[5].trimmed(), Barcodes),
           Barcodes});
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
        ErrorMessages.append(QString("第 %1 行：条码 [%2] 已在系统库中存在")
                                 .arg(Temp.Line)
                                 .arg(Code.trimmed()));
        HasDatabaseConflict = true;
      }
    }

    // 只有既没有文件冲突，也没有数据库冲突的记录，才存入 Results
    if (!HasDatabaseConflict) {
      Results.append(Temp.Record);
    }
  }

  if (!ErrorMessages.isEmpty()) {
    return {ErrorCode::ValidationError, ErrorMessages.join("\n")};
  }

  return {};
}
