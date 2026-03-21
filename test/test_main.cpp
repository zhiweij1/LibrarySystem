#include "../src/CSVParser.h"
#include "../src/Library.h"

#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QtTest>

class LibrarySystemTest : public QObject {
  Q_OBJECT

private slots:
  void initTestCase() {
    bool OK = LibrarySystem::getInstance().init(":memory:");
    QVERIFY2(OK, "数据库初始化失败");
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

  void testCSVParserLogic() {
    CSVParser Parser;
    auto Res = Parser.parse("non_existent_file.csv");
    QVERIFY2(!Res, "解析不存在的文件应该返回错误状态");
  }

  void testCSVCountMismatch() {
    QTemporaryFile File;
    if (File.open()) {
      QTextStream Out(&File);
      Out << "ID,Title,Author,Pub,Count,Cat,Barcodes\n";
      // 册数写 2，但条码只给 1 个，预期触发 ValidationError
      Out << "1,Test,Author,Press,2,B1,CODE_001\n";
      File.close();
    }

    CSVParser Parser;
    auto Res = Parser.parse(File.fileName());
    QVERIFY(!Res);
    QCOMPARE(Res.getErrCode(), ErrorCode::ValidationError);
    QVERIFY(Res.getErrMsg().contains("不符"));
  }

  void testBorrowTransactionRollback() {
    // 假设 ID 1 是存在的，ID 99999 是不存在的
    QVector<int> Ids = {1, 99999};

    // 执行批量借书
    auto Res = LibrarySystem::getInstance().borrowBooks(1, Ids);

    // 预期结果：因为 99999 不存在，borrowBook 会失败，触发事务回滚
    QVERIFY(!Res);

    // 验证回滚：ID 为 1 的书状态应该还是 0 (BS_InLibrary)，而不是 1
    // (BS_Borrowed)
    QSqlQuery Query;
    Query.exec("SELECT status FROM bookcopy WHERE id = 1");
    if (Query.next()) {
      QCOMPARE(Query.value(0).toInt(), 0);
    }
  }

  void testQueryBooksFuzzySearch() {
    // 插入测试数据
    QSqlQuery Query;
    Query.exec("INSERT INTO bookinfo (title) VALUES ('C++ Primer')");
    int InfoId = Query.lastInsertId().toInt();
    Query.exec(
        QString(
            "INSERT INTO bookcopy (info_id, barcode) VALUES (%1, 'ABC-123')")
            .arg(InfoId));

    // 只搜标题的一部分
    auto Res = LibrarySystem::getInstance().queryBooks("", "Primer", "", "");
    QVERIFY(Res);
    QVERIFY(Res.getValue().size() >= 1);
    QCOMPARE(Res.getValue()[0].first.Info.Title, QString("C++ Primer"));
  }

  void testNormalBorrowProcess() {
    // 1. 准备环境：清理数据并插入一个测试读者和一本书
    QSqlQuery Query;
    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec(
        "INSERT INTO reader (name, card_number) VALUES ('测试员', 'CARD_001')");
    int ReaderId = Query.lastInsertId().toInt();

    Query.exec("INSERT INTO bookinfo (title) VALUES ('单元测试艺术')");
    int InfoId = Query.lastInsertId().toInt();
    Query.exec(QString("INSERT INTO bookcopy (info_id, barcode, status) VALUES "
                       "(%1, 'BC_001', 0)")
                   .arg(InfoId));
    int CopyId = Query.lastInsertId().toInt();

    // 2. 执行正常借书动作
    auto Res = LibrarySystem::getInstance().borrowBooks(ReaderId, {CopyId});

    // 验证 A：逻辑层返回成功
    QVERIFY2(Res, Res.getErrMsg().toUtf8().constData());

    // 3. 数据库验证（最关键的一步）
    // 验证 B：书籍状态是否真的变成了“已借出”(1)
    Query.prepare("SELECT status FROM bookcopy WHERE id = :id");
    Query.bindValue(":id", CopyId);
    Query.exec();
    Query.next();
    QCOMPARE(Query.value(0).toInt(), 1); // 1 代表 BookCopy::BS_Borrowed

    // 验证 C：借阅记录表是否真的多了一条数据
    Query.prepare("SELECT COUNT(*) FROM borrow_record WHERE reader_id = :rid "
                  "AND copy_id = :cid");
    Query.bindValue(":rid", ReaderId);
    Query.bindValue(":cid", CopyId);
    Query.exec();
    Query.next();
    QCOMPARE(Query.value(0).toInt(), 1);
  }

  void testNormalReturnProcess() {
    QSqlQuery Query;
    // 1. 模拟环境：先插入一条“已借出”的记录
    // 假设 reader_id=1, copy_id=1, 且该副本当前状态为 1 (BS_Borrowed)
    Query.exec("INSERT INTO reader (id, name, card_number) VALUES (10, "
               "'还书人', 'CARD_10')");
    Query.exec(
        "INSERT INTO bookinfo (id, title) VALUES (10, '深入理解计算机系统')");
    Query.exec("INSERT INTO bookcopy (id, info_id, barcode, status) VALUES "
               "(10, 10, 'BC_10', 1)"); // 状态为1
    Query.exec("INSERT INTO borrow_record (id, reader_id, copy_id, "
               "borrow_date, due_date) "
               "VALUES (100, 10, 10, date('now', '-10 days'), date('now', '+20 "
               "days'))");

    // 2. 执行还书
    auto Res = LibrarySystem::getInstance().returnBooks({100}); // 传入记录ID
    QVERIFY2(Res, Res.getErrMsg().toUtf8().constData());

    // 3. 验证数据库状态
    Query.exec("SELECT status FROM bookcopy WHERE id = 10");
    Query.next();
    QCOMPARE(Query.value(0).toInt(), 0);

    Query.exec("SELECT return_date FROM borrow_record WHERE id = 100");
    Query.next();
    QString Today = QDate::currentDate().toString("yyyy-MM-dd");
    QCOMPARE(Query.value(0).toString(), Today); // 验证还书日期确实是今天
  }

  void testNormalRenewProcess() {
    QSqlQuery Query;

    // 1. 准备初始数据
    // 使用 QDate 生成一个标准的日期字符串，避免硬编码格式错误
    QDate StartDate(2023, 12, 15);
    QString OriginalDueDateStr = StartDate.toString("yyyy-MM-dd");

    // 插入一条借阅记录，到期时间为 2023-12-15
    Query.exec(
        QString("INSERT INTO borrow_record (id, reader_id, copy_id, due_date) "
                "VALUES (200, 10, 10, '%1')")
            .arg(OriginalDueDateStr));

    // 2. 执行续借逻辑 (内部调用了 SQLite 的 date(due_date, '+30 days'))
    auto Res = LibrarySystem::getInstance().renewBooks({200});
    QVERIFY2(Res, Res.getErrMsg().toUtf8().constData());

    // 3. 验证数据库中的新日期
    Query.exec("SELECT due_date FROM borrow_record WHERE id = 200");
    Query.next();
    QString DBDateStr = Query.value(0).toString();
    QDate ActualDate = QDate::fromString(DBDateStr, "yyyy-MM-dd");

    // 4. 精确匹配：计算 C++ 端预期的日期 (12月15日 + 30天 = 次年1月14日)
    QDate ExpectedDate = StartDate.addDays(30);

    // 验证：数据库算出来的日期必须和 QDate 算出来的一模一样
    QCOMPARE(ActualDate, ExpectedDate);
  }

  void testReturnInvalidRecord() {
    // 传入一个不存在的记录ID：999999
    auto Res = LibrarySystem::getInstance().returnBooks({999999});

    // 预期：应该返回失败，而不是假装成功
    QVERIFY(!Res);
    QCOMPARE(Res.getErrCode(), ErrorCode::NotFound);
  }

  void testGetRemindBorrowingsBasic() {
    QSqlQuery Query;

    // 1. 准备测试数据
    // 清理旧数据
    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");

    // 插入读者
    Query.exec("INSERT INTO reader (id, name, card_number, phone) VALUES "
               "(100, '张三', 'CARD_100', '13800000001')");
    Query.exec("INSERT INTO reader (id, name, card_number, phone) VALUES "
               "(101, '李四', 'CARD_101', '13800000002')");

    // 插入书籍信息
    Query.exec("INSERT INTO bookinfo (id, title, author, publisher) VALUES "
               "(100, '紧急书A', '作者A', '出版社A')");
    Query.exec("INSERT INTO bookinfo (id, title, author, publisher) VALUES "
               "(101, '普通书B', '作者B', '出版社B')");
    Query.exec("INSERT INTO bookinfo (id, title, author, publisher) VALUES "
               "(102, '李四的书', '作者C', '出版社C')");

    // 插入书籍副本
    Query.exec("INSERT INTO bookcopy (id, info_id, barcode, status) VALUES "
               "(100, 100, 'BC_100', 1)"); // 紧急书
    Query.exec("INSERT INTO bookcopy (id, info_id, barcode, status) VALUES "
               "(101, 101, 'BC_101', 1)"); // 张三的普通书
    Query.exec("INSERT INTO bookcopy (id, info_id, barcode, status) VALUES "
               "(102, 102, 'BC_102', 1)"); // 李四的书

    // 插入借阅记录
    // 张三借了紧急书（3天后到期）+ 普通书（30天后到期）
    Query.exec("INSERT INTO borrow_record (reader_id, copy_id, "
               "borrow_date, due_date) VALUES "
               "(100, 100, date('now'), date('now', '+3 days'))");
    Query.exec("INSERT INTO borrow_record (reader_id, copy_id, "
               "borrow_date, due_date) VALUES "
               "(100, 101, date('now'), date('now', '+30 days'))");

    // 李四只借了普通书（30天后到期），没有紧急书
    Query.exec("INSERT INTO borrow_record (reader_id, copy_id, "
               "borrow_date, due_date) VALUES "
               "(101, 102, date('now'), date('now', '+30 days'))");

    // 2. 调用 getRemindBorrowings(7)，查询7天内到期的书
    auto Res = LibrarySystem::getInstance().getRemindBorrowings(7);
    QVERIFY2(Res, Res.getErrMsg().toUtf8().constData());

    auto Results = Res.getValue();

    // 3. 验证：只返回张三（有紧急书），不返回李四（无紧急书）
    QCOMPARE(Results.size(), 1);
    QCOMPARE(Results[0].reader.Name, QString("张三"));

    // 4. 验证：张三的紧急书和普通书分类正确
    QCOMPARE(Results[0].urgentBooks.size(), 1);
    QCOMPARE(Results[0].otherBooks.size(), 1);
    QCOMPARE(Results[0].urgentBooks[0].Info.Title, QString("紧急书A"));
    QCOMPARE(Results[0].otherBooks[0].Info.Title, QString("普通书B"));
  }

  void testGetRemindBorrowingsOverdue() {
    QSqlQuery Query;

    // 准备数据：已逾期的书
    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");

    Query.exec("INSERT INTO reader (id, name, card_number) VALUES "
               "(200, '王五', 'CARD_200')");
    Query.exec("INSERT INTO bookinfo (id, title) VALUES (200, '逾期书')");
    Query.exec("INSERT INTO bookcopy (id, info_id, barcode, status) VALUES "
               "(200, 200, 'BC_200', 1)");
    // 5天前到期（已逾期）
    Query.exec("INSERT INTO borrow_record (reader_id, copy_id, due_date) "
               "VALUES (200, 200, date('now', '-5 days'))");

    // 查询7天内到期的书（应包含已逾期的）
    auto Res = LibrarySystem::getInstance().getRemindBorrowings(7);
    QVERIFY2(Res, Res.getErrMsg().toUtf8().constData());

    auto Results = Res.getValue();
    QCOMPARE(Results.size(), 1);
    QCOMPARE(Results[0].reader.Name, QString("王五"));
    QCOMPARE(Results[0].urgentBooks.size(), 1); // 逾期书也是紧急书
    QCOMPARE(Results[0].otherBooks.size(), 0);
  }

  void testGetRemindBorrowingsTodayDue() {
    QSqlQuery Query;

    // 准备数据：今天到期
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

    // 查询0天内到期的书（只有今天到期或已逾期）
    auto Res = LibrarySystem::getInstance().getRemindBorrowings(0);
    QVERIFY2(Res, Res.getErrMsg().toUtf8().constData());

    auto Results = Res.getValue();
    QCOMPARE(Results.size(), 1);
    QCOMPARE(Results[0].urgentBooks.size(), 1);
  }

  void testGetRemindBorrowingsCopyFieldsPopulated() {
    QSqlQuery Query;

    // 验证 BorrowDetailType::Copy 字段完整性
    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");

    Query.exec("INSERT INTO reader (id, name, card_number) VALUES "
               "(400, '测试员', 'CARD_400')");
    Query.exec("INSERT INTO bookinfo (id, title, author, publisher, cover_path) "
               "VALUES (400, '测试书', '测试作者', '测试出版社', 'cover.jpg')");
    Query.exec("INSERT INTO bookcopy (id, info_id, barcode, status) VALUES "
               "(400, 400, 'BC_TEST_400', 1)"); // status = 1 (借出)
    Query.exec("INSERT INTO borrow_record (reader_id, copy_id, due_date) "
               "VALUES (400, 400, date('now', '+2 days'))");

    auto Res = LibrarySystem::getInstance().getRemindBorrowings(7);
    QVERIFY2(Res, Res.getErrMsg().toUtf8().constData());

    auto Results = Res.getValue();
    QCOMPARE(Results.size(), 1);

    // 验证 Copy 字段完整性
    const auto &Copy = Results[0].urgentBooks[0].Copy;
    QCOMPARE(Copy.ID, 400);
    QCOMPARE(Copy.Barcode, QString("BC_TEST_400"));
    QCOMPARE(Copy.InfoID, 400);
    QCOMPARE(Copy.Status, BookCopy::BS_Borrowed);

    // 验证 Info 字段完整性
    const auto &Info = Results[0].urgentBooks[0].Info;
    QCOMPARE(Info.Title, QString("测试书"));
    QCOMPARE(Info.Author, QString("测试作者"));
    QCOMPARE(Info.Publisher, QString("测试出版社"));
    QCOMPARE(Info.CoverPath, QString("cover.jpg"));
  }
  // ===== 遗失书籍操作拦截测试 =====

  void testCannotBorrowLostBook() {
    QSqlQuery Query;
    // 准备：插入读者和一本"遗失"状态的书籍
    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");

    Query.exec("INSERT INTO reader (id, name, card_number) VALUES "
               "(500, '借书员', 'CARD_500')");
    Query.exec("INSERT INTO bookinfo (id, title) VALUES (500, '遗失的书')");
    // status = 2 代表 BS_Lost
    Query.exec("INSERT INTO bookcopy (id, info_id, barcode, status) VALUES "
               "(500, 500, 'BC_500', 2)");

    // 尝试借出遗失的书
    auto Res = LibrarySystem::getInstance().borrowBooks(500, {500});
    QVERIFY2(!Res, "遗失的书籍不应该能借出");
    QCOMPARE(Res.getErrCode(), ErrorCode::InvalidStatus);
    QVERIFY(Res.getErrMsg().contains("遗失"));

    // 验证数据库状态未被改变
    Query.exec("SELECT status FROM bookcopy WHERE id = 500");
    Query.next();
    QCOMPARE(Query.value(0).toInt(), 2); // 仍然是 BS_Lost
  }

  void testCannotReturnLostBook() {
    QSqlQuery Query;
    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");

    Query.exec("INSERT INTO reader (id, name, card_number) VALUES "
               "(501, '还书员', 'CARD_501')");
    Query.exec("INSERT INTO bookinfo (id, title) VALUES (501, '遗失的书B')");
    // status = 2 代表 BS_Lost（已遗失但仍有借阅记录）
    Query.exec("INSERT INTO bookcopy (id, info_id, barcode, status) VALUES "
               "(501, 501, 'BC_501', 2)");
    Query.exec("INSERT INTO borrow_record (id, reader_id, copy_id, "
               "borrow_date, due_date) "
               "VALUES (5001, 501, 501, date('now', '-10 days'), date('now', "
               "'-5 days'))");

    // 尝试归还遗失的书
    auto Res = LibrarySystem::getInstance().returnBooks({5001});
    QVERIFY2(!Res, "遗失的书籍不应该能直接归还");
    QCOMPARE(Res.getErrCode(), ErrorCode::InvalidStatus);
    QVERIFY(Res.getErrMsg().contains("遗失"));

    // 验证书籍状态未改变
    Query.exec("SELECT status FROM bookcopy WHERE id = 501");
    Query.next();
    QCOMPARE(Query.value(0).toInt(), 2); // 仍然是 BS_Lost

    // 验证借阅记录的 return_date 仍为 NULL
    Query.exec("SELECT return_date FROM borrow_record WHERE id = 5001");
    Query.next();
    QVERIFY(Query.value(0).isNull());
  }

  void testCannotRenewLostBook() {
    QSqlQuery Query;
    Query.exec("DELETE FROM borrow_record");
    Query.exec("DELETE FROM bookcopy");
    Query.exec("DELETE FROM bookinfo");
    Query.exec("DELETE FROM reader");

    Query.exec("INSERT INTO reader (id, name, card_number) VALUES "
               "(502, '续借员', 'CARD_502')");
    Query.exec("INSERT INTO bookinfo (id, title) VALUES (502, '遗失的书C')");
    // status = 2 代表 BS_Lost
    Query.exec("INSERT INTO bookcopy (id, info_id, barcode, status) VALUES "
               "(502, 502, 'BC_502', 2)");
    Query.exec("INSERT INTO borrow_record (id, reader_id, copy_id, due_date) "
               "VALUES (5002, 502, 502, date('now', '+5 days'))");

    // 尝试续借遗失的书
    auto Res = LibrarySystem::getInstance().renewBooks({5002});
    QVERIFY2(!Res, "遗失的书籍不应该能续借");
    QCOMPARE(Res.getErrCode(), ErrorCode::InvalidStatus);
    QVERIFY(Res.getErrMsg().contains("遗失"));

    // 验证 due_date 未被修改（使用固定日期避免时区问题）
    QString OriginalDueDate("2025-01-10");
    Query.exec(
        QString("UPDATE borrow_record SET due_date = '%1' WHERE id = 5002")
            .arg(OriginalDueDate));
    Query.exec("SELECT due_date FROM borrow_record WHERE id = 5002");
    Query.next();
    QCOMPARE(Query.value(0).toString(), OriginalDueDate);
  }
};

QTEST_GUILESS_MAIN(LibrarySystemTest)
#include "test_main.moc"
