#include "Library.h"

#include "xlsxdocument.h"

#include <QDate>
#include <QFile>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// ========== 文件锁（Windows 独占锁，防止 Excel 同时打开） ==========

ErrorOr<void> LibrarySystem::lockXlsxFile(const QString &Path) {
#ifdef Q_OS_WIN
  HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(Path.utf16()), 0, 0, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    DWORD err = GetLastError();
    if (err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION)
      return {ErrorCode::InternalError, "请关闭 Excel 后重试"};
    // 文件不存在的情况交给 loadFromXlsx 报错
    return {};
  }
  LockHandle = h;
#endif
  return {};
}

void LibrarySystem::unlockXlsxFile() {
#ifdef Q_OS_WIN
  if (LockHandle) {
    CloseHandle(LockHandle);
    LockHandle = nullptr;
  }
#endif
}

// ========== 从 xlsx 加载全部数据到内存 ==========

ErrorOr<void> LibrarySystem::loadFromXlsx() {
  QXlsx::Document xlsx(XlsxPath);
  if (!xlsx.load())
    return {ErrorCode::DatabaseError, "无法打开 Excel 文件: " + XlsxPath};

  // 清空内存
  Infos.clear();
  Copies.clear();
  Readers.clear();
  Borrows.clear();
  BarcodeToCopyIdx.clear();
  CardToReaderIdx.clear();
  BarcodeNotes.clear();
  NextInfoID = 1;
  NextCopyID = 1;
  NextReaderID = 1;
  NextBorrowID = 1;

  QStringList sheets = xlsx.sheetNames();
  if (sheets.isEmpty())
    return {ErrorCode::DatabaseError, "Excel 文件没有 Sheet"};

  // ---- 主 Sheet（第一个 Sheet）：书名/作者/出版社/数量/编号/重新生成的编号/备注/状态 ----
  xlsx.selectSheet(sheets[0]);
  int rowCount = xlsx.dimension().rowCount();
  int colCount = xlsx.dimension().columnCount();

  for (int row = 2; row <= rowCount; ++row) {
    QString title = xlsx.read(row, 1).toString().trimmed();
    if (title.isEmpty())
      continue;

    QString author = xlsx.read(row, 2).toString().trimmed();
    QString publisher = xlsx.read(row, 3).toString().trimmed();
    QString imageNo = xlsx.read(row, 5).toString().trimmed();
    QString barcode = xlsx.read(row, 6).toString().trimmed();
    QString notes = xlsx.read(row, 7).toString().trimmed();

    if (barcode.isEmpty())
      continue;

    int status = 0;
    if (colCount >= 8)
      status = xlsx.read(row, 8).toInt();

    QString coverPath = "covers/" + imageNo + ".jpg";

    BookInfo info;
    info.ID = NextInfoID++;
    info.Title = title;
    info.Author = author;
    info.Publisher = publisher;
    info.CoverPath = coverPath;
    Infos.append(info);

    BookCopy copy;
    copy.ID = NextCopyID++;
    copy.InfoID = info.ID;
    copy.Barcode = barcode;
    copy.Status = static_cast<BookCopy::BookStatus>(status);
    Copies.append(copy);
    BarcodeToCopyIdx[barcode] = Copies.size() - 1;
    if (!notes.isEmpty())
      BarcodeNotes[barcode] = notes;
  }

  // ---- _readers Sheet ----
  if (sheets.contains("_readers")) {
    xlsx.selectSheet("_readers");
    int rMax = xlsx.dimension().rowCount();
    for (int row = 2; row <= rMax; ++row) {
      QString name = xlsx.read(row, 1).toString().trimmed();
      if (name.isEmpty())
        continue;
      Reader r;
      r.ID = NextReaderID++;
      r.Name = name;
      r.CardNumber = xlsx.read(row, 2).toString().trimmed();
      r.PhoneNumber = xlsx.read(row, 3).toString().trimmed();
      r.IsInactive = xlsx.read(row, 4).toInt() != 0;
      Readers.append(r);
      if (!r.CardNumber.isEmpty())
        CardToReaderIdx[r.CardNumber] = Readers.size() - 1;
    }
  }

  // ---- _borrows Sheet ----
  if (sheets.contains("_borrows")) {
    xlsx.selectSheet("_borrows");
    int bMax = xlsx.dimension().rowCount();
    for (int row = 2; row <= bMax; ++row) {
      QVariant idVal = xlsx.read(row, 1);
      if (idVal.isNull() || idVal.toString().trimmed().isEmpty())
        continue;

      BorrowRecord br;
      br.ID = idVal.toInt();
      if (br.ID >= NextBorrowID)
        NextBorrowID = br.ID + 1;

      QString cardNumber = xlsx.read(row, 2).toString().trimmed();
      QString barcode = xlsx.read(row, 3).toString().trimmed();

      if (CardToReaderIdx.contains(cardNumber))
        br.ReaderId = Readers[CardToReaderIdx[cardNumber]].ID;
      if (BarcodeToCopyIdx.contains(barcode))
        br.CopyId = Copies[BarcodeToCopyIdx[barcode]].ID;

      QString borrowStr = xlsx.read(row, 4).toString().trimmed();
      QString dueStr = xlsx.read(row, 5).toString().trimmed();
      QString returnStr = xlsx.read(row, 6).toString().trimmed();
      br.BorrowDate = QDateTime::fromString(borrowStr, "yyyy-MM-dd");
      br.DueDate = QDateTime::fromString(dueStr, "yyyy-MM-dd");
      if (!returnStr.isEmpty())
        br.ReturnDate = QDateTime::fromString(returnStr, "yyyy-MM-dd");

      Borrows.append(br);
    }
  }

  // 回到主 Sheet
  xlsx.selectSheet(sheets[0]);
  return {};
}

