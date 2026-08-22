#ifndef COVERPREVIEW_H
#define COVERPREVIEW_H

#include "Library.h"

#include <QDialog>
#include <QEvent>
#include <QImageReader>
#include <QLabel>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QTimer>
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

// 封面大图预览对话框。
// 普通模态对话框被最小化（如 Win+D "显示桌面"）后会从屏幕上消失，
// 但 exec() 仍在运行并锁死整个程序。此对话框在检测到最小化时立即恢复
// 显示，并提供显式关闭按钮，避免程序"假死"。
class CoverPreviewDialog : public QDialog {
public:
  explicit CoverPreviewDialog(const QPixmap &Pix, QWidget *Parent = nullptr)
      : QDialog(Parent) {
    setWindowTitle("封面预览");
    setModal(true);
    // 不显示最小化按钮，降低被意外隐藏的概率
    setWindowFlag(Qt::WindowMinimizeButtonHint, false);

    QVBoxLayout *Layout = new QVBoxLayout(this);
    Layout->setContentsMargins(10, 10, 10, 10);
    QLabel *ImgLabel = new QLabel(this);
    ImgLabel->setPixmap(
        Pix.scaled(600, 800, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ImgLabel->setAlignment(Qt::AlignCenter);
    Layout->addWidget(ImgLabel);

    // 显式关闭按钮：提供明确的退出途径
    QPushButton *CloseBtn = new QPushButton("关闭", this);
    connect(CloseBtn, &QPushButton::clicked, this, &QDialog::accept);
    Layout->addWidget(CloseBtn, 0, Qt::AlignHCenter);
  }

protected:
  void changeEvent(QEvent *Event) override {
    QDialog::changeEvent(Event);
    if (Event->type() == QEvent::WindowStateChange &&
        (windowState() & Qt::WindowMinimized)) {
      // 被最小化（如 Win+D）时立即恢复，避免模态窗口消失导致程序假死
      QTimer::singleShot(0, this, [this]() {
        showNormal();
        raise();
        activateWindow();
      });
    }
  }
};

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
        CoverPreviewDialog Dlg(Pix, this->window());
        Dlg.exec();
      }
    }
    QLabel::mousePressEvent(event);
  }

private:
  QString CoverPath;
};

#endif // COVERPREVIEW_H
