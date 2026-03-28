#include "Library.h"

#include "CSVParser.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>

ErrorOr<void> LibrarySystem::backupDatabaseTo(const QString &BackupPath) {
  closeDatabase();
  bool Success = QFile::copy(DBPath, BackupPath);
  if (!openDatabase()) {
    return {ErrorCode::DatabaseError, "备份后重新打开数据库失败"};
  }
  if (!Success) {
    return {ErrorCode::InternalError, "复制数据库文件失败"};
  }
  return {};
}

ErrorOr<bool> LibrarySystem::isBarcodeExists(const QString &Barcode) {
  QSqlQuery Query(DB);
  Query.prepare("SELECT 1 FROM bookcopy WHERE barcode = :code LIMIT 1");
  Query.bindValue(":code", Barcode);
  if (!Query.exec())
    return {ErrorCode::DatabaseError, "查询失败: " + Query.lastError().text()};
  return Query.next();
}

ErrorOr<void> LibrarySystem::importFromCSV(const QString &FilePath) {
  CSVParser Parser;
  // 1. 调用新接口解析 CSV，获取详细报错
  auto ParseRes = Parser.parse(FilePath);
  if (!ParseRes) {
    return ParseRes;
  }

  QString CoverTargetDir = DataDir + "/covers/";
  QDir().mkpath(CoverTargetDir);

  // 2. 开启事务
  if (!DB.transaction()) {
    return {ErrorCode::DatabaseError, "事务启动失败: " + DB.lastError().text()};
  }

  QSqlQuery Query(DB);
  int Total = Parser.Results.size();
  int Current = 0;
  for (const auto &Data : std::as_const(Parser.Results)) {
    ++Current;
    emit importProgress(Current, Total);
    // 拼接封面路径
    QString SourcePath =
        QFileInfo(FilePath).absolutePath() + "/photos/" + Data.CSVID + ".jpg";
    QString TargetPath = CoverTargetDir + Data.CSVID + ".jpg";
    QString RelativePath = "covers/" + Data.CSVID + ".jpg";

    // 如果目标文件已存在，copy 会失败，建议先删除或检查
    if (QFile::exists(SourcePath)) {
      if (QFile::exists(TargetPath))
        QFile::remove(TargetPath);
      QFile::copy(SourcePath, TargetPath);
    }

    // 3. 插入书籍信息 (BookInfo)
    Query.prepare("INSERT INTO bookinfo (title, author, publisher, cover_path, "
                  "category_id) "
                  "VALUES (:t, :a, :p, :c, :cat)");
    Query.bindValue(":t", Data.Title);
    Query.bindValue(":a", Data.Author);
    Query.bindValue(":p", Data.Publisher);
    Query.bindValue(":c", RelativePath);
    Query.bindValue(":cat", Data.Category);

    if (!Query.exec()) {
      DB.rollback();
      return {ErrorCode::DatabaseError,
              "导入失败: " + Query.lastError().text()};
    }

    int InfoID = Query.lastInsertId().toInt();

    // 4. 插入书籍副本 (BookCopy)
    for (const QString &Barcode : Data.Barcodes) {
      Query.prepare("INSERT INTO bookcopy (info_id, barcode, status) "
                    "VALUES (:iid, :bc, 0)");
      Query.bindValue(":iid", InfoID);
      Query.bindValue(":bc", Barcode);

      if (!Query.exec()) {
        DB.rollback();
        return {ErrorCode::DatabaseError,
                "导入失败: " + Query.lastError().text()};
      }
    }
  }

  // 5. 提交事务
  if (DB.commit()) {
    return {};
  }
  DB.rollback();
  return {ErrorCode::DatabaseError, "提交失败: " + DB.lastError().text()};
}