// ========== 全量保存内存数据到 xlsx ==========

ErrorOr<void> LibrarySystem::saveToXlsx() {
  unlockXlsxFile();

  QXlsx::Document xlsx;

  // ---- 主 Sheet：书名/作者/出版社/数量/编号/重新生成的编号/备注/状态 ----
  QStringList headers = {"书名", "作者", "出版社", "数量", "编号",
                         "重新生成的编号", "备注", "状态"};
  for (int c = 0; c < headers.size(); ++c)
    xlsx.write(1, c + 1, headers[c]);

  int row = 2;
  for (const auto &copy : std::as_const(Copies)) {
    // 查找对应 BookInfo
    const BookInfo *info = nullptr;
    for (const auto &inf : std::as_const(Infos)) {
      if (inf.ID == copy.InfoID) {
        info = &inf;
        break;
      }
    }
    if (!info)
      continue;

    // 从 CoverPath "covers/xxx.jpg" 提取图片编号 "xxx"
    QString imageNo;
    const QString &cp = info->CoverPath;
    if (cp.startsWith("covers/") && cp.endsWith(".jpg"))
      imageNo = cp.mid(7, cp.length() - 11);

    xlsx.write(row, 1, info->Title);
    xlsx.write(row, 2, info->Author);
    xlsx.write(row, 3, info->Publisher);
    xlsx.write(row, 4, 1);                      // 数量
    xlsx.write(row, 5, imageNo);                // 编号
    xlsx.write(row, 6, copy.Barcode);           // 重新生成的编号
    xlsx.write(row, 7, BarcodeNotes.value(copy.Barcode)); // 备注
    xlsx.write(row, 8, static_cast<int>(copy.Status));    // 状态
    ++row;
  }

  // ---- _readers Sheet ----
  xlsx.addSheet("_readers");
  QStringList rHeaders = {"姓名", "卡号", "电话", "是否注销"};
  for (int c = 0; c < rHeaders.size(); ++c)
    xlsx.write(1, c + 1, rHeaders[c]);
  int rRow = 2;
  for (const auto &r : std::as_const(Readers)) {
    xlsx.write(rRow, 1, r.Name);
    xlsx.write(rRow, 2, r.CardNumber);
    xlsx.write(rRow, 3, r.PhoneNumber);
    xlsx.write(rRow, 4, r.IsInactive ? 1 : 0);
    ++rRow;
  }

  // ---- _borrows Sheet ----
  xlsx.addSheet("_borrows");
  QStringList bHeaders = {"id", "读者卡号", "书籍条码", "借书日期",
                          "应还日期", "归还日期"};
  for (int c = 0; c < bHeaders.size(); ++c)
    xlsx.write(1, c + 1, bHeaders[c]);
  int bRow = 2;
  for (const auto &br : std::as_const(Borrows)) {
    // 解析外键为自然键
    QString cardNumber, barcode;
    for (const auto &r : std::as_const(Readers)) {
      if (r.ID == br.ReaderId) {
        cardNumber = r.CardNumber;
        break;
      }
    }
    for (const auto &c : std::as_const(Copies)) {
      if (c.ID == br.CopyId) {
        barcode = c.Barcode;
        break;
      }
    }

    xlsx.write(bRow, 1, br.ID);
    xlsx.write(bRow, 2, cardNumber);
    xlsx.write(bRow, 3, barcode);
    xlsx.write(bRow, 4, br.BorrowDate.toString("yyyy-MM-dd"));
    xlsx.write(bRow, 5, br.DueDate.toString("yyyy-MM-dd"));
    if (br.ReturnDate.isValid())
      xlsx.write(bRow, 6, br.ReturnDate.toString("yyyy-MM-dd"));
    ++bRow;
  }

  // 回到主 Sheet
  xlsx.selectSheet(xlsx.sheetNames()[0]);

  if (!xlsx.saveAs(XlsxPath)) {
    lockXlsxFile(XlsxPath);
    return {ErrorCode::DatabaseError, "保存 Excel 文件失败"};
  }

  return lockXlsxFile(XlsxPath);
}

