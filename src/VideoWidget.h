#pragma once

#include <QImage>
#include <QPixmap>
#include <QWidget>

// ---------------------------------------------------------------------------
// VideoWidget — только показ. Никакой сети и никакой обработки здесь нет.
//
// Ключевое правило Qt: с виджетами (QWidget, QPixmap) можно работать только
// из потока GUI. Поэтому QImage (это данные, а не виджет) спокойно приезжает
// из потока обработки, а в QPixmap он превращается уже здесь.
// ---------------------------------------------------------------------------
class VideoWidget : public QWidget {
    Q_OBJECT

public:
    explicit VideoWidget(QWidget* parent = nullptr);

    // Вызывать только из потока GUI (сигналом из FrameProcessor).
    void setFrame(const QImage& image, quint32 frameId, qint64 senderTimestampMs);
    void clearFrame();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage m_image;
    QPixmap m_scaledCache;   // результат масштабирования под текущий размер окна
    QSize m_cacheSize;
    quint32 m_frameId = 0;
    qint64 m_senderTs = 0;
};