ErrorOr<void> LibrarySystem::init(const QString &DBPath) {
  this->DBPath = DBPath;
  this->DataDir = QFileInfo(DBPath).absolutePath();  // 数据目录路径

  if (QSqlDatabase::contains("qt_sql_default_connection")) {
    DB = QSqlDatabase::database("qt_sql_default_connection");
  } else {
    DB = QSqlDatabase::addDatabase("QSQLITE");
  }

  DB.setDatabaseName(DBPath);

  if (!DB.open())
    return {ErrorCode::DatabaseError,
            "无法打开数据库: " + DB.lastError().text()};

  QSqlQuery Pragma(DB);
  Pragma.exec("PRAGMA foreign_keys = ON;");

  QSqlQuery Query(DB);

  // BookInfo
  bool OK = Query.exec("CREATE TABLE IF NOT EXISTS bookinfo ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "title TEXT NOT NULL,"
                       "author TEXT,"
                       "publisher TEXT,"
                       "category_id TEXT," // CLC
                       "cover_path TEXT"
                       ")");

  // BookCopy
  OK &= Query.exec(
      QString("CREATE TABLE IF NOT EXISTS bookcopy ("
              "id INTEGER PRIMARY KEY AUTOINCREMENT,"
              "info_id INTEGER,"
              "barcode TEXT UNIQUE,"
              "status INTEGER DEFAULT %1,"
              "FOREIGN KEY(info_id) REFERENCES bookinfo(id) ON DELETE CASCADE"
              ")")
          .arg(BookCopy::BookStatus::BS_InLibrary));

  // Reader
  OK &= Query.exec(QString("CREATE TABLE IF NOT EXISTS reader ("
                           "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                           "name TEXT NOT NULL,"
                           "card_number TEXT UNIQUE,"
                           "phone TEXT,"
                           "is_inactive INTEGER DEFAULT %1)")
                       .arg(RS_Active));

  // BorrowRecord
  OK &= Query.exec("CREATE TABLE IF NOT EXISTS borrow_record ("
                   "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                   "reader_id INTEGER,"
                   "copy_id INTEGER,"
                   "borrow_date DATE DEFAULT (date('now')),"
                   "due_date DATE,"
                   "return_date DATE,"
                   "FOREIGN KEY(reader_id) REFERENCES reader(id),"
                   "FOREIGN KEY(copy_id) REFERENCES bookcopy(id)"
                   ")");

  if (!OK)
    return {ErrorCode::DatabaseError, "建表失败: " + Query.lastError().text()};

  return {};
}

ErrorOr<std::pair<BookInfo, BookCopy>>
LibrarySystem::getBookDataByBarcode(const QString &Barcode) {
  QSqlQuery Query(DB);
  Query.prepare(R"(
        SELECT 
            bi.id, bi.title, bi.author, bi.publisher, bi.cover_path,
            bc.id, bc.info_id, bc.barcode, bc.status
        FROM bookcopy bc
        JOIN bookinfo bi ON bc.info_id = bi.id
        WHERE bc.barcode = :barcode
    )");

  Query.bindValue(":barcode", Barcode);

  if (!Query.exec())
    return {ErrorCode::DatabaseError,
            "查询书籍失败:" + Query.lastError().text()};

  if (Query.next()) {
    BookInfo Info;
    BookCopy Copy;

    Info.ID = Query.value(0).toInt();
    Info.Title = Query.value(1).toString();
    Info.Author = Query.value(2).toString();
    Info.Publisher = Query.value(3).toString();
    Info.CoverPath = Query.value(4).toString();

    Copy.ID = Query.value(5).toInt();
    Copy.InfoID = Query.value(6).toInt();
    Copy.Barcode = Query.value(7).toString();
    Copy.Status = static_cast<BookCopy::BookStatus>(Query.value(8).toInt());

    return std::make_pair(Info, Copy);
  }

  return {ErrorCode::NotFound, "条码不存在: " + Barcode};
}

ErrorOr<Reader>
LibrarySystem::getReaderByCardNumber(const QString &CardNumber) {
  QSqlQuery Query(DB);
  Query.prepare("SELECT id, name, card_number, phone FROM reader WHERE "
                "card_number = :card");
  Query.bindValue(":card", CardNumber);

  if (Query.exec() && Query.next()) {
    Reader Rdr;
    Rdr.ID = Query.value("id").toInt();
    Rdr.Name = Query.value("name").toString();
    Rdr.CardNumber = Query.value("card_number").toString();
    Rdr.PhoneNumber = Query.value("phone").toString();
    return Rdr;
  }
  return {ErrorCode::NotFound, "读者不存在: " + CardNumber};
}