// ========== 初始化 ==========

ErrorOr<void> LibrarySystem::init(const QString &XlsxPath) {
  this->XlsxPath = XlsxPath;
  this->DataDir = QFileInfo(XlsxPath).absolutePath();

  if (!QFile::exists(XlsxPath))
    return {ErrorCode::DatabaseError, "Excel 文件不存在: " + XlsxPath};

  // 先加载数据，再锁定文件（锁定后 QXlsx 读不了）
  auto loadRes = loadFromXlsx();
  if (!loadRes)
    return loadRes;

  return lockXlsxFile(XlsxPath);
}

// ========== 通过条码查询书籍信息 ==========

ErrorOr<std::pair<BookInfo, BookCopy>>
LibrarySystem::getBookDataByBarcode(const QString &Barcode) {
  if (!BarcodeToCopyIdx.contains(Barcode))
    return {ErrorCode::NotFound, "条码不存在: " + Barcode};

  const BookCopy &copy = Copies[BarcodeToCopyIdx[Barcode]];
  for (const auto &info : std::as_const(Infos)) {
    if (info.ID == copy.InfoID)
      return std::make_pair(info, copy);
  }
  return {ErrorCode::InternalError, "数据不一致：副本关联的书籍信息不存在"};
}

// ========== 通过卡号查询读者 ==========

ErrorOr<Reader>
LibrarySystem::getReaderByCardNumber(const QString &CardNumber) {
  if (!CardToReaderIdx.contains(CardNumber))
    return {ErrorCode::NotFound, "读者不存在: " + CardNumber};
  return Readers[CardToReaderIdx[CardNumber]];
}

// ========== 借书（单本，内存操作） ==========

