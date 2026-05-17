#ifndef LIBRARY_H
#define LIBRARY_H

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

enum class ErrorCode {
  Success = 0,
  DatabaseError,   // 数据操作失败
  NotFound,        // 条码、读者等不存在
  InvalidStatus,   // 书籍状态不支持该操作（如已借出不能再借）
  ValidationError, // 输入数据格式错误
  InternalError    // 其他系统内部错误
};

template <typename T> class ErrorOr {
public:
  ErrorOr(const T &Val) : Value(Val), ErrCode(ErrorCode::Success) {}
  ErrorOr(ErrorCode Code, const QString &Msg) : ErrCode(Code), ErrMsg(Msg) {}

  bool isOK() const { return ErrCode == ErrorCode::Success; }
  ErrorCode getErrCode() const { return ErrCode; }
  QString getErrMsg() const { return ErrMsg; }
  const T &getValue() const {
    Q_ASSERT(isOK());
    return Value;
  }
  T &getValue() {
    Q_ASSERT(isOK());
    return Value;
  }
  explicit operator bool() const { return isOK(); }

private:
  T Value{};
  ErrorCode ErrCode;
  QString ErrMsg;
};

template <> class ErrorOr<void> {
public:
  ErrorOr() : ErrCode(ErrorCode::Success) {}
  ErrorOr(ErrorCode Code, const QString &Msg) : ErrCode(Code), ErrMsg(Msg) {}

  bool isOK() const { return ErrCode == ErrorCode::Success; }
  ErrorCode getErrCode() const { return ErrCode; }
  QString getErrMsg() const { return ErrMsg; }
  explicit operator bool() const { return isOK(); }

private:
  ErrorCode ErrCode;
  QString ErrMsg;
};

struct BookInfo {
  int ID = -1;
  QString Title;
  QString Author;
  QString Publisher;
  QString CoverPath;
};

struct BookCopy {
  enum BookStatus { BS_InLibrary = 0, BS_Borrowed, BS_Lost, BS_NonLendable };
  int ID = -1;
  int InfoID = -1;
  QString Barcode;
  BookStatus Status = BS_InLibrary;
};

struct Reader {
  int ID = -1;
  QString Name;
  QString CardNumber;
  QString PhoneNumber;
  bool IsInactive = false;
};

struct BorrowRecord {
  int ID = -1;
  int ReaderId = -1;
  int CopyId = -1;
  QDateTime BorrowDate;
  QDateTime DueDate;
  QDateTime ReturnDate;
};

struct BorrowDetailType {
  BorrowRecord Record;
  BookCopy Copy;
  BookInfo Info;
};

class LibrarySystem : public QObject {
  Q_OBJECT

public:
  static LibrarySystem &getInstance() {
    static LibrarySystem Instance;
    return Instance;
  }

  LibrarySystem(const LibrarySystem &) = delete;
  LibrarySystem &operator=(const LibrarySystem &) = delete;

  ErrorOr<void> init(const QString &XlsxPath);
  ErrorOr<void> borrowBooks(int ReaderID, const QVector<int> &CopyIDs);
  ErrorOr<void> returnAndRenewBooks(const QList<int> &ReturnRecordIDs,
                                    const QList<int> &RenewRecordIDs);
  ErrorOr<std::pair<BookInfo, BookCopy>>
  getBookDataByBarcode(const QString &Barcode);
  ErrorOr<Reader> getReaderByCardNumber(const QString &CardNumber);
  ErrorOr<QVector<BorrowDetailType>> getBorrowingDetailsByReader(int ReaderId);
  ErrorOr<QVector<std::pair<BorrowDetailType, Reader>>>
  queryBooks(const QString &Barcode, const QString &Title,
             const QString &Author, const QString &Publisher);
  ErrorOr<QString> getNewReaderCardID();
  ErrorOr<QVector<Reader>> getAllReaders();
  ErrorOr<void> addReader(const QString &Name, const QString &CardNumber,
                          const QString &PhoneNumber);
  ErrorOr<void> updateReader(int ID, const QString &Name,
                             const QString &CardNumber,
                             const QString &PhoneNumber);

  struct ReaderBorrowInfo {
    Reader reader;
    QVector<BorrowDetailType> urgentBooks;
    QVector<BorrowDetailType> otherBooks;
  };
  ErrorOr<QVector<ReaderBorrowInfo>> getRemindBorrowings(int Days);

  QString getDataDir() const { return DataDir; }

private:
  LibrarySystem() = default;

  // ---- xlsx 文件操作 ----
  ErrorOr<void> loadFromXlsx();
  ErrorOr<void> saveToXlsx();
  ErrorOr<void> lockXlsxFile(const QString &Path);
  void unlockXlsxFile();

  // ---- 事务相关 private 方法（内存操作，不再需要 QSqlQuery） ----
  ErrorOr<void> borrowBook(int ReaderID, int CopyID);
  ErrorOr<void> returnBook(int RecordID);
  ErrorOr<void> renewBook(int RecordID);

  // ---- 内存数据存储 ----
  QVector<BookInfo> Infos;
  QVector<BookCopy> Copies;
  QVector<Reader> Readers;
  QVector<BorrowRecord> Borrows;

  // 查找索引：条码 -> Copies 下标，卡号 -> Readers 下标
  QHash<QString, int> BarcodeToCopyIdx;
  QHash<QString, int> CardToReaderIdx;
  QHash<QString, QString> BarcodeNotes; // 条码 -> 备注，保存时写回

  int NextInfoID = 1;
  int NextCopyID = 1;
  int NextReaderID = 1;
  int NextBorrowID = 1;

  QString XlsxPath;
  QString DataDir;

#ifdef Q_OS_WIN
  void *LockHandle = nullptr; // HANDLE
#endif
};

#endif // LIBRARY_H
