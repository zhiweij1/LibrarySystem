#ifndef LIBRARY_H
#define LIBRARY_H

#include <QDateTime>
#include <QObject>
#include <QSqlDatabase>
#include <QString>

enum class ErrorCode {
  Success = 0,
  DatabaseError,   // 数据库操作失败
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
  const T &getValue() const { return Value; }
  T &getValue() { return Value; }
  operator bool() const { return isOK(); }

private:
  T Value;
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
  operator bool() const { return isOK(); }

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
  enum BookStatus { BS_InLibrary = 0, BS_Borrowed, BS_Lost };
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

enum ReaderStatus {
  RS_Active = 0,
  RS_InActive = 1,
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

  ErrorOr<void> init(const QString &DBPath = "library.db");
  ErrorOr<bool> isBarcodeExists(const QString &Barcode);
  ErrorOr<void> importFromCSV(const QString &FilePath);
  ErrorOr<void> borrowBooks(int ReaderID, const QVector<int> &CopyIDs);
  ErrorOr<void> returnBooks(const QList<int> &RecordIDs);
  ErrorOr<void> renewBooks(const QList<int> &RecordIDs);
  ErrorOr<std::pair<BookInfo, BookCopy>>
  getBookDataByBarcode(const QString &Barcode);
  ErrorOr<Reader> getReaderByCardNumber(const QString &CardNumber);
  ErrorOr<QVector<BorrowDetailType>> getBorrowingDetailsByReader(int ReaderId);
  ErrorOr<QVector<std::pair<BorrowDetailType, Reader>>>
  queryBooks(const QString &Barcode, const QString &Title,
             const QString &Author, const QString &Publisher);
  ErrorOr<QSet<QString>> checkExistingBarcodes(const QSet<QString> &Barcodes);
  ErrorOr<QString> getNewReaderCardID();

private:
  LibrarySystem() = default;
  ErrorOr<void> borrowBook(QSqlQuery &Query, const int ReaderID,
                           const int CopyID);
  ErrorOr<void> returnBook(QSqlQuery &Query, const int RecordID);
  ErrorOr<void> renewBook(QSqlQuery &Query, const int RecordID);

  QSqlDatabase DB;
};

#endif // LIBRARY_H