ErrorOr<void> LibrarySystem::borrowBook(int ReaderID, int CopyID) {
  // 查找副本
  int copyIdx = -1;
  for (int i = 0; i < Copies.size(); ++i) {
    if (Copies[i].ID == CopyID) {
      copyIdx = i;
      break;
    }
  }
  if (copyIdx < 0)
    return {ErrorCode::NotFound, "未找到书籍副本，副本ID: " + QString::number(CopyID)};

  const BookCopy &copy = Copies[copyIdx];
  if (copy.Status == BookCopy::BS_Borrowed)
    return {ErrorCode::InvalidStatus, "该书籍目前处于'借出'状态，无法再次借出"};
  if (copy.Status == BookCopy::BS_Lost)
    return {ErrorCode::InvalidStatus, "该书籍目前处于'遗失'状态，无法借出"};
  if (copy.Status == BookCopy::BS_NonLendable)
    return {ErrorCode::InvalidStatus, "该书籍目前处于'非外借书'状态，无法借出"};

  // 创建借阅记录
  BorrowRecord br;
  br.ID = NextBorrowID++;
  br.ReaderId = ReaderID;
  br.CopyId = CopyID;
  br.BorrowDate = QDateTime::currentDateTime();
  br.DueDate = br.BorrowDate.addDays(30);
  Borrows.append(br);

  // 更新副本状态
  Copies[copyIdx].Status = BookCopy::BS_Borrowed;
  return {};
}

// ========== 批量借书 ==========

ErrorOr<void> LibrarySystem::borrowBooks(int ReaderID,
                                         const QVector<int> &CopyIDs) {
  if (CopyIDs.isEmpty())
    return {ErrorCode::ValidationError, "未选择任何书籍"};

  // 检查读者是否存在且未注销
  Reader *reader = nullptr;
  for (auto &r : Readers) {
    if (r.ID == ReaderID) {
      reader = &r;
      break;
    }
  }
  if (!reader)
    return {ErrorCode::NotFound, "读者不存在"};
  if (reader->IsInactive)
    return {ErrorCode::InvalidStatus, "该读者已注销，无法借书"};

  for (int cid : CopyIDs) {
    auto res = borrowBook(ReaderID, cid);
    if (!res)
      return res;
  }

  return saveToXlsx();
}

// ========== 归还（单本，内存操作） ==========

ErrorOr<void> LibrarySystem::returnBook(int RecordID) {
  int brIdx = -1;
  for (int i = 0; i < Borrows.size(); ++i) {
    if (Borrows[i].ID == RecordID) {
      brIdx = i;
      break;
    }
  }
  if (brIdx < 0)
    return {ErrorCode::NotFound, "未找到借阅记录，记录ID: " + QString::number(RecordID)};

  BorrowRecord &br = Borrows[brIdx];
  if (br.ReturnDate.isValid())
    return {ErrorCode::InvalidStatus, "该借阅记录已归还，无法重复操作"};

  // 查找副本
  int copyIdx = -1;
  for (int i = 0; i < Copies.size(); ++i) {
    if (Copies[i].ID == br.CopyId) {
      copyIdx = i;
      break;
    }
  }
  if (copyIdx < 0)
    return {ErrorCode::InternalError, "数据不一致：借阅记录关联的副本不存在"};

  const BookCopy &copy = Copies[copyIdx];
  if (copy.Status == BookCopy::BS_Lost)
    return {ErrorCode::InvalidStatus, "该书籍处于'遗失'状态，请先办理挂失处理后再归还"};
  if (copy.Status == BookCopy::BS_NonLendable)
    return {ErrorCode::InvalidStatus, "该书籍处于'非外借书'状态，无法归还"};

  br.ReturnDate = QDateTime::currentDateTime();
  Copies[copyIdx].Status = BookCopy::BS_InLibrary;
  return {};
}

// ========== 续借（单本，内存操作） ==========

ErrorOr<void> LibrarySystem::renewBook(int RecordID) {
  int brIdx = -1;
  for (int i = 0; i < Borrows.size(); ++i) {
    if (Borrows[i].ID == RecordID) {
      brIdx = i;
      break;
    }
  }
  if (brIdx < 0)
    return {ErrorCode::NotFound, "未找到借阅记录，记录ID: " + QString::number(RecordID)};

  BorrowRecord &br = Borrows[brIdx];
  if (br.ReturnDate.isValid())
    return {ErrorCode::InvalidStatus, "该借阅记录已归还，无法续借"};

  int copyIdx = -1;
  for (int i = 0; i < Copies.size(); ++i) {
    if (Copies[i].ID == br.CopyId) {
      copyIdx = i;
      break;
    }
  }
  if (copyIdx < 0)
    return {ErrorCode::InternalError, "数据不一致：借阅记录关联的副本不存在"};

  const BookCopy &copy = Copies[copyIdx];
  if (copy.Status == BookCopy::BS_Lost)
    return {ErrorCode::InvalidStatus, "该书籍处于'遗失'状态，无法续借"};
  if (copy.Status == BookCopy::BS_NonLendable)
    return {ErrorCode::InvalidStatus, "该书籍处于'非外借书'状态，无法续借"};

  br.DueDate = br.DueDate.addDays(30);
  return {};
}

