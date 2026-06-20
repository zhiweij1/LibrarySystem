#ifndef COVERPREVIEW_H
#define COVERPREVIEW_H

#include "Library.h"

#include <QDialog>
#include <QImageReader>
#include <QLabel>
#include <QMouseEvent>
#include <QPixmap>
#include <QVBoxLayout>

static inline QPixmap loadPixmapWithExif(const QString &Path) {
  QImageReader Reader(Path);
  Reader.setAutoTransform(true); // 自动处理 EXIF 方向
  QImage Img = Reader.read();
  if (Img.isNull()) {
    return QPixmap();
  }
  return QPixmap::fromImage(Img);
}

class CoverPreviewLabel : public QLabel {
public:
  explicit CoverPreviewLabel(const QString &CoverPath, int ThumbWidth,
                             int ThumbHeight, QWidget *Parent = nullptr)
      : QLabel(Parent), CoverPath(CoverPath) {
    QString FullPath =
        LibrarySystem::getInstance().getDataDir() + "/" + CoverPath;
    if (CoverPath.isEmpty() ||
        LibrarySystem::getInstance().getDataDir().isEmpty()) {
      setText("无封面");
      setStyleSheet("background-color: #E0E0E0; color: #999; font-size: 11px;");
      setAlignment(Qt::AlignCenter);
      return;
    }
    QPixmap Pix = loadPixmapWithExif(FullPath);
    if (Pix.isNull()) {
      setText("无封面");
      setStyleSheet("background-color: #E0E0E0; color: #999; font-size: 11px;");
    } else {
      setPixmap(Pix.scaled(ThumbWidth, ThumbHeight, Qt::KeepAspectRatio,
                           Qt::SmoothTransformation));
      setCursor(Qt::PointingHandCursor);
    }
    setAlignment(Qt::AlignCenter);
  }

  QString getCoverPath() const { return CoverPath; }

protected:
  void mousePressEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton) {
      QString FullPath =
          LibrarySystem::getInstance().getDataDir() + "/" + CoverPath;
      QPixmap Pix = loadPixmapWithExif(FullPath);
      if (!Pix.isNull()) {
        QDialog Dlg(this->window());
        Dlg.setWindowTitle("封面预览");
        Dlg.setModal(true);
        QVBoxLayout *Layout = new QVBoxLayout(&Dlg);
        Layout->setContentsMargins(10, 10, 10, 10);
        QLabel *ImgLabel = new QLabel(&Dlg);
        ImgLabel->setPixmap(Pix.scaled(600, 800, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation));
        ImgLabel->setAlignment(Qt::AlignCenter);
        Layout->addWidget(ImgLabel);
        Dlg.exec();
      }
    }
    QLabel::mousePressEvent(event);
  }

private:
  QString CoverPath;
};

#endif // COVERPREVIEW_H
