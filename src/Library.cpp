#include "Library.h"

#include "xlsxdocument.h"

#include <QDate>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

// ========== 文件锁（Windows 独占锁，防止 Excel 同时打开） ==========

ErrorOr<void> LibrarySystem::lockXlsxFile(const QString &Path) {
#ifdef Q_OS_WIN
  HANDLE H = CreateFileW(reinterpret_cast<LPCWSTR>(Path.utf16()), 0, 0, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (H == INVALID_HANDLE_VALUE) {
    DWORD Err = GetLastError();
    if (Err == ERROR_SHARING_VIOLATION || Err == ERROR_LOCK_VIOLATION)
      return {ErrorCode::InternalError, "请关闭 Excel 后重试"};
    return {ErrorCode::InternalError,
            QString("无法锁定数据文件（错误代码 %1）").arg(Err)};
  }
  LockHandle = H;
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
  QXlsx::Document Xlsx(XlsxPath);
  if (!Xlsx.load())
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

  QStringList Sheets = Xlsx.sheetNames();
  if (Sheets.isEmpty())
    return {ErrorCode::DatabaseError, "Excel 文件没有 Sheet"};

  // ---- 主 Sheet（第一个
  // Sheet）：书名/作者/出版社/数量/封面图文件号/条形码/分类号/备注/状态 ----
  Xlsx.selectSheet(Sheets[0]);
  int RowCount = Xlsx.dimension().rowCount();
  int ColCount = Xlsx.dimension().columnCount();

  for (int Row = 2; Row <= RowCount; ++Row) {
    QString Title = Xlsx.read(Row, 1).toString().trimmed();
    if (Title.isEmpty())
      continue;

    QString Author = Xlsx.read(Row, 2).toString().trimmed();
    QString Publisher = Xlsx.read(Row, 3).toString().trimmed();
    QString ImageNo = Xlsx.read(Row, 5).toString().trimmed();
    QString BarCode = Xlsx.read(Row, 6).toString().trimmed();
    QString CLCID = Xlsx.read(Row, 7).toString().trimmed();
    QString Notes = Xlsx.read(Row, 8).toString().trimmed();

    if (BarCode.isEmpty())
      continue;

    int Status = 0;
    if (ColCount >= 9)
      Status = Xlsx.read(Row, 9).toInt();
    // 校验状态值范围，非法值直接报错
    if (Status < BookCopy::BS_InLibrary || Status > BookCopy::BS_Unkown_Status)
      return {ErrorCode::ValidationError,
              QString("条码 [%1] 的状态值 %2 非法，应为 0-4")
                  .arg(BarCode)
                  .arg(Status)};

    QString CoverPath = "covers/" + ImageNo + ".jpg";

    BookInfo Info;
    Info.ID = NextInfoID++;
    Info.Title = Title;
    Info.Author = Author;
    Info.Publisher = Publisher;
    Info.CoverPath = CoverPath;
    Info.CLCID = CLCID;
    Infos.append(Info);

    BookCopy Copy;
    Copy.ID = NextCopyID++;
    Copy.InfoID = Info.ID;
    Copy.Barcode = BarCode;
    Copy.Status = static_cast<BookCopy::BookStatus>(Status);
    Copies.append(Copy);
    BarcodeToCopyIdx[BarCode] = Copies.size() - 1;
    if (!Notes.isEmpty())
      BarcodeNotes[BarCode] = Notes;
  }

  // ---- _readers Sheet ----
  if (Sheets.contains("_readers")) {
    Xlsx.selectSheet("_readers");
    int RMax = Xlsx.dimension().rowCount();
    for (int Row = 2; Row <= RMax; ++Row) {
      QString Name = Xlsx.read(Row, 1).toString().trimmed();
      if (Name.isEmpty())
        continue;
      Reader R;
      R.ID = NextReaderID++;
      R.Name = Name;
      R.CardNumber = Xlsx.read(Row, 2).toString().trimmed();
      R.PhoneNumber = Xlsx.read(Row, 3).toString().trimmed();
      R.IsInactive = Xlsx.read(Row, 4).toInt() != 0;
      Readers.append(R);
      if (!R.CardNumber.isEmpty())
        CardToReaderIdx[R.CardNumber] = Readers.size() - 1;
    }
  }

  // ---- _borrows Sheet ----
  if (Sheets.contains("_borrows")) {
    Xlsx.selectSheet("_borrows");
    int BMax = Xlsx.dimension().rowCount();

    // 日期单元格可能是字符串（本程序写入）或 QDateTime（Excel 转换后），
    // 需要兼容两种格式，否则 Excel 打开保存后日期全部解析失败。
    auto readDateCell = [&Xlsx](int Row, int Col) -> QDateTime {
      QVariant Val = Xlsx.read(Row, Col);
      if (Val.userType() == QMetaType::QDateTime)
        return Val.toDateTime();
      if (Val.userType() == QMetaType::QDate)
        return Val.toDate().startOfDay();
      QString Str = Val.toString().trimmed();
      if (Str.isEmpty())
        return QDateTime();
      return QDateTime::fromString(Str, "yyyy-MM-dd");
    };

    for (int Row = 2; Row <= BMax; ++Row) {
      QVariant IdVal = Xlsx.read(Row, 1);
      if (IdVal.isNull() || IdVal.toString().trimmed().isEmpty())
        continue;

      BorrowRecord Br;
      Br.ID = IdVal.toInt();
      if (Br.ID >= NextBorrowID)
        NextBorrowID = Br.ID + 1;

      QString CardNumber = Xlsx.read(Row, 2).toString().trimmed();
      QString BarCode = Xlsx.read(Row, 3).toString().trimmed();

      if (CardToReaderIdx.contains(CardNumber))
        Br.ReaderId = Readers[CardToReaderIdx[CardNumber]].ID;
      if (BarcodeToCopyIdx.contains(BarCode))
        Br.CopyId = Copies[BarcodeToCopyIdx[BarCode]].ID;

      Br.BorrowDate = readDateCell(Row, 4);
      Br.DueDate = readDateCell(Row, 5);
      QDateTime Ret = readDateCell(Row, 6);
      if (Ret.isValid())
        Br.ReturnDate = Ret;

      Borrows.append(Br);
    }
  }

  // 回到主 Sheet
  Xlsx.selectSheet(Sheets[0]);
  return {};
}

// ========== 全量保存内存数据到 xlsx ==========

ErrorOr<void> LibrarySystem::saveToXlsx() {
  unlockXlsxFile();

  QXlsx::Document Xlsx;

  // ---- 主 Sheet：书名/作者/出版社/数量/封面图文件号/条形码/分类号/备注/状态
  // ----
  QStringList Headers = {"书名",   "作者",   "出版社", "数量", "封面图文件号",
                         "条形码", "分类号", "备注",   "状态"};
  for (int C = 0; C < Headers.size(); ++C)
    Xlsx.write(1, C + 1, Headers[C]);

  int Row = 2;
  for (const auto &Copy : std::as_const(Copies)) {
    // 查找对应 BookInfo
    const BookInfo *Info = nullptr;
    for (const auto &Inf : std::as_const(Infos)) {
      if (Inf.ID == Copy.InfoID) {
        Info = &Inf;
        break;
      }
    }
    if (!Info)
      continue;

    // 从 CoverPath "covers/xxx.jpg" 提取图片编号 "xxx"
    QString ImageNo;
    const QString &Cp = Info->CoverPath;
    if (Cp.startsWith("covers/") && Cp.endsWith(".jpg"))
      ImageNo = Cp.mid(7, Cp.length() - 11);

    Xlsx.write(Row, 1, Info->Title);
    Xlsx.write(Row, 2, Info->Author);
    Xlsx.write(Row, 3, Info->Publisher);
    Xlsx.write(Row, 4, 1);                                // 数量
    Xlsx.write(Row, 5, ImageNo);                          // 封面图文件号
    Xlsx.write(Row, 6, Copy.Barcode);                     // 条形码
    Xlsx.write(Row, 7, Info->CLCID);                      // 分类号
    Xlsx.write(Row, 8, BarcodeNotes.value(Copy.Barcode)); // 备注
    Xlsx.write(Row, 9, static_cast<int>(Copy.Status));    // 状态
    ++Row;
  }

  // ---- _readers Sheet ----
  Xlsx.addSheet("_readers");
  QStringList RHeaders = {"姓名", "卡号", "电话", "是否注销"};
  for (int C = 0; C < RHeaders.size(); ++C)
    Xlsx.write(1, C + 1, RHeaders[C]);
  int RRow = 2;
  for (const auto &R : std::as_const(Readers)) {
    Xlsx.write(RRow, 1, R.Name);
    Xlsx.write(RRow, 2, R.CardNumber);
    Xlsx.write(RRow, 3, R.PhoneNumber);
    Xlsx.write(RRow, 4, R.IsInactive ? 1 : 0);
    ++RRow;
  }

  // ---- _borrows Sheet ----
  Xlsx.addSheet("_borrows");
  QStringList BHeaders = {"id",       "读者卡号", "书籍条码",
                          "借书日期", "应还日期", "归还日期"};
  for (int C = 0; C < BHeaders.size(); ++C)
    Xlsx.write(1, C + 1, BHeaders[C]);
  int BRow = 2;
  for (const auto &Br : std::as_const(Borrows)) {
    // 解析外键为自然键
    QString CardNumber, BarCode;
    for (const auto &R : std::as_const(Readers)) {
      if (R.ID == Br.ReaderId) {
        CardNumber = R.CardNumber;
        break;
      }
    }
    for (const auto &C : std::as_const(Copies)) {
      if (C.ID == Br.CopyId) {
        BarCode = C.Barcode;
        break;
      }
    }

    Xlsx.write(BRow, 1, Br.ID);
    Xlsx.write(BRow, 2, CardNumber);
    Xlsx.write(BRow, 3, BarCode);
    Xlsx.write(BRow, 4, Br.BorrowDate.toString("yyyy-MM-dd"));
    Xlsx.write(BRow, 5, Br.DueDate.toString("yyyy-MM-dd"));
    if (Br.ReturnDate.isValid())
      Xlsx.write(BRow, 6, Br.ReturnDate.toString("yyyy-MM-dd"));
    ++BRow;
  }

  // 回到主 Sheet
  Xlsx.selectSheet(Xlsx.sheetNames()[0]);

  // 保存前备份当前文件（覆盖旧 .bak），保证 saveAs 失败时有可恢复的副本。
  // 备份失败不中断保存流程，仅记日志。
  QString BakPath = XlsxPath + ".bak";
  if (QFile::exists(BakPath))
    QFile::remove(BakPath);
  if (QFile::exists(XlsxPath)) {
    if (QFile::copy(XlsxPath, BakPath))
      qInfo() << "已备份原数据文件到" << BakPath;
    else
      qWarning() << "备份数据文件失败，继续保存:" << BakPath;
  }

  if (!Xlsx.saveAs(XlsxPath)) {
    // saveAs 失败：原文件可能已损坏，尝试从 .bak 恢复
    if (QFile::exists(BakPath)) {
      QFile::remove(XlsxPath);
      QFile::copy(BakPath, XlsxPath);
      qInfo() << "saveAs 失败，已从备份恢复原文件:" << BakPath;
    }
    lockXlsxFile(XlsxPath);
    return {ErrorCode::DatabaseError, "保存 Excel 文件失败"};
  }

  return lockXlsxFile(XlsxPath);
}

// ========== 初始化 ==========

ErrorOr<void> LibrarySystem::init(const QString &XlsxPath, int BorrowDays,
                                  int MaxBooks) {
  this->XlsxPath = XlsxPath;
  this->DataDir = QFileInfo(XlsxPath).absolutePath();
  this->BorrowDays = qMax(1, BorrowDays);
  this->MaxBooks = qMax(1, MaxBooks);

  if (!QFile::exists(XlsxPath))
    return {ErrorCode::DatabaseError, "Excel 文件不存在: " + XlsxPath};

  // 先加载数据，再锁定文件（锁定后 QXlsx 读不了）
  auto LoadRes = loadFromXlsx();
  if (!LoadRes)
    return LoadRes;

  return lockXlsxFile(XlsxPath);
}

// ========== 通过条码查询书籍信息 ==========

ErrorOr<std::pair<BookInfo, BookCopy>>
LibrarySystem::getBookDataByBarcode(const QString &Barcode) {
  if (!BarcodeToCopyIdx.contains(Barcode))
    return {ErrorCode::NotFound, "条码不存在: " + Barcode};

  const BookCopy &Copy = Copies[BarcodeToCopyIdx[Barcode]];
  for (const auto &Info : std::as_const(Infos)) {
    if (Info.ID == Copy.InfoID)
      return std::make_pair(Info, Copy);
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

// ========== 按手机号精确匹配读者（家庭共用号码可能多人命中） ==========

ErrorOr<QVector<Reader>>
LibrarySystem::searchReadersByPhone(const QString &PhoneNumber) {
  if (PhoneNumber.isEmpty())
    return {ErrorCode::ValidationError, "手机号不能为空"};
  QVector<Reader> Results;
  for (const auto &R : std::as_const(Readers)) {
    if (R.PhoneNumber == PhoneNumber)
      Results.append(R);
  }
  return Results;
}

// ========== 按姓名模糊匹配读者（重名可能多人命中） ==========

ErrorOr<QVector<Reader>>
LibrarySystem::searchReadersByName(const QString &Name) {
  if (Name.isEmpty())
    return {ErrorCode::ValidationError, "姓名不能为空"};
  QVector<Reader> Results;
  for (const auto &R : std::as_const(Readers)) {
    if (R.Name.contains(Name, Qt::CaseInsensitive))
      Results.append(R);
  }
  return Results;
}

// ========== 借书（单本，内存操作） ==========

ErrorOr<void> LibrarySystem::borrowBook(int ReaderID, int CopyID) {
  // 查找副本
  int CopyIdx = -1;
  for (int I = 0; I < Copies.size(); ++I) {
    if (Copies[I].ID == CopyID) {
      CopyIdx = I;
      break;
    }
  }
  if (CopyIdx < 0)
    return {ErrorCode::NotFound,
            "未找到书籍副本，副本ID: " + QString::number(CopyID)};

  const BookCopy &Copy = Copies[CopyIdx];
  if (Copy.Status == BookCopy::BS_Borrowed)
    return {ErrorCode::InvalidStatus, "该书籍目前处于'借出'状态，无法再次借出"};
  if (Copy.Status == BookCopy::BS_Lost)
    return {ErrorCode::InvalidStatus, "该书籍目前处于'遗失'状态，无法借出"};
  if (Copy.Status == BookCopy::BS_NonLendable)
    return {ErrorCode::InvalidStatus, "该书籍目前处于'非外借书'状态，无法借出"};
  if (Copy.Status == BookCopy::BS_Unkown_Status)
    return {ErrorCode::InvalidStatus, "该书籍目前处于'未知状态'，无法借出"};

  // 创建借阅记录
  BorrowRecord Br;
  Br.ID = NextBorrowID++;
  Br.ReaderId = ReaderID;
  Br.CopyId = CopyID;
  Br.BorrowDate = QDateTime::currentDateTime();
  Br.DueDate = Br.BorrowDate.addDays(BorrowDays);
  Borrows.append(Br);

  // 更新副本状态
  Copies[CopyIdx].Status = BookCopy::BS_Borrowed;
  return {};
}

// ========== 批量借书 ==========

ErrorOr<void> LibrarySystem::borrowBooks(int ReaderID,
                                         const QVector<int> &CopyIDs) {
  if (CopyIDs.isEmpty())
    return {ErrorCode::ValidationError, "未选择任何书籍"};

  // 检查是否有重复的副本 ID
  QSet<int> Seen;
  for (int Cid : CopyIDs) {
    if (Seen.contains(Cid))
      return {ErrorCode::ValidationError, "重复选择了同一本书"};
    Seen.insert(Cid);
  }

  // 检查读者是否存在且未注销
  Reader *Reader = nullptr;
  for (auto &R : Readers) {
    if (R.ID == ReaderID) {
      Reader = &R;
      break;
    }
  }
  if (!Reader)
    return {ErrorCode::NotFound, "读者不存在"};
  if (Reader->IsInactive)
    return {ErrorCode::InvalidStatus, "该读者已注销，无法借书"};

  // 借书数量上限：读者未归还数量 + 本次借书数量不得超过上限
  int ActiveCount = getActiveBorrowCount(ReaderID);
  if (ActiveCount + CopyIDs.size() > MaxBooks)
    return {ErrorCode::InvalidStatus,
            QString("超出借书数量上限：每人最多可借 %1 本，该读者当前已借 %2 "
                    "本，本次拟借 %3 本")
                .arg(MaxBooks)
                .arg(ActiveCount)
                .arg(CopyIDs.size())};

  // 预检：所有副本必须存在且可借，避免部分失败导致内存状态不一致
  for (int Cid : CopyIDs) {
    int CopyIdx = -1;
    for (int I = 0; I < Copies.size(); ++I) {
      if (Copies[I].ID == Cid) {
        CopyIdx = I;
        break;
      }
    }
    if (CopyIdx < 0)
      return {ErrorCode::NotFound,
              "未找到书籍副本，副本ID: " + QString::number(Cid)};

    const BookCopy &Copy = Copies[CopyIdx];
    if (Copy.Status == BookCopy::BS_Borrowed)
      return {ErrorCode::InvalidStatus,
              "该书籍目前处于'借出'状态，无法再次借出"};
    if (Copy.Status == BookCopy::BS_Lost)
      return {ErrorCode::InvalidStatus, "该书籍目前处于'遗失'状态，无法借出"};
    if (Copy.Status == BookCopy::BS_NonLendable)
      return {ErrorCode::InvalidStatus,
              "该书籍目前处于'非外借书'状态，无法借出"};
    if (Copy.Status == BookCopy::BS_Unkown_Status)
      return {ErrorCode::InvalidStatus,
              "该书籍目前处于'未知状态'，无法借出"};
  }

  // 全部预检通过后执行借书（此时不会再因状态问题失败）
  for (int Cid : CopyIDs) {
    auto Res = borrowBook(ReaderID, Cid);
    if (!Res)
      return Res;
  }

  return saveToXlsx();
}

// ========== 归还（单本，内存操作） ==========

ErrorOr<void> LibrarySystem::returnBook(int RecordID) {
  int BrIdx = -1;
  for (int I = 0; I < Borrows.size(); ++I) {
    if (Borrows[I].ID == RecordID) {
      BrIdx = I;
      break;
    }
  }
  if (BrIdx < 0)
    return {ErrorCode::NotFound,
            "未找到借阅记录，记录ID: " + QString::number(RecordID)};

  BorrowRecord &Br = Borrows[BrIdx];
  if (Br.ReturnDate.isValid())
    return {ErrorCode::InvalidStatus, "该借阅记录已归还，无法重复操作"};

  // 查找副本
  int CopyIdx = -1;
  for (int I = 0; I < Copies.size(); ++I) {
    if (Copies[I].ID == Br.CopyId) {
      CopyIdx = I;
      break;
    }
  }
  if (CopyIdx < 0)
    return {ErrorCode::InternalError, "数据不一致：借阅记录关联的副本不存在"};

  const BookCopy &Copy = Copies[CopyIdx];
  if (Copy.Status == BookCopy::BS_Lost)
    return {ErrorCode::InvalidStatus,
            "该书籍处于'遗失'状态，请先办理挂失处理后再归还"};
  if (Copy.Status == BookCopy::BS_NonLendable)
    return {ErrorCode::InvalidStatus, "该书籍处于'非外借书'状态，无法归还"};
  if (Copy.Status == BookCopy::BS_Unkown_Status)
    return {ErrorCode::InvalidStatus, "该书籍处于'未知状态'，无法归还"};

  Br.ReturnDate = QDateTime::currentDateTime();
  Copies[CopyIdx].Status = BookCopy::BS_InLibrary;
  return {};
}

// ========== 续借（单本，内存操作） ==========

ErrorOr<void> LibrarySystem::renewBook(int RecordID) {
  int BrIdx = -1;
  for (int I = 0; I < Borrows.size(); ++I) {
    if (Borrows[I].ID == RecordID) {
      BrIdx = I;
      break;
    }
  }
  if (BrIdx < 0)
    return {ErrorCode::NotFound,
            "未找到借阅记录，记录ID: " + QString::number(RecordID)};

  BorrowRecord &Br = Borrows[BrIdx];
  if (Br.ReturnDate.isValid())
    return {ErrorCode::InvalidStatus, "该借阅记录已归还，无法续借"};

  int CopyIdx = -1;
  for (int I = 0; I < Copies.size(); ++I) {
    if (Copies[I].ID == Br.CopyId) {
      CopyIdx = I;
      break;
    }
  }
  if (CopyIdx < 0)
    return {ErrorCode::InternalError, "数据不一致：借阅记录关联的副本不存在"};

  const BookCopy &Copy = Copies[CopyIdx];
  if (Copy.Status == BookCopy::BS_Lost)
    return {ErrorCode::InvalidStatus, "该书籍处于'遗失'状态，无法续借"};
  if (Copy.Status == BookCopy::BS_NonLendable)
    return {ErrorCode::InvalidStatus, "该书籍处于'非外借书'状态，无法续借"};
  if (Copy.Status == BookCopy::BS_Unkown_Status)
    return {ErrorCode::InvalidStatus, "该书籍处于'未知状态'，无法续借"};

  Br.DueDate = Br.DueDate.addDays(BorrowDays);
  return {};
}

// ========== 归还 + 续借（同一事务，先预检再执行，保证原子性） ==========

ErrorOr<void>
LibrarySystem::returnAndRenewBooks(const QList<int> &ReturnRecordIDs,
                                   const QList<int> &RenewRecordIDs) {
  if (ReturnRecordIDs.isEmpty() && RenewRecordIDs.isEmpty())
    return {};

  // 检查是否有重复的记录 ID（同一记录不能出现两次，也不能同时归还和续借）
  QSet<int> Seen;
  for (int Rid : ReturnRecordIDs) {
    if (Seen.contains(Rid))
      return {ErrorCode::ValidationError, "重复选择了同一借阅记录"};
    Seen.insert(Rid);
  }
  for (int Rid : RenewRecordIDs) {
    if (Seen.contains(Rid))
      return {ErrorCode::ValidationError, "同一记录不能同时归还和续借"};
    Seen.insert(Rid);
  }

  // 预检归还记录
  for (int Rid : ReturnRecordIDs) {
    int BrIdx = -1;
    for (int I = 0; I < Borrows.size(); ++I) {
      if (Borrows[I].ID == Rid) {
        BrIdx = I;
        break;
      }
    }
    if (BrIdx < 0)
      return {ErrorCode::NotFound,
              "未找到借阅记录，记录ID: " + QString::number(Rid)};
    if (Borrows[BrIdx].ReturnDate.isValid())
      return {ErrorCode::InvalidStatus, "该借阅记录已归还，无法重复操作"};

    int CopyIdx = -1;
    for (int I = 0; I < Copies.size(); ++I) {
      if (Copies[I].ID == Borrows[BrIdx].CopyId) {
        CopyIdx = I;
        break;
      }
    }
    if (CopyIdx < 0)
      return {ErrorCode::InternalError, "数据不一致：借阅记录关联的副本不存在"};
    if (Copies[CopyIdx].Status == BookCopy::BS_Lost)
      return {ErrorCode::InvalidStatus,
              "该书籍处于'遗失'状态，请先办理挂失处理后再归还"};
    if (Copies[CopyIdx].Status == BookCopy::BS_NonLendable)
      return {ErrorCode::InvalidStatus, "该书籍处于'非外借书'状态，无法归还"};
    if (Copies[CopyIdx].Status == BookCopy::BS_Unkown_Status)
      return {ErrorCode::InvalidStatus, "该书籍处于'未知状态'，无法归还"};
  }

  // 预检续借记录
  for (int Rid : RenewRecordIDs) {
    int BrIdx = -1;
    for (int I = 0; I < Borrows.size(); ++I) {
      if (Borrows[I].ID == Rid) {
        BrIdx = I;
        break;
      }
    }
    if (BrIdx < 0)
      return {ErrorCode::NotFound,
              "未找到借阅记录，记录ID: " + QString::number(Rid)};
    if (Borrows[BrIdx].ReturnDate.isValid())
      return {ErrorCode::InvalidStatus, "该借阅记录已归还，无法续借"};

    int CopyIdx = -1;
    for (int I = 0; I < Copies.size(); ++I) {
      if (Copies[I].ID == Borrows[BrIdx].CopyId) {
        CopyIdx = I;
        break;
      }
    }
    if (CopyIdx < 0)
      return {ErrorCode::InternalError, "数据不一致：借阅记录关联的副本不存在"};
    if (Copies[CopyIdx].Status == BookCopy::BS_Lost)
      return {ErrorCode::InvalidStatus, "该书籍处于'遗失'状态，无法续借"};
    if (Copies[CopyIdx].Status == BookCopy::BS_NonLendable)
      return {ErrorCode::InvalidStatus, "该书籍处于'非外借书'状态，无法续借"};
    if (Copies[CopyIdx].Status == BookCopy::BS_Unkown_Status)
      return {ErrorCode::InvalidStatus, "该书籍处于'未知状态'，无法续借"};
  }

  // 全部预检通过后执行（此时不会再因状态问题失败）
  for (int Rid : ReturnRecordIDs) {
    auto Res = returnBook(Rid);
    if (!Res)
      return Res;
  }
  for (int Rid : RenewRecordIDs) {
    auto Res = renewBook(Rid);
    if (!Res)
      return Res;
  }
  return saveToXlsx();
}

// ========== 查询某读者的借阅详情（未归还的） ==========

ErrorOr<QVector<BorrowDetailType>>
LibrarySystem::getBorrowingDetailsByReader(int ReaderId) {
  QVector<BorrowDetailType> Results;
  for (const auto &Br : std::as_const(Borrows)) {
    if (Br.ReaderId != ReaderId)
      continue;
    if (Br.ReturnDate.isValid())
      continue; // 已归还的跳过

    // 查找对应 Copy 和 Info
    const BookCopy *Copy = nullptr;
    for (const auto &C : std::as_const(Copies)) {
      if (C.ID == Br.CopyId) {
        Copy = &C;
        break;
      }
    }
    if (!Copy)
      continue;
    const BookInfo *Info = nullptr;
    for (const auto &Inf : std::as_const(Infos)) {
      if (Inf.ID == Copy->InfoID) {
        Info = &Inf;
        break;
      }
    }
    if (!Info)
      continue;

    BorrowDetailType Detail;
    Detail.Record = Br;
    Detail.Copy = *Copy;
    Detail.Info = *Info;
    Results.append(Detail);
  }
  return Results;
}

// ========== 通过书籍条码查询在借记录（扫码还书用） ==========

ErrorOr<std::pair<BorrowDetailType, Reader>>
LibrarySystem::getBorrowingDetailByBarcode(const QString &Barcode) {
  if (!BarcodeToCopyIdx.contains(Barcode))
    return {ErrorCode::NotFound, "条码不存在: " + Barcode};

  const BookCopy &Copy = Copies[BarcodeToCopyIdx[Barcode]];
  if (Copy.Status != BookCopy::BS_Borrowed)
    return {ErrorCode::InvalidStatus, "该书籍当前不在'借出'状态: " + Barcode};

  const BookInfo *Info = nullptr;
  for (const auto &Inf : std::as_const(Infos)) {
    if (Inf.ID == Copy.InfoID) {
      Info = &Inf;
      break;
    }
  }
  if (!Info)
    return {ErrorCode::InternalError, "数据不一致：副本关联的书籍信息不存在"};

  // 查找未归还的借阅记录
  const BorrowRecord *Found = nullptr;
  for (const auto &Br : std::as_const(Borrows)) {
    if (Br.CopyId == Copy.ID && !Br.ReturnDate.isValid()) {
      Found = &Br;
      break;
    }
  }
  if (!Found)
    return {ErrorCode::InternalError,
            "数据不一致：书籍为'借出'状态但没有对应的在借记录"};

  const Reader *Rdr = nullptr;
  for (const auto &R : std::as_const(Readers)) {
    if (R.ID == Found->ReaderId) {
      Rdr = &R;
      break;
    }
  }
  if (!Rdr)
    return {ErrorCode::InternalError, "数据不一致：借阅记录关联的读者不存在"};

  BorrowDetailType Detail;
  Detail.Record = *Found;
  Detail.Copy = Copy;
  Detail.Info = *Info;
  return std::make_pair(Detail, *Rdr);
}

// ========== 统计读者当前未归还数量 ==========

int LibrarySystem::getActiveBorrowCount(int ReaderId) const {
  int Count = 0;
  for (const auto &Br : Borrows) {
    if (Br.ReaderId == ReaderId && !Br.ReturnDate.isValid())
      ++Count;
  }
  return Count;
}

// ========== 综合查询（条码/书名/作者/出版社，支持模糊） ==========

ErrorOr<QVector<std::pair<BorrowDetailType, Reader>>>
LibrarySystem::queryBooks(const QString &Barcode, const QString &Title,
                          const QString &Author, const QString &Publisher) {
  QVector<std::pair<BorrowDetailType, Reader>> Results;

  for (const auto &Copy : std::as_const(Copies)) {
    // 模糊匹配
    if (!Barcode.isEmpty() &&
        !Copy.Barcode.contains(Barcode, Qt::CaseInsensitive))
      continue;

    const BookInfo *Info = nullptr;
    for (const auto &Inf : std::as_const(Infos)) {
      if (Inf.ID == Copy.InfoID) {
        Info = &Inf;
        break;
      }
    }
    if (!Info)
      continue;

    if (!Title.isEmpty() && !Info->Title.contains(Title, Qt::CaseInsensitive))
      continue;
    if (!Author.isEmpty() &&
        !Info->Author.contains(Author, Qt::CaseInsensitive))
      continue;
    if (!Publisher.isEmpty() &&
        !Info->Publisher.contains(Publisher, Qt::CaseInsensitive))
      continue;

    BorrowDetailType Detail;
    Detail.Copy = Copy;
    Detail.Info = *Info;
    Detail.Record.ID = -1;

    Reader Reader;
    Reader.ID = -1;

    // 查找未归还的借阅记录及相关读者
    for (const auto &Br : std::as_const(Borrows)) {
      if (Br.CopyId == Copy.ID && !Br.ReturnDate.isValid()) {
        Detail.Record = Br;
        for (const auto &R : std::as_const(Readers)) {
          if (R.ID == Br.ReaderId) {
            Reader = R;
            break;
          }
        }
        break;
      }
    }

    Results.append(std::make_pair(Detail, Reader));
  }
  return Results;
}

// ========== 生成新读者卡号 ==========

ErrorOr<QString> LibrarySystem::getNewReaderCardID() {
  int MaxId = 2000000;
  for (const auto &R : std::as_const(Readers)) {
    bool Ok;
    int Id = R.CardNumber.toInt(&Ok);
    if (Ok && Id > MaxId)
      MaxId = Id;
  }
  return QString::number(MaxId + 1);
}

// ========== 读者管理 ==========

ErrorOr<QVector<Reader>> LibrarySystem::getAllReaders() { return Readers; }

ErrorOr<void> LibrarySystem::addReader(const QString &Name,
                                       const QString &CardNumber,
                                       const QString &PhoneNumber) {
  // 卡号唯一性检查
  if (CardToReaderIdx.contains(CardNumber))
    return {ErrorCode::ValidationError, "卡号已存在: " + CardNumber};

  Reader R;
  R.ID = NextReaderID++;
  R.Name = Name;
  R.CardNumber = CardNumber;
  R.PhoneNumber = PhoneNumber;
  R.IsInactive = false;
  Readers.append(R);
  CardToReaderIdx[CardNumber] = Readers.size() - 1;

  return saveToXlsx();
}

ErrorOr<void> LibrarySystem::updateReader(int ID, const QString &Name,
                                          const QString &CardNumber,
                                          const QString &PhoneNumber,
                                          bool IsInactive) {
  int Idx = -1;
  for (int I = 0; I < Readers.size(); ++I) {
    if (Readers[I].ID == ID) {
      Idx = I;
      break;
    }
  }
  if (Idx < 0)
    return {ErrorCode::NotFound, "读者不存在"};

  // 注销检查：有未归还图书时不允许注销
  if (IsInactive && !Readers[Idx].IsInactive) {
    for (const auto &Br : std::as_const(Borrows)) {
      if (Br.ReaderId == ID && !Br.ReturnDate.isValid())
        return {ErrorCode::InvalidStatus, "该读者有未归还的图书，无法注销"};
    }
  }

  // 卡号唯一性检查（排除自身）
  if (CardToReaderIdx.contains(CardNumber) &&
      CardToReaderIdx[CardNumber] != Idx)
    return {ErrorCode::ValidationError, "卡号已被其他读者使用: " + CardNumber};

  // 更新卡号索引
  QString OldCard = Readers[Idx].CardNumber;
  CardToReaderIdx.remove(OldCard);

  Readers[Idx].Name = Name;
  Readers[Idx].CardNumber = CardNumber;
  Readers[Idx].PhoneNumber = PhoneNumber;
  Readers[Idx].IsInactive = IsInactive;
  CardToReaderIdx[CardNumber] = Idx;

  return saveToXlsx();
}

ErrorOr<void> LibrarySystem::deleteReader(int ID) {
  // 检查是否有未归还的借阅记录
  for (const auto &Br : std::as_const(Borrows)) {
    if (Br.ReaderId == ID && !Br.ReturnDate.isValid())
      return {ErrorCode::InvalidStatus, "该读者有未归还的图书，无法删除"};
  }

  int Idx = -1;
  for (int I = 0; I < Readers.size(); ++I) {
    if (Readers[I].ID == ID) {
      Idx = I;
      break;
    }
  }
  if (Idx < 0)
    return {ErrorCode::NotFound, "读者不存在"};

  CardToReaderIdx.remove(Readers[Idx].CardNumber);
  Readers.removeAt(Idx);

  return saveToXlsx();
}

// ========== 催还查询 ==========

ErrorOr<QVector<LibrarySystem::ReaderBorrowInfo>>
LibrarySystem::getRemindBorrowings(int Days) {
  QMap<int, ReaderBorrowInfo> ReaderMap;
  QDate Today = QDate::currentDate();
  QDate Deadline = Today.addDays(Days);

  for (const auto &Br : std::as_const(Borrows)) {
    if (Br.ReturnDate.isValid())
      continue;

    // 查找 Copy 和 Info
    const BookCopy *Copy = nullptr;
    for (const auto &C : std::as_const(Copies)) {
      if (C.ID == Br.CopyId) {
        Copy = &C;
        break;
      }
    }
    if (!Copy)
      continue;
    const BookInfo *Info = nullptr;
    for (const auto &Inf : std::as_const(Infos)) {
      if (Inf.ID == Copy->InfoID) {
        Info = &Inf;
        break;
      }
    }
    if (!Info)
      continue;

    // 查找 Reader
    const Reader *Reader = nullptr;
    for (const auto &R : std::as_const(Readers)) {
      if (R.ID == Br.ReaderId) {
        Reader = &R;
        break;
      }
    }
    if (!Reader)
      continue;

    if (!ReaderMap.contains(Reader->ID)) {
      ReaderBorrowInfo Rbi;
      Rbi.Reader = *Reader;
      ReaderMap[Reader->ID] = Rbi;
    }

    BorrowDetailType Detail;
    Detail.Record = Br;
    Detail.Copy = *Copy;
    Detail.Info = *Info;

    QDate DueDate = Br.DueDate.date();
    if (DueDate <= Deadline)
      ReaderMap[Reader->ID].UrgentBooks.append(Detail);
    else
      ReaderMap[Reader->ID].OtherBooks.append(Detail);
  }

  QVector<ReaderBorrowInfo> Results;
  for (auto &Info : ReaderMap) {
    if (!Info.UrgentBooks.isEmpty())
      Results.append(Info);
  }
  return Results;
}