// ========== 归还 + 续借（同一事务） ==========

ErrorOr<void>
LibrarySystem::returnAndRenewBooks(const QList<int> &ReturnRecordIDs,
                                   const QList<int> &RenewRecordIDs) {
  if (ReturnRecordIDs.isEmpty() && RenewRecordIDs.isEmpty())
    return {};

  for (int rid : ReturnRecordIDs) {
    auto res = returnBook(rid);
    if (!res)
      return res;
  }
  for (int rid : RenewRecordIDs) {
    auto res = renewBook(rid);
    if (!res)
      return res;
  }
  return saveToXlsx();
}

// ========== 查询某读者的借阅详情（未归还的） ==========

ErrorOr<QVector<BorrowDetailType>>
LibrarySystem::getBorrowingDetailsByReader(int ReaderId) {
  QVector<BorrowDetailType> results;
  for (const auto &br : std::as_const(Borrows)) {
    if (br.ReaderId != ReaderId)
      continue;
    if (br.ReturnDate.isValid())
      continue; // 已归还的跳过

    // 查找对应 Copy 和 Info
    const BookCopy *copy = nullptr;
    for (const auto &c : std::as_const(Copies)) {
      if (c.ID == br.CopyId) {
        copy = &c;
        break;
      }
    }
    if (!copy)
      continue;
    const BookInfo *info = nullptr;
    for (const auto &inf : std::as_const(Infos)) {
      if (inf.ID == copy->InfoID) {
        info = &inf;
        break;
      }
    }
    if (!info)
      continue;

    BorrowDetailType detail;
    detail.Record = br;
    detail.Copy = *copy;
    detail.Info = *info;
    results.append(detail);
  }
  return results;
}

// ========== 综合查询（条码/书名/作者/出版社，支持模糊） ==========

ErrorOr<QVector<std::pair<BorrowDetailType, Reader>>>
LibrarySystem::queryBooks(const QString &Barcode, const QString &Title,
                          const QString &Author, const QString &Publisher) {
  QVector<std::pair<BorrowDetailType, Reader>> results;

  for (const auto &copy : std::as_const(Copies)) {
    // 模糊匹配
    if (!Barcode.isEmpty() && !copy.Barcode.contains(Barcode, Qt::CaseInsensitive))
      continue;

    const BookInfo *info = nullptr;
    for (const auto &inf : std::as_const(Infos)) {
      if (inf.ID == copy.InfoID) {
        info = &inf;
        break;
      }
    }
    if (!info)
      continue;

    if (!Title.isEmpty() && !info->Title.contains(Title, Qt::CaseInsensitive))
      continue;
    if (!Author.isEmpty() && !info->Author.contains(Author, Qt::CaseInsensitive))
      continue;
    if (!Publisher.isEmpty() &&
        !info->Publisher.contains(Publisher, Qt::CaseInsensitive))
      continue;

    BorrowDetailType detail;
    detail.Copy = copy;
    detail.Info = *info;
    detail.Record.ID = -1;

    Reader reader;
    reader.ID = -1;

    // 查找未归还的借阅记录及相关读者
    for (const auto &br : std::as_const(Borrows)) {
      if (br.CopyId == copy.ID && !br.ReturnDate.isValid()) {
        detail.Record = br;
        for (const auto &r : std::as_const(Readers)) {
          if (r.ID == br.ReaderId) {
            reader = r;
            break;
          }
        }
        break;
      }
    }

    results.append(std::make_pair(detail, reader));
  }
  return results;
}

// ========== 生成新读者卡号 ==========

