#ifndef TSVPARSER_H
#define TSVPARSER_H

#include "Library.h"

#include <QFile>
#include <QStringList>
#include <QTextStream>
#include <QVector>

// TSV（制表符分隔）文件格式示例：
// 书名	作者	出版社	数量	图片名	条码号
// 斯大林全集第八卷		人民出版社	2	1-21	1000002100
// 斯大林全集第八卷		人民出版社	2	1-21	1000002101
class TSVParser {
public:
  struct RawData {
    QString ImageName; // 封面图片文件名，完整路径为 ImageName.jpg
    QString Title;
    QString Author;
    QString Publisher;
    int Count;
    QStringList Barcodes;
    RawData(QString ImageName, QString Title, QString Author, QString Publisher,
            int Count, QStringList Barcodes)
        : ImageName(ImageName), Title(Title), Author(Author),
          Publisher(Publisher), Count(Count), Barcodes(Barcodes) {}
  };

  ErrorOr<void> parse(const QString &Path);

private:
  QVector<RawData> Results;
  friend class LibrarySystem;
};

#endif // TSVPARSER_H
