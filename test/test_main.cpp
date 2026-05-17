#include "../src/TSVParser.h"
#include "../src/Library.h"

#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QtTest>

class LibrarySystemTest : public QObject {
  Q_OBJECT

private slots:
  void initTestCase() {
    auto Res = LibrarySystem::getInstance().init(":memory:");
    QVERIFY2(Res, "数据库初始化失败");
  }

  void testErrorOrLogic() {
    ErrorOr<int> Success(100);
    QVERIFY(Success.getErrCode() == ErrorCode::Success);
    QVERIFY(static_cast<bool>(Success) == true);
    QCOMPARE(Success.getValue(), 100);

    ErrorOr<int> Failure =
        ErrorOr<int>(ErrorCode::DatabaseError, "Error Message");
    QVERIFY(Failure.getErrCode() == ErrorCode::DatabaseError);
    QVERIFY(static_cast<bool>(Failure) == false);
    QCOMPARE(Failure.getErrMsg(), QString("Error Message"));
  }

  void testBorrowBooksRegression() {
    auto Res =
        LibrarySystem::getInstance().getReaderByCardNumber("NON_EXIST_999");
    QVERIFY2(!Res, "不存在的卡号不应返回有效读者");
  }

  void testTSVParserLogic() {
    TSVParser Parser;
    auto Res = Parser.parse("non_existent_file.tsv");
    QVERIFY2(!Res, "解析不存在的文件应该返回错误状态");
  }

  void testTSVCountMismatch() {
    QTemporaryFile File;
    if (File.open()) {
      QTextStream Out(&File);
      Out << "书名\t作者\t出版社\t数量\t图片名\t条码号\n";
      // 册数写 2，但只给 1 行条码，预期触发 ValidationError
      Out << "Test\tAuthor\tPress\t2\timg\tCODE_00\n";
      File.close();
    }

    TSVParser Parser;
    auto Res = Parser.parse(File.fileName());
    QVERIFY(!Res);
    QCOMPARE(Res.getErrCode(), ErrorCode::ValidationError);
    QVERIFY(Res.getErrMsg().contains("不符"));
  }

  void testBorrowTransactionRollback() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    // 准备数据：插入一个读者和两本书
    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");
    Query.exec("INSERT INTO reader (id, name, card_number) VALUES (1, '测试', 'C001')");
    Query.exec("INSERT INTO bookinfo (id, title) VALUES (1, '书A')");
    Query.exec("INSERT INTO bookcopy (id, info_id, barcode, status) VALUES (1, 1, 'BC_01', 0)");

    // 假设 ID 1 存在，ID 99999 不存在
    QVector<int> Ids = {1, 99999};

    auto Res = Lib.borrowBooks(1, Ids);

    QVERIFY(!Res);