ErrorOr<QString> LibrarySystem::getNewReaderCardID() {
  int maxId = 2000000;
  for (const auto &r : std::as_const(Readers)) {
    bool ok;
    int id = r.CardNumber.toInt(&ok);
    if (ok && id > maxId)
      maxId = id;
  }
  return QString::number(maxId + 1);
}

// ========== 读者管理 ==========

ErrorOr<QVector<Reader>> LibrarySystem::getAllReaders() {
  return Readers;
}

ErrorOr<void> LibrarySystem::addReader(const QString &Name,
                                       const QString &CardNumber,
                                       const QString &PhoneNumber) {
  // 卡号唯一性检查
  if (CardToReaderIdx.contains(CardNumber))
    return {ErrorCode::ValidationError, "卡号已存在: " + CardNumber};

  Reader r;
  r.ID = NextReaderID++;
  r.Name = Name;
  r.CardNumber = CardNumber;
  r.PhoneNumber = PhoneNumber;
  r.IsInactive = false;
  Readers.append(r);
  CardToReaderIdx[CardNumber] = Readers.size() - 1;

  return saveToXlsx();
}

ErrorOr<void> LibrarySystem::updateReader(int ID, const QString &Name,
                                          const QString &CardNumber,
                                          const QString &PhoneNumber) {
  int idx = -1;
  for (int i = 0; i < Readers.size(); ++i) {
    if (Readers[i].ID == ID) {
      idx = i;
      break;
    }
  }
  if (idx < 0)
    return {ErrorCode::NotFound, "读者不存在"};

  // 卡号唯一性检查（排除自身）
  if (CardToReaderIdx.contains(CardNumber) &&
      CardToReaderIdx[CardNumber] != idx)
    return {ErrorCode::ValidationError, "卡号已被其他读者使用: " + CardNumber};

  // 更新卡号索引
  QString oldCard = Readers[idx].CardNumber;
  CardToReaderIdx.remove(oldCard);

  Readers[idx].Name = Name;
  Readers[idx].CardNumber = CardNumber;
  Readers[idx].PhoneNumber = PhoneNumber;
  CardToReaderIdx[CardNumber] = idx;

  return saveToXlsx();
}

// ========== 催还查询 ==========

ErrorOr<QVector<LibrarySystem::ReaderBorrowInfo>>
LibrarySystem::getRemindBorrowings(int Days) {
  QMap<int, ReaderBorrowInfo> readerMap;
  QDate today = QDate::currentDate();
  QDate deadline = today.addDays(Days);

  for (const auto &br : std::as_const(Borrows)) {
    if (br.ReturnDate.isValid())
      continue;

    // 查找 Copy 和 Info
    const BookCopy *copy = nullptr;
    for (const auto &c : std::as_const(Copies)) {
      if (c.ID == br.CopyId) {
        copy = &c;
        break;
      }
    }
    if (!copy)
      continue;
    const BookInfo *info = nullptr;
    for (const auto &inf : std::as_const(Infos)) {
      if (inf.ID == copy->InfoID) {
        info = &inf;
        break;
      }
    }
    if (!info)
      continue;

    // 查找 Reader
    const Reader *reader = nullptr;
    for (const auto &r : std::as_const(Readers)) {
      if (r.ID == br.ReaderId) {
        reader = &r;
        break;
      }
    }
    if (!reader)
      continue;

    if (!readerMap.contains(reader->ID)) {
      ReaderBorrowInfo rbi;
      rbi.reader = *reader;
      readerMap[reader->ID] = rbi;
    }

    BorrowDetailType detail;
    detail.Record = br;
    detail.Copy = *copy;
    detail.Info = *info;

    QDate dueDate = br.DueDate.date();
    if (dueDate <= deadline)
      readerMap[reader->ID].urgentBooks.append(detail);
    else
      readerMap[reader->ID].otherBooks.append(detail);
  }

  QVector<ReaderBorrowInfo> results;
  for (auto &info : readerMap) {
    if (!info.urgentBooks.isEmpty())
      results.append(info);
  }
  return results;
}

