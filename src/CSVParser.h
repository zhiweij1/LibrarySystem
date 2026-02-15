#ifndef CSVPARSER_H
#define CSVPARSER_H

#include "Library.h"

#include <QFile>
#include <QStringList>
#include <QTextStream>
#include <QVector>

// csv file format example:
// 编号,书名,作者,出版社,数量,分类号,条形码
// 1-193,丁光训文集,丁光训,译林出版社,4,B978,0000000:0000001:0000002:0000003
// 1-194,丁光训文集精装,丁光训,译林出版社,1,B978,0000004
class CSVParser {
public:
  struct RawData {
    QString CSVID; // ID for cover image file name. Filename is CSVID.jpg
    QString Title;
    QString Author;
    QString Publisher;
    int Count;
    QString Category;
    QStringList Barcodes;
    RawData(QString CSVID, QString Title, QString Author, QString Publisher,
            int Count, QString Category, QStringList Barcodes)
        : CSVID(CSVID), Title(Title), Author(Author), Publisher(Publisher),
          Count(Count), Category(Category), Barcodes(Barcodes) {}
  };

  ErrorOr<void> parse(const QString &Path);

private:
  QVector<RawData> Results;
  friend class LibrarySystem;
};

#endif // CSVPARSER_H