ErrorOr<void> LibrarySystem::borrowBook(QSqlQuery &Query, const int ReaderID,
                                        const int CopyID) {
  // 检查书籍当前状态
  Query.prepare("SELECT status FROM bookcopy WHERE id = :cid");
  Query.bindValue(":cid", CopyID);
  if (!Query.exec())
    return {ErrorCode::DatabaseError,
            "查询书籍副本状态失败: " + Query.lastError().text()};
  if (!Query.next())
    return {ErrorCode::NotFound,
            "未找到书籍副本，副本ID: " + QString::number(CopyID)};

  int Status = Query.value(0).toInt();
  if (Status == BookCopy::BookStatus::BS_Borrowed)
    return {ErrorCode::InvalidStatus, "该书籍目前处于'借出'状态，无法再次借出"};
  if (Status == BookCopy::BookStatus::BS_Lost)
    return {ErrorCode::InvalidStatus, "该书籍目前处于'遗失'状态，无法借出"};

  Query.prepare(
      "INSERT INTO borrow_record (reader_id, copy_id, borrow_date, due_date) "
      "VALUES (:rid, :cid, date('now'), date('now', '+30 days'))");
  Query.bindValue(":rid", ReaderID);
  Query.bindValue(":cid", CopyID);

  if (!Query.exec())
    return {ErrorCode::DatabaseError,
            "写入借阅记录失败: " + Query.lastError().text()};

  Query.prepare("UPDATE bookcopy SET status = :status WHERE id = :id");
  Query.bindValue(":status", BookCopy::BS_Borrowed);
  Query.bindValue(":id", CopyID);

  if (!Query.exec())
    return {ErrorCode::DatabaseError,
            "更新书籍状态失败: " + Query.lastError().text()};
  return {};
}

ErrorOr<void> LibrarySystem::borrowBooks(int ReaderID,
                                         const QVector<int> &CopyIDs) {
  if (CopyIDs.isEmpty())
    return {ErrorCode::ValidationError, "未选择任何书籍"};

  if (!DB.transaction()) {
    return {ErrorCode::DatabaseError, "事务启动失败: " + DB.lastError().text()};
  }

  QSqlQuery Query(DB);

  // 检查读者状态（只查询一次）
  Query.prepare("SELECT is_inactive FROM reader WHERE id = :rid");
  Query.bindValue(":rid", ReaderID);
  if (!Query.exec()) {
    DB.rollback();
    return {ErrorCode::DatabaseError,
            "查询读者状态失败: " + Query.lastError().text()};
  }
  if (!Query.next()) {
    DB.rollback();
    return {ErrorCode::NotFound, "读者不存在"};
  }
  if (Query.value(0).toInt() == RS_InActive) {
    DB.rollback();
    return {ErrorCode::InvalidStatus, "该读者已注销，无法借书"};
  }

  for (int CID : CopyIDs) {
    auto Res = borrowBook(Query, ReaderID, CID);
    if (!Res) {
      DB.rollback();
      return Res;
    }
  }

  if (DB.commit())
    return {};
  DB.rollback();
  return {ErrorCode::DatabaseError,
          "提交借书事务失败: " + DB.lastError().text()};
}