    // 验证回滚：ID 为 1 的书状态应该还是 0 (BS_InLibrary)
    Query.exec("SELECT status FROM bookcopy WHERE id = 1");
    if (Query.next()) {
      QCOMPARE(Query.value(0).toInt(), 0);
    }
  }

  void testQueryBooksFuzzySearch() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("INSERT INTO bookinfo (title) VALUES ('C++ Primer')");
    int InfoId = Query.lastInsertId().toInt();
    Query.exec(
        QString(
            "INSERT INTO bookcopy (info_id, barcode) VALUES (%1, 'ABC-123')")
            .arg(InfoId));

    auto Res = Lib.queryBooks("", "Primer", "", "");
    QVERIFY(Res);
    QVERIFY(Res.getValue().size() >= 1);
    QCOMPARE(Res.getValue()[0].first.Info.Title, QString("C++ Primer"));
  }

  void testNormalBorrowProcess() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec(
        "INSERT INTO reader (name, card_number) VALUES ('测试员', 'CARD_001')");
    int ReaderId = Query.lastInsertId().toInt();

    Query.exec("INSERT INTO bookinfo (title) VALUES ('单元测试艺术')");
    int InfoId = Query.lastInsertId().toInt();
    Query.exec(QString("INSERT INTO bookcopy (info_id, barcode, status) VALUES "
                       "(%1, 'BC_001', 0)")
                   .arg(InfoId));
    int CopyId = Query.lastInsertId().toInt();

    auto Res = Lib.borrowBooks(ReaderId, {CopyId});

    QVERIFY2(Res, Res.getErrMsg().toUtf8().constData());

    Query.prepare("SELECT status FROM bookcopy WHERE id = :id");
    Query.bindValue(":id", CopyId);
    Query.exec();
    Query.next();
    QCOMPARE(Query.value(0).toInt(), 1);

    Query.prepare("SELECT COUNT(*) FROM borrow_record WHERE reader_id = :rid "
                  "AND copy_id = :cid");
    Query.bindValue(":rid", ReaderId);
    Query.bindValue(":cid", CopyId);
    Query.exec();
    Query.next();
    QCOMPARE(Query.value(0).toInt(), 1);
  }

  void testNormalReturnProcess() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");
    Query.exec("INSERT INTO reader (id, name, card_number) VALUES (10, "
               "'还书人', 'CARD_10')");
    Query.exec(
        "INSERT INTO bookinfo (id, title) VALUES (10, '深入理解计算机系统')");
    Query.exec("INSERT INTO bookcopy (id, info_id, barcode, status) VALUES "
               "(10, 10, 'BC_10', 1)");
    Query.exec("INSERT INTO borrow_record (id, reader_id, copy_id, "
               "borrow_date, due_date) "
               "VALUES (100, 10, 10, date('now', '-10 days'), date('now', '+20 "
               "days'))");

    auto Res = Lib.returnBooks({100});
    QVERIFY2(Res, Res.getErrMsg().toUtf8().constData());

    Query.exec("SELECT status FROM bookcopy WHERE id = 10");
    Query.next();
    QCOMPARE(Query.value(0).toInt(), 0);

    Query.exec("SELECT return_date FROM borrow_record WHERE id = 100");
    Query.next();
    QString Today = QDate::currentDate().toString("yyyy-MM-dd");
    QCOMPARE(Query.value(0).toString(), Today);
  }

  void testNormalRenewProcess() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");

    Query.exec("INSERT INTO reader (id, name, card_number) VALUES (10, '还书人', 'CARD_10')");
    Query.exec("INSERT INTO bookinfo (id, title) VALUES (10, '深入理解计算机系统')");
    Query.exec("INSERT INTO bookcopy (id, info_id, barcode, status) VALUES (10, 10, 'BC_10', 1)");

    QDate StartDate(2023, 12, 15);
    QString OriginalDueDateStr = StartDate.toString("yyyy-MM-dd");

    Query.exec(
        QString("INSERT INTO borrow_record (id, reader_id, copy_id, due_date) "
                "VALUES (200, 10, 10, '%1')")
            .arg(OriginalDueDateStr));

    auto Res = Lib.renewBooks({200});
    QVERIFY2(Res, Res.getErrMsg().toUtf8().constData());

    Query.exec("SELECT due_date FROM borrow_record WHERE id = 200");
    Query.next();
    QString DBDateStr = Query.value(0).toString();
    QDate ActualDate = QDate::fromString(DBDateStr, "yyyy-MM-dd");

    QDate ExpectedDate = StartDate.addDays(30);
    QCOMPARE(ActualDate, ExpectedDate);
  }

  void testReturnInvalidRecord() {
    auto &Lib = LibrarySystem::getInstance();
    auto Res = Lib.returnBooks({999999});

    QVERIFY(!Res);
    QCOMPARE(Res.getErrCode(), ErrorCode::NotFound);
  }

  void testCannotReturnAlreadyReturnedRecord() {
    // 已归还的记录不应能被再次归还
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");

    Query.exec("INSERT INTO reader (id, name, card_number) VALUES (800, '测试', 'CARD_800')");
    Query.exec("INSERT INTO bookinfo (id, title) VALUES (800, '已还书')");
    Query.exec("INSERT INTO bookcopy (id, info_id, barcode, status) VALUES (800, 800, 'BC_800', 0)");
    // 插入一条已归还的借阅记录
    Query.exec("INSERT INTO borrow_record (id, reader_id, copy_id, "
               "borrow_date, due_date, return_date) "
               "VALUES (8000, 800, 800, date('now', '-20 days'), "
               "date('now', '-10 days'), date('now', '-5 days'))");

    auto Res = Lib.returnBooks({8000});
    QVERIFY2(!Res, "已归还的记录不应能被再次归还");
    QCOMPARE(Res.getErrCode(), ErrorCode::InvalidStatus);
    QVERIFY(Res.getErrMsg().contains("已归还"));
  }

  void testCannotRenewAlreadyReturnedRecord() {
    // 已归还的记录不应能续借
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");

    Query.exec("INSERT INTO reader (id, name, card_number) VALUES (801, '测试', 'CARD_801')");
    Query.exec("INSERT INTO bookinfo (id, title) VALUES (801, '已还书B')");
    Query.exec("INSERT INTO bookcopy (id, info_id, barcode, status) VALUES (801, 801, 'BC_801', 0)");
    Query.exec("INSERT INTO borrow_record (id, reader_id, copy_id, "
               "borrow_date, due_date, return_date) "
               "VALUES (8001, 801, 801, date('now', '-20 days'), "
               "date('now', '-10 days'), date('now', '-5 days'))");

    auto Res = Lib.renewBooks({8001});
    QVERIFY2(!Res, "已归还的记录不应能续借");
    QCOMPARE(Res.getErrCode(), ErrorCode::InvalidStatus);
    QVERIFY(Res.getErrMsg().contains("已归还"));
  }

  void testReturnAndRenewAtomicity() {
    // 测试归还和续借在同一事务中的原子性
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");

    Query.exec("INSERT INTO reader (id, name, card_number) VALUES (900, '测试', 'CARD_900')");
    Query.exec("INSERT INTO bookinfo (id, title) VALUES (900, '原子书A')");
    Query.exec("INSERT INTO bookinfo (id, title) VALUES (901, '原子书B')");
    Query.exec("INSERT INTO bookcopy (id, info_id, barcode, status) VALUES (900, 900, 'BC_900', 1)");
    Query.exec("INSERT INTO bookcopy (id, info_id, barcode, status) VALUES (901, 901, 'BC_901', 1)");
    Query.exec("INSERT INTO borrow_record (id, reader_id, copy_id, "
               "borrow_date, due_date) "
               "VALUES (9000, 900, 900, date('now', '-10 days'), date('now', '+20 days'))");
    Query.exec("INSERT INTO borrow_record (id, reader_id, copy_id, "
               "borrow_date, due_date) "
               "VALUES (9001, 900, 901, date('now', '-10 days'), date('now', '+20 days'))");

    // 归还 9000，续借 9001
    auto Res = Lib.returnAndRenewBooks({9000}, {9001});
    QVERIFY2(Res, Res.getErrMsg().toUtf8().constData());

    // 验证归还成功
    Query.exec("SELECT status FROM bookcopy WHERE id = 900");
    Query.next();
    QCOMPARE(Query.value(0).toInt(), 0); // 在馆

    // 验证续借成功
    Query.exec("SELECT due_date FROM borrow_record WHERE id = 9001");
    Query.next();
    QDate DueDate = QDate::fromString(Query.value(0).toString(), "yyyy-MM-dd");
    QDate ExpectedDue = QDate::currentDate().addDays(50); // 原本+20天，续借+30天=+50天
    QCOMPARE(DueDate, ExpectedDue);
  }

  void testReturnAndRenewRollback() {
    // 测试归还和续借在事务中一个失败时整体回滚
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");

    Query.exec("INSERT INTO reader (id, name, card_number) VALUES (910, '测试', 'CARD_910')");
    Query.exec("INSERT INTO bookinfo (id, title) VALUES (910, '回滚书A')");
    Query.exec("INSERT INTO bookcopy (id, info_id, barcode, status) VALUES (910, 910, 'BC_910', 1)");
    Query.exec("INSERT INTO borrow_record (id, reader_id, copy_id, "
               "borrow_date, due_date) "
               "VALUES (9100, 910, 910, date('now', '-10 days'), date('now', '+20 days'))");

    // 归还 9100（存在），续借 99999（不存在）→ 应整体回滚
    auto Res = Lib.returnAndRenewBooks({9100}, {99999});
    QVERIFY(!Res);

    // 验证归还也被回滚：书籍状态仍为借出
    Query.exec("SELECT status FROM bookcopy WHERE id = 910");
    Query.next();
    QCOMPARE(Query.value(0).toInt(), 1); // 仍为借出

    // 验证 return_date 未被设置
    Query.exec("SELECT return_date FROM borrow_record WHERE id = 9100");
    Query.next();
    QVERIFY(Query.value(0).isNull());
  }

  void testGetRemindBorrowingsBasic() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");

    Query.exec("INSERT INTO reader (id, name, card_number, phone) VALUES "
               "(100, '张三', 'CARD_100', '13800000001')");
    Query.exec("INSERT INTO reader (id, name, card_number, phone) VALUES "
               "(101, '李四', 'CARD_101', '13800000002')");

    Query.exec("INSERT INTO bookinfo (id, title, author, publisher) VALUES "
               "(100, '紧急书A', '作者A', '出版社A')");
    Query.exec("INSERT INTO bookinfo (id, title, author, publisher) VALUES "
               "(101, '普通书B', '作者B', '出版社B')");
    Query.exec("INSERT INTO bookinfo (id, title, author, publisher) VALUES "
               "(102, '李四的书', '作者C', '出版社C')");

    Query.exec("INSERT INTO bookcopy (id, info_id, barcode, status) VALUES "
               "(100, 100, 'BC_100', 1)");
    Query.exec("INSERT INTO bookcopy (id, info_id, barcode, status) VALUES "
               "(101, 101, 'BC_101', 1)");
    Query.exec("INSERT INTO bookcopy (id, info_id, barcode, status) VALUES "
               "(102, 102, 'BC_102', 1)");

    Query.exec("INSERT INTO borrow_record (reader_id, copy_id, "
               "borrow_date, due_date) VALUES "
               "(100, 100, date('now'), date('now', '+3 days'))");
    Query.exec("INSERT INTO borrow_record (reader_id, copy_id, "
               "borrow_date, due_date) VALUES "
               "(100, 101, date('now'), date('now', '+30 days'))");

    Query.exec("INSERT INTO borrow_record (reader_id, copy_id, "
               "borrow_date, due_date) VALUES "
               "(101, 102, date('now'), date('now', '+30 days'))");

    auto Res = Lib.getRemindBorrowings(7);
    QVERIFY2(Res, Res.getErrMsg().toUtf8().constData());

    auto Results = Res.getValue();

    QCOMPARE(Results.size(), 1);
    QCOMPARE(Results[0].reader.Name, QString("张三"));

    QCOMPARE(Results[0].urgentBooks.size(), 1);
    QCOMPARE(Results[0].otherBooks.size(), 1);
    QCOMPARE(Results[0].urgentBooks[0].Info.Title, QString("紧急书A"));
    QCOMPARE(Results[0].otherBooks[0].Info.Title, QString("普通书B"));
  }

  void testGetRemindBorrowingsOverdue() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");

    Query.exec("INSERT INTO reader (id, name, card_number) VALUES "
               "(200, '王五', 'CARD_200')");
    Query.exec("INSERT INTO bookinfo (id, title) VALUES (200, '逾期书')");
    Query.exec("INSERT INTO bookcopy (id, info_id, barcode, status) VALUES "
               "(200, 200, 'BC_200', 1)");
    Query.exec("INSERT INTO borrow_record (reader_id, copy_id, due_date) "
               "VALUES (200, 200, date('now', '-5 days'))");

    auto Res = Lib.getRemindBorrowings(7);
    QVERIFY2(Res, Res.getErrMsg().toUtf8().constData());

    auto Results = Res.getValue();
    QCOMPARE(Results.size(), 1);
    QCOMPARE(Results[0].reader.Name, QString("王五"));
    QCOMPARE(Results[0].urgentBooks.size(), 1);
    QCOMPARE(Results[0].otherBooks.size(), 0);
  }

  void testGetRemindBorrowingsTodayDue() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");

    Query.exec("INSERT INTO reader (id, name, card_number) VALUES "
               "(300, '赵六', 'CARD_300')");
    Query.exec("INSERT INTO bookinfo (id, title) VALUES (300, '今日到期书')");
    Query.exec("INSERT INTO bookcopy (id, info_id, barcode, status) VALUES "
               "(300, 300, 'BC_300', 1)");
    Query.exec("INSERT INTO borrow_record (reader_id, copy_id, due_date) "
               "VALUES (300, 300, date('now'))");

    auto Res = Lib.getRemindBorrowings(0);
    QVERIFY2(Res, Res.getErrMsg().toUtf8().constData());

    auto Results = Res.getValue();
    QCOMPARE(Results.size(), 1);
    QCOMPARE(Results[0].urgentBooks.size(), 1);
  }

  void testGetRemindBorrowingsCopyFieldsPopulated() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");

    Query.exec("INSERT INTO reader (id, name, card_number) VALUES "
               "(400, '测试员', 'CARD_400')");
    Query.exec("INSERT INTO bookinfo (id, title, author, publisher, cover_path) "
               "VALUES (400, '测试书', '测试作者', '测试出版社', 'cover.jpg')");
    Query.exec("INSERT INTO bookcopy (id, info_id, barcode, status) VALUES "
               "(400, 400, 'BC_TEST_400', 1)");
    Query.exec("INSERT INTO borrow_record (reader_id, copy_id, due_date) "
               "VALUES (400, 400, date('now', '+2 days'))");

    auto Res = Lib.getRemindBorrowings(7);
    QVERIFY2(Res, Res.getErrMsg().toUtf8().constData());

    auto Results = Res.getValue();
    QCOMPARE(Results.size(), 1);

    const auto &Copy = Results[0].urgentBooks[0].Copy;
    QCOMPARE(Copy.ID, 400);
    QCOMPARE(Copy.Barcode, QString("BC_TEST_400"));
    QCOMPARE(Copy.InfoID, 400);
    QCOMPARE(Copy.Status, BookCopy::BS_Borrowed);

    const auto &Info = Results[0].urgentBooks[0].Info;
    QCOMPARE(Info.Title, QString("测试书"));
    QCOMPARE(Info.Author, QString("测试作者"));
    QCOMPARE(Info.Publisher, QString("测试出版社"));
    QCOMPARE(Info.CoverPath, QString("cover.jpg"));
  }

  void testGetReaderByCardNumberReturnsInactiveStatus() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");

    // 插入活跃读者
    Query.exec("INSERT INTO reader (name, card_number, is_inactive) VALUES "
               "('活跃读者', 'CARD_ACTIVE', 0)");
    // 插入注销读者
    Query.exec("INSERT INTO reader (name, card_number, is_inactive) VALUES "
               "('注销读者', 'CARD_INACTIVE', 1)");

    auto ActiveRes = Lib.getReaderByCardNumber("CARD_ACTIVE");
    QVERIFY(ActiveRes);
    QCOMPARE(ActiveRes.getValue().IsInactive, false);

    auto InactiveRes = Lib.getReaderByCardNumber("CARD_INACTIVE");
    QVERIFY(InactiveRes);
    QCOMPARE(InactiveRes.getValue().IsInactive, true);
  }

  // ===== 遗失书籍操作拦截测试 =====

  void testCannotBorrowFromInactiveReader() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");

    Query.exec("INSERT INTO reader (id, name, card_number, is_inactive) VALUES "
               "(700, '注销读者', 'CARD_700', 1)");
    Query.exec("INSERT INTO bookinfo (id, title) VALUES (700, '测试书')");
    Query.exec(QString("INSERT INTO bookcopy (id, info_id, barcode, status) "
                        "VALUES (700, 700, 'BC_700', %1)")
                   .arg(BookCopy::BS_InLibrary));

    auto Res = Lib.borrowBooks(700, {700});
    QVERIFY2(!Res, "已注销读者不应该能借书");
    QCOMPARE(Res.getErrCode(), ErrorCode::InvalidStatus);
    QVERIFY(Res.getErrMsg().contains("注销"));

    Query.exec("SELECT status FROM bookcopy WHERE id = 700");
    Query.next();
    QCOMPARE(Query.value(0).toInt(), static_cast<int>(BookCopy::BS_InLibrary));
  }

  void testCannotBorrowLostBook() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");

    Query.exec("INSERT INTO reader (id, name, card_number) VALUES "
               "(500, '借书员', 'CARD_500')");
    Query.exec("INSERT INTO bookinfo (id, title) VALUES (500, '遗失的书')");
    Query.exec(QString("INSERT INTO bookcopy (id, info_id, barcode, status) "
                        "VALUES (500, 500, 'BC_500', %1)")
                   .arg(BookCopy::BS_Lost));

    auto Res = Lib.borrowBooks(500, {500});
    QVERIFY2(!Res, "遗失的书籍不应该能借出");
    QCOMPARE(Res.getErrCode(), ErrorCode::InvalidStatus);
    QVERIFY(Res.getErrMsg().contains("遗失"));

    Query.exec("SELECT status FROM bookcopy WHERE id = 500");
    Query.next();
    QCOMPARE(Query.value(0).toInt(), static_cast<int>(BookCopy::BS_Lost));
  }

  void testCannotReturnLostBook() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");

    Query.exec("INSERT INTO reader (id, name, card_number) VALUES "
               "(501, '还书员', 'CARD_501')");
    Query.exec("INSERT INTO bookinfo (id, title) VALUES (501, '遗失的书B')");
    Query.exec(QString("INSERT INTO bookcopy (id, info_id, barcode, status) "
                        "VALUES (501, 501, 'BC_501', %1)")
                   .arg(BookCopy::BS_Lost));
    Query.exec("INSERT INTO borrow_record (id, reader_id, copy_id, "
               "borrow_date, due_date) "
               "VALUES (5001, 501, 501, date('now', '-10 days'), date('now', "
               "'-5 days'))");

    auto Res = Lib.returnBooks({5001});
    QVERIFY2(!Res, "遗失的书籍不应该能直接归还");
    QCOMPARE(Res.getErrCode(), ErrorCode::InvalidStatus);
    QVERIFY(Res.getErrMsg().contains("遗失"));

    Query.exec("SELECT status FROM bookcopy WHERE id = 501");
    Query.next();
    QCOMPARE(Query.value(0).toInt(), static_cast<int>(BookCopy::BS_Lost));

    Query.exec("SELECT return_date FROM borrow_record WHERE id = 5001");
    Query.next();
    QVERIFY(Query.value(0).isNull());
  }

  void testCannotRenewLostBook() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");

    Query.exec("INSERT INTO reader (id, name, card_number) VALUES "
               "(502, '续借员', 'CARD_502')");
    Query.exec("INSERT INTO bookinfo (id, title) VALUES (502, '遗失的书C')");
    Query.exec(QString("INSERT INTO bookcopy (id, info_id, barcode, status) "
                        "VALUES (502, 502, 'BC_502', %1)")
                   .arg(BookCopy::BS_Lost));

    QString OriginalDueDate("2025-01-10");
    Query.exec(QString("INSERT INTO borrow_record (id, reader_id, copy_id, "
                       "due_date) VALUES (5002, 502, 502, '%1')")
                   .arg(OriginalDueDate));

    auto Res = Lib.renewBooks({5002});
    QVERIFY2(!Res, "遗失的书籍不应该能续借");
    QCOMPARE(Res.getErrCode(), ErrorCode::InvalidStatus);
    QVERIFY(Res.getErrMsg().contains("遗失"));

    Query.exec("SELECT due_date FROM borrow_record WHERE id = 5002");
    Query.next();
    QCOMPARE(Query.value(0).toString(), OriginalDueDate);
  }

  // ===== modifyBookStatusByBarcode 测试 =====

  void testMarkInLibraryBookAsLost() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");

    Query.exec("INSERT INTO bookinfo (id, title) VALUES (600, '待遗失书')");
    Query.exec(QString("INSERT INTO bookcopy (id, info_id, barcode, status) "
                        "VALUES (600, 600, 'BC_LOST_01', %1)")
                   .arg(BookCopy::BS_InLibrary));

    auto Res = Lib.modifyBookStatusByBarcode(
        "BC_LOST_01", BookCopy::BS_Lost);
    QVERIFY2(Res, Res.getErrMsg().toUtf8().constData());

    Query.exec("SELECT status FROM bookcopy WHERE barcode = 'BC_LOST_01'");
    Query.next();
    QCOMPARE(Query.value(0).toInt(), static_cast<int>(BookCopy::BS_Lost));
  }

  void testMarkBorrowedBookAsLost() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");

    Query.exec("INSERT INTO reader (id, name, card_number) VALUES "
               "(600, '读者', 'CARD_600')");
    Query.exec("INSERT INTO bookinfo (id, title) VALUES (601, '借出中遗失书')");
    Query.exec(QString("INSERT INTO bookcopy (id, info_id, barcode, status) "
                        "VALUES (601, 601, 'BC_LOST_02', %1)")
                   .arg(BookCopy::BS_Borrowed));
    Query.exec("INSERT INTO borrow_record (reader_id, copy_id, borrow_date, "
               "due_date) "
               "VALUES (600, 601, date('now', '-5 days'), date('now', '+25 "
               "days'))");

    auto Res = Lib.modifyBookStatusByBarcode(
        "BC_LOST_02", BookCopy::BS_Lost);
    QVERIFY2(Res, Res.getErrMsg().toUtf8().constData());

    Query.exec("SELECT status FROM bookcopy WHERE barcode = 'BC_LOST_02'");
    Query.next();
    QCOMPARE(Query.value(0).toInt(), static_cast<int>(BookCopy::BS_Lost));

    Query.exec("SELECT return_date FROM borrow_record WHERE copy_id = 601");
    Query.next();
    QString Today = QDate::currentDate().toString("yyyy-MM-dd");
    QCOMPARE(Query.value(0).toString(), Today);
  }

  void testModifyStatusNonExistentBarcode() {
    auto &Lib = LibrarySystem::getInstance();
    auto Res = Lib.modifyBookStatusByBarcode(
        "NOT_EXIST_999", BookCopy::BS_Lost);
    QVERIFY(!Res);
    QCOMPARE(Res.getErrCode(), ErrorCode::NotFound);
    QVERIFY(Res.getErrMsg().contains("不存在"));
  }

  void testModifyStatusAlreadyLost() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("INSERT INTO bookinfo (id, title) VALUES (602, '已遗失书')");
    Query.exec(QString("INSERT INTO bookcopy (id, info_id, barcode, status) "
                        "VALUES (602, 602, 'BC_LOST_03', %1)")
                   .arg(BookCopy::BS_Lost));

    auto Res = Lib.modifyBookStatusByBarcode(
        "BC_LOST_03", BookCopy::BS_Lost);
    QVERIFY(!Res);
    QCOMPARE(Res.getErrCode(), ErrorCode::InvalidStatus);
    QVERIFY(Res.getErrMsg().contains("已处于该状态"));
  }

  void testModifyStatusToNonLost() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("INSERT INTO bookinfo (id, title) VALUES (603, '在馆书')");
    Query.exec(QString("INSERT INTO bookcopy (id, info_id, barcode, status) "
                        "VALUES (603, 603, 'BC_LOST_04', %1)")
                   .arg(BookCopy::BS_InLibrary));

    auto Res = Lib.modifyBookStatusByBarcode(
        "BC_LOST_04", BookCopy::BS_Borrowed);
    QVERIFY(!Res);
    QCOMPARE(Res.getErrCode(), ErrorCode::InvalidStatus);
    QVERIFY(Res.getErrMsg().contains("不支持"));
  }

  void testModifyLostBookStatus() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("INSERT INTO bookinfo (id, title) VALUES (604, '遗失书')");
    Query.exec(QString("INSERT INTO bookcopy (id, info_id, barcode, status) "
                        "VALUES (604, 604, 'BC_LOST_05', %1)")
                   .arg(BookCopy::BS_Lost));

    // 现在允许从"遗失"恢复为"在馆"
    auto Res = Lib.modifyBookStatusByBarcode(
        "BC_LOST_05", BookCopy::BS_InLibrary);
    QVERIFY2(Res, Res.getErrMsg().toUtf8().constData());

    Query.exec("SELECT status FROM bookcopy WHERE barcode = 'BC_LOST_05'");
    Query.next();
    QCOMPARE(Query.value(0).toInt(), static_cast<int>(BookCopy::BS_InLibrary));
  }

  // ===== 非外借书状态测试 =====

  void testMarkInLibraryBookAsNonLendable() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");

    Query.exec("INSERT INTO bookinfo (id, title) VALUES (610, '非外借候选书')");
    Query.exec(QString("INSERT INTO bookcopy (id, info_id, barcode, status) "
                       "VALUES (610, 610, 'BC_NL_01', %1)")
                   .arg(BookCopy::BS_InLibrary));

    auto Res = Lib.modifyBookStatusByBarcode(
        "BC_NL_01", BookCopy::BS_NonLendable);
    QVERIFY2(Res, Res.getErrMsg().toUtf8().constData());

    Query.exec("SELECT status FROM bookcopy WHERE barcode = 'BC_NL_01'");
    Query.next();
    QCOMPARE(Query.value(0).toInt(), static_cast<int>(BookCopy::BS_NonLendable));
  }

  void testMarkBorrowedBookAsNonLendable() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");

    Query.exec("INSERT INTO reader (id, name, card_number) VALUES "
               "(610, '读者', 'CARD_610')");
    Query.exec("INSERT INTO bookinfo (id, title) VALUES (611, '借出中非外借书')");
    Query.exec(QString("INSERT INTO bookcopy (id, info_id, barcode, status) "
                       "VALUES (611, 611, 'BC_NL_02', %1)")
                   .arg(BookCopy::BS_Borrowed));
    Query.exec("INSERT INTO borrow_record (reader_id, copy_id, borrow_date, "
               "due_date) "
               "VALUES (610, 611, date('now', '-5 days'), date('now', '+25 "
               "days'))");

    auto Res = Lib.modifyBookStatusByBarcode(
        "BC_NL_02", BookCopy::BS_NonLendable);
    QVERIFY2(Res, Res.getErrMsg().toUtf8().constData());

    Query.exec("SELECT status FROM bookcopy WHERE barcode = 'BC_NL_02'");
    Query.next();
    QCOMPARE(Query.value(0).toInt(), static_cast<int>(BookCopy::BS_NonLendable));

    // 验证借阅记录已关闭
    Query.exec("SELECT return_date FROM borrow_record WHERE copy_id = 611");
    Query.next();
    QString Today = QDate::currentDate().toString("yyyy-MM-dd");
    QCOMPARE(Query.value(0).toString(), Today);
  }

  void testRevertNonLendableBookToInLibrary() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("INSERT INTO bookinfo (id, title) VALUES (612, '非外借恢复书')");
    Query.exec(QString("INSERT INTO bookcopy (id, info_id, barcode, status) "
                       "VALUES (612, 612, 'BC_NL_03', %1)")
                   .arg(BookCopy::BS_NonLendable));

    auto Res = Lib.modifyBookStatusByBarcode(
        "BC_NL_03", BookCopy::BS_InLibrary);
    QVERIFY2(Res, Res.getErrMsg().toUtf8().constData());

    Query.exec("SELECT status FROM bookcopy WHERE barcode = 'BC_NL_03'");
    Query.next();
    QCOMPARE(Query.value(0).toInt(), static_cast<int>(BookCopy::BS_InLibrary));
  }

  void testCannotBorrowNonLendableBook() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");

    Query.exec("INSERT INTO reader (id, name, card_number) VALUES "
               "(613, '借书员', 'CARD_613')");
    Query.exec("INSERT INTO bookinfo (id, title) VALUES (613, '非外借的书')");
    Query.exec(QString("INSERT INTO bookcopy (id, info_id, barcode, status) "
                       "VALUES (613, 613, 'BC_NL_04', %1)")
                   .arg(BookCopy::BS_NonLendable));

    auto Res = Lib.borrowBooks(613, {613});
    QVERIFY2(!Res, "非外借书不应该能借出");
    QCOMPARE(Res.getErrCode(), ErrorCode::InvalidStatus);
    QVERIFY(Res.getErrMsg().contains("非外借书"));

    Query.exec("SELECT status FROM bookcopy WHERE id = 613");
    Query.next();
    QCOMPARE(Query.value(0).toInt(), static_cast<int>(BookCopy::BS_NonLendable));
  }

  void testCannotReturnNonLendableBook() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");

    Query.exec("INSERT INTO reader (id, name, card_number) VALUES "
               "(614, '还书员', 'CARD_614')");
    Query.exec("INSERT INTO bookinfo (id, title) VALUES (614, '非外借书B')");
    Query.exec(QString("INSERT INTO bookcopy (id, info_id, barcode, status) "
                       "VALUES (614, 614, 'BC_NL_05', %1)")
                   .arg(BookCopy::BS_NonLendable));
    Query.exec("INSERT INTO borrow_record (id, reader_id, copy_id, "
               "borrow_date, due_date) "
               "VALUES (6014, 614, 614, date('now', '-10 days'), date('now', "
               "'-5 days'))");

    auto Res = Lib.returnBooks({6014});
    QVERIFY2(!Res, "非外借书不应该能直接归还");
    QCOMPARE(Res.getErrCode(), ErrorCode::InvalidStatus);
    QVERIFY(Res.getErrMsg().contains("非外借书"));
  }

  void testCannotRenewNonLendableBook() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");

    Query.exec("INSERT INTO reader (id, name, card_number) VALUES "
               "(615, '续借员', 'CARD_615')");
    Query.exec("INSERT INTO bookinfo (id, title) VALUES (615, '非外借书C')");
    Query.exec(QString("INSERT INTO bookcopy (id, info_id, barcode, status) "
                       "VALUES (615, 615, 'BC_NL_06', %1)")
                   .arg(BookCopy::BS_NonLendable));

    QString OriginalDueDate("2025-01-10");
    Query.exec(QString("INSERT INTO borrow_record (id, reader_id, copy_id, "
                       "due_date) VALUES (6015, 615, 615, '%1')")
                   .arg(OriginalDueDate));

    auto Res = Lib.renewBooks({6015});
    QVERIFY2(!Res, "非外借书不应该能续借");
    QCOMPARE(Res.getErrCode(), ErrorCode::InvalidStatus);
    QVERIFY(Res.getErrMsg().contains("非外借书"));
  }

  void testModifyStatusInvalidTransition() {
    auto &Lib = LibrarySystem::getInstance();
    QSqlDatabase DB = QSqlDatabase::database();
    QSqlQuery Query(DB);

    Query.exec("INSERT INTO bookinfo (id, title) VALUES (616, '测试书')");
    Query.exec(QString("INSERT INTO bookcopy (id, info_id, barcode, status) "
                       "VALUES (616, 616, 'BC_NL_07', %1)")
                   .arg(BookCopy::BS_NonLendable));

    // 不能从非外借书直接改为遗失（应先恢复为在馆）
    auto Res = Lib.modifyBookStatusByBarcode(
        "BC_NL_07", BookCopy::BS_Lost);
    QVERIFY(!Res);
    QCOMPARE(Res.getErrCode(), ErrorCode::InvalidStatus);
    QVERIFY(Res.getErrMsg().contains("不支持"));
  }
};

QTEST_GUILESS_MAIN(LibrarySystemTest)
#include "test_main.moc"
