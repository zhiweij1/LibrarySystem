#ifndef COVERPREVIEW_H
#define COVERPREVIEW_H

#include <QCoreApplication>
#include <QDialog>
#include <QLabel>
#include <QMouseEvent>
#include <QPixmap>
#include <QVBoxLayout>

class CoverPreviewLabel : public QLabel {
public:
  explicit CoverPreviewLabel(const QString &coverPath, int thumbWidth,
                             int thumbHeight, QWidget *parent = nullptr)
      : QLabel(parent), CoverPath(coverPath) {
    QString FullPath = QCoreApplication::applicationDirPath() + "/" + coverPath;
    QPixmap Pix(FullPath);
    if (Pix.isNull()) {
      setText("无封面");
      setStyleSheet(
          "background-color: #E0E0E0; color: #999; font-size: 11px;");
    } else {
      setPixmap(Pix.scaled(thumbWidth, thumbHeight, Qt::KeepAspectRatio,
                           Qt::SmoothTransformation));
      setCursor(Qt::PointingHandCursor);
    }
    setAlignment(Qt::AlignCenter);
  }

  QString coverPath() const { return CoverPath; }

protected:
  void mousePressEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton) {
      QString FullPath = QCoreApplication::applicationDirPath() + "/" + CoverPath;
      QPixmap Pix(FullPath);
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