ErrorOr<QVector<BorrowDetailType>>
LibrarySystem::getBorrowingDetailsByReader(int ReaderId) {
  QVector<BorrowDetailType> Results;
  QSqlQuery Query(DB);

  QString Sql =
      "SELECT "
      "br.id, br.reader_id, br.copy_id, br.borrow_date, br.due_date, "
      "br.return_date, "                                         // br (0-5)
      "bc.id, bc.info_id, bc.barcode, bc.status, "               // bc (6-9)
      "bi.id, bi.title, bi.author, bi.publisher, bi.cover_path " // bi (10-14)
      "FROM borrow_record br "
      "JOIN bookcopy bc ON br.copy_id = bc.id "
      "JOIN bookinfo bi ON bc.info_id = bi.id "
      "WHERE br.reader_id = :rid AND br.return_date IS NULL"; // 只查未归还的

  Query.prepare(Sql);
  Query.bindValue(":rid", ReaderId);

  if (Query.exec()) {
    while (Query.next()) {
      BorrowDetailType Detail;

      // fill BorrowRecord (0-5)
      Detail.Record.ID = Query.value(0).toInt();
      Detail.Record.ReaderId = Query.value(1).toInt();
      Detail.Record.CopyId = Query.value(2).toInt();
      Detail.Record.BorrowDate = Query.value(3).toDateTime();
      Detail.Record.DueDate = Query.value(4).toDateTime();
      Detail.Record.ReturnDate = Query.value(5).toDateTime();

      // fill BookCopy (6-9)
      Detail.Copy.ID = Query.value(6).toInt();
      Detail.Copy.InfoID = Query.value(7).toInt();
      Detail.Copy.Barcode = Query.value(8).toString();
      Detail.Copy.Status =
          static_cast<BookCopy::BookStatus>(Query.value(9).toInt());

      // fill BookInfo (10-14)
      Detail.Info.ID = Query.value(10).toInt();
      Detail.Info.Title = Query.value(11).toString();
      Detail.Info.Author = Query.value(12).toString();
      Detail.Info.Publisher = Query.value(13).toString();
      Detail.Info.CoverPath = Query.value(14).toString();

      Results.append(Detail);
    }
    return Results;
  }
  return {ErrorCode::DatabaseError,
          "获取读者借阅详情失败: " + Query.lastError().text()};
}

ErrorOr<void> LibrarySystem::returnBook(QSqlQuery &Query, const int RecordID) {
  int CopyId = -1;
  int Status = -1;

  // 获取副本ID及其状态
  Query.prepare(
      "SELECT br.copy_id, bc.status FROM borrow_record br "
      "JOIN bookcopy bc ON br.copy_id = bc.id "
      "WHERE br.id = :rid");
  Query.bindValue(":rid", RecordID);
  if (!Query.exec())
    return {ErrorCode::DatabaseError,
            "查询借阅记录失败: " + Query.lastError().text()};
  if (!Query.next())
    return {ErrorCode::NotFound,
            "未找到借阅记录，记录ID: " + QString::number(RecordID)};
  CopyId = Query.value(0).toInt();
  Status = Query.value(1).toInt();

  if (Status == BookCopy::BookStatus::BS_Lost)
    return {ErrorCode::InvalidStatus, "该书籍处于'遗失'状态，请先办理挂失处理后再归还"};

  // 更新归还日期
  Query.prepare(
      "UPDATE borrow_record SET return_date = date('now') WHERE id = :rid");
  Query.bindValue(":rid", RecordID);
  if (!Query.exec())
    return {ErrorCode::DatabaseError,
            "更新归还时间失败: " + Query.lastError().text()};

  // 更新书籍副本状态为 0 (在库)
  Query.prepare("UPDATE bookcopy SET status = 0 WHERE id = :cid");
  Query.bindValue(":cid", CopyId);
  if (!Query.exec())
    return {ErrorCode::DatabaseError,
            "恢复书籍在库状态失败: " + Query.lastError().text()};

  return {};
}

ErrorOr<void> LibrarySystem::returnBooks(const QList<int> &RecordIDs) {
  if (RecordIDs.isEmpty())
    return {};

  if (!DB.transaction()) {
    return {ErrorCode::DatabaseError, "事务启动失败: " + DB.lastError().text()};
  }

  QSqlQuery Query(DB);
  for (int RID : RecordIDs) {
    auto Res = returnBook(Query, RID);
    if (!Res) {
      DB.rollback();
      return Res;
    }
  }

  if (DB.commit()) {
    return {};
  }

  DB.rollback();
  return {ErrorCode::DatabaseError,
          "提交归还事务失败: " + DB.lastError().text()};
}

ErrorOr<void> LibrarySystem::renewBook(QSqlQuery &Query, const int RecordID) {
  // 先查询借阅记录对应的书籍副本状态
  Query.prepare(
      "SELECT bc.status FROM borrow_record br "
      "JOIN bookcopy bc ON br.copy_id = bc.id "
      "WHERE br.id = :rid");
  Query.bindValue(":rid", RecordID);
  if (!Query.exec())
    return {ErrorCode::DatabaseError,
            "查询借阅记录失败: " + Query.lastError().text()};
  if (!Query.next())
    return {ErrorCode::NotFound,
            "未找到借阅记录，记录ID: " + QString::number(RecordID)};

  int Status = Query.value(0).toInt();
  if (Status == BookCopy::BookStatus::BS_Lost)
    return {ErrorCode::InvalidStatus, "该书籍处于'遗失'状态，无法续借"};

  Query.prepare(
      "UPDATE borrow_record SET due_date = date(due_date, '+30 days') "
      "WHERE id = :rid");
  Query.bindValue(":rid", RecordID);

  if (!Query.exec())
    return {ErrorCode::DatabaseError,
            "更新续借时间失败: " + Query.lastError().text()};
  return {};
}

ErrorOr<void> LibrarySystem::renewBooks(const QList<int> &RecordIDs) {
  if (RecordIDs.isEmpty())
    return {};

  if (!DB.transaction()) {
    return {ErrorCode::DatabaseError, "事务启动失败: " + DB.lastError().text()};
  }

  QSqlQuery Query(DB);
  for (int RID : RecordIDs) {
    auto Res = renewBook(Query, RID);
    if (!Res) {
      DB.rollback();
      return Res;
    }
  }

  if (DB.commit()) {
    return {};
  }

  DB.rollback();
  return {ErrorCode::DatabaseError,
          "提交续借事务失败: " + DB.lastError().text()};
}

ErrorOr<QVector<std::pair<BorrowDetailType, Reader>>>
LibrarySystem::queryBooks(const QString &Barcode, const QString &Title,
                          const QString &Author, const QString &Publisher) {
  QVector<std::pair<BorrowDetailType, Reader>> Results;
  QSqlQuery Query(DB);

  QString Sql =
      "SELECT "
      "bi.id, bi.title, bi.author, bi.publisher, bi.cover_path, " // bi (0-4)
      "bc.id, bc.barcode, bc.status, "                            // bc (5-7)
      "r.id, r.name, r.card_number, r.phone, "                    // r  (8-11)
      "br.id, br.borrow_date, br.due_date, br.return_date "       // br (12-15)
      "FROM bookinfo bi "
      "JOIN bookcopy bc ON bi.id = bc.info_id "
      "LEFT JOIN borrow_record br ON bc.id = br.copy_id AND br.return_date IS "
      "NULL "
      "LEFT JOIN reader r ON br.reader_id = r.id "
      "WHERE 1=1 ";

  if (!Barcode.isEmpty())
    Sql += " AND bc.barcode LIKE :barcode";
  if (!Title.isEmpty())
    Sql += " AND bi.title LIKE :title";
  if (!Author.isEmpty())
    Sql += " AND bi.author LIKE :author";
  if (!Publisher.isEmpty())
    Sql += " AND bi.publisher LIKE :publisher";

  Query.prepare(Sql);

  if (!Barcode.isEmpty())
    Query.bindValue(":barcode", "%" + Barcode + "%");
  if (!Title.isEmpty())
    Query.bindValue(":title", "%" + Title + "%");
  if (!Author.isEmpty())
    Query.bindValue(":author", "%" + Author + "%");
  if (!Publisher.isEmpty())
    Query.bindValue(":publisher", "%" + Publisher + "%");

  if (!Query.exec()) {
    return {ErrorCode::DatabaseError,
            "查询执行失败: " + Query.lastError().text()};
  }

  while (Query.next()) {
    BorrowDetailType Detail;
    Reader Rdr;

    Detail.Info.ID = Query.value(0).toInt();
    Detail.Info.Title = Query.value(1).toString();
    Detail.Info.Author = Query.value(2).toString();
    Detail.Info.Publisher = Query.value(3).toString();
    Detail.Info.CoverPath = Query.value(4).toString();

    Detail.Copy.ID = Query.value(5).toInt();
    Detail.Copy.Barcode = Query.value(6).toString();
    Detail.Copy.Status =
        static_cast<BookCopy::BookStatus>(Query.value(7).toInt());

    if (Query.value(8).isNull()) {
      Rdr.ID = -1;
      Detail.Record.ID = -1;
    } else {
      Rdr.ID = Query.value(8).toInt();
      Rdr.Name = Query.value(9).toString();
      Rdr.CardNumber = Query.value(10).toString();
      Rdr.PhoneNumber = Query.value(11).toString();

      Detail.Record.ID = Query.value(12).toInt();
      Detail.Record.BorrowDate = Query.value(13).toDateTime();
      Detail.Record.DueDate = Query.value(14).toDateTime();
      Detail.Record.ReturnDate = Query.value(15).toDateTime();
      Detail.Record.ReaderId = Rdr.ID;
      Detail.Record.CopyId = Detail.Copy.ID;
    }

    Results.append(std::make_pair(Detail, Rdr));
  }

  return Results;
}

ErrorOr<QSet<QString>>
LibrarySystem::checkExistingBarcodes(const QSet<QString> &Barcodes) {
  QSet<QString> Found;
  if (Barcodes.isEmpty())
    return Found;

  QStringList Placeholders;
  for (int Idx = 0; Idx < Barcodes.size(); ++Idx) {
    Placeholders << "?";
  }

  QString QueryStr =
      QString("SELECT barcode FROM bookcopy WHERE barcode IN (%1)")
          .arg(Placeholders.join(","));

  QSqlQuery Query(DB);
  Query.prepare(QueryStr);

  int Idx = 0;
  for (const auto &Code : Barcodes) {
    Query.bindValue(Idx++, Code);
  }

  if (Query.exec()) {
    while (Query.next()) {
      Found.insert(Query.value(0).toString());
    }
  } else {
    return {ErrorCode::DatabaseError,
            "批量检查条码失败: " + Query.lastError().text()};
  }
  return Found;
}

ErrorOr<QString> LibrarySystem::getNewReaderCardID() {
  QSqlQuery Query(DB);
  if (!Query.exec("SELECT MAX(CAST(card_number AS INTEGER)) FROM reader")) {
    return {ErrorCode::DatabaseError,
            "数据库查询失败: " + Query.lastError().text()};
  }

  int NewID = 1000000;
  if (Query.next()) {
    QVariant Value = Query.value(0);
    if (!Value.isNull()) {
      NewID = Value.toInt() + 1;
    }
  }
  return QString::number(NewID);
}

ErrorOr<QVector<LibrarySystem::ReaderBorrowInfo>>
LibrarySystem::getRemindBorrowings(int Days) {
  QMap<int, ReaderBorrowInfo> ReaderMap;

  QSqlQuery Query(DB);

  // 查询所有未归还的借书记录，按到期日期排序
  QString Sql =
      "SELECT "
      "br.id, br.reader_id, br.borrow_date, br.due_date, "               // br (0-3)
      "bc.id, bc.barcode, bc.info_id, bc.status, "                       // bc (4-7)
      "bi.id, bi.title, bi.author, bi.publisher, bi.cover_path, "        // bi (8-12)
      "r.id, r.name, r.card_number, r.phone "                            // r  (13-16)
      "FROM borrow_record br "
      "JOIN bookcopy bc ON br.copy_id = bc.id "
      "JOIN bookinfo bi ON bc.info_id = bi.id "
      "JOIN reader r ON br.reader_id = r.id "
      "WHERE br.return_date IS NULL "
      "ORDER BY br.due_date ASC";

  if (!Query.exec(Sql)) {
    return {ErrorCode::DatabaseError,
            "查询催还数据失败: " + Query.lastError().text()};
  }

  QDate Today = QDate::currentDate();
  QDate Deadline = Today.addDays(Days);

  while (Query.next()) {
    BorrowDetailType Detail;
    Reader Rdr;

    Detail.Record.ID = Query.value(0).toInt();
    Detail.Record.ReaderId = Query.value(1).toInt();
    Detail.Record.BorrowDate = Query.value(2).toDateTime();
    Detail.Record.DueDate = Query.value(3).toDateTime();

    Detail.Copy.ID = Query.value(4).toInt();
    Detail.Copy.Barcode = Query.value(5).toString();
    Detail.Copy.InfoID = Query.value(6).toInt();
    Detail.Copy.Status = static_cast<BookCopy::BookStatus>(Query.value(7).toInt());
    Detail.Record.CopyId = Detail.Copy.ID;

    Detail.Info.ID = Query.value(8).toInt();
    Detail.Info.Title = Query.value(9).toString();
    Detail.Info.Author = Query.value(10).toString();
    Detail.Info.Publisher = Query.value(11).toString();
    Detail.Info.CoverPath = Query.value(12).toString();

    Rdr.ID = Query.value(13).toInt();
    Rdr.Name = Query.value(14).toString();
    Rdr.CardNumber = Query.value(15).toString();
    Rdr.PhoneNumber = Query.value(16).toString();

    int ReaderId = Rdr.ID;
    if (!ReaderMap.contains(ReaderId)) {
      ReaderBorrowInfo Info;
      Info.reader = Rdr;
      ReaderMap[ReaderId] = Info;
    }

    QDate DueDate = Detail.Record.DueDate.date();
    // 判断是否紧急（到期日期 <= Deadline 即为紧急，包括已逾期）
    if (DueDate <= Deadline) {
      ReaderMap[ReaderId].urgentBooks.append(Detail);
    } else {
      ReaderMap[ReaderId].otherBooks.append(Detail);
    }
  }

  // 只返回有紧急借书的读者
  QVector<ReaderBorrowInfo> Results;
  for (auto &Info : ReaderMap) {
    if (!Info.urgentBooks.isEmpty()) {
      Results.append(Info);
    }
  }

  return Results;
}

ErrorOr<void> LibrarySystem::modifyBookStatusByBarcode(const QString &Barcode, int NewStatus) {
  QSqlQuery Query(DB);

  // 查询书籍当前状态
  Query.prepare("SELECT id, status FROM bookcopy WHERE barcode = :barcode");
  Query.bindValue(":barcode", Barcode);
  if (!Query.exec())
    return {ErrorCode::DatabaseError, "查询失败: " + Query.lastError().text()};
  if (!Query.next())
    return {ErrorCode::NotFound, "条码不存在: " + Barcode};

  int CopyId = Query.value(0).toInt();
  int CurrentStatus = Query.value(1).toInt();

  // 如果状态相同，无需修改
  if (CurrentStatus == NewStatus)
    return {ErrorCode::InvalidStatus, "书籍已处于该状态"};

  // 验证状态转换：只允许从"在馆"或"借出"改为"遗失"
  if (CurrentStatus == BookCopy::BS_Lost) {
    return {ErrorCode::InvalidStatus, "遗失状态的图书无法修改状态"};
  }
  if (NewStatus != BookCopy::BS_Lost) {
    return {ErrorCode::InvalidStatus, "只允许将图书状态修改为'遗失'"};
  }

  if (!DB.transaction())
    return {ErrorCode::DatabaseError, "事务启动失败: " + DB.lastError().text()};

  // 更新书籍状态
  Query.prepare("UPDATE bookcopy SET status = :status WHERE id = :id");
  Query.bindValue(":status", NewStatus);
  Query.bindValue(":id", CopyId);
  if (!Query.exec()) {
    DB.rollback();
    return {ErrorCode::DatabaseError, "更新状态失败: " + Query.lastError().text()};
  }

  // 如果之前是"借出"状态，需要关闭借阅记录
  if (CurrentStatus == BookCopy::BS_Borrowed) {
    Query.prepare(
        "UPDATE borrow_record SET return_date = date('now') "
        "WHERE copy_id = :cid AND return_date IS NULL");
    Query.bindValue(":cid", CopyId);
    if (!Query.exec()) {
      DB.rollback();
      return {ErrorCode::DatabaseError,
              "关闭借阅记录失败: " + Query.lastError().text()};
    }
  }

  if (DB.commit())
    return {};
  DB.rollback();
  return {ErrorCode::DatabaseError, "提交失败: " + DB.lastError().text()};
}
