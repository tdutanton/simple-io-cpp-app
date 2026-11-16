#pragma once

#include <QImage>
#include <QObject>

#include "Protocol.h"

// ---------------------------------------------------------------------------
// FrameProcessor — «тяжёлая» часть: декодирование JPEG и обработка картинки.
//
// Живёт в своём потоке. Причины две:
//   1) декодирование и фильтры — это миллисекунды процессорного времени,
//      которые не должны попадать в поток интерфейса (иначе окно «дёргается»);
//   2) пока идёт обработка, приёмник продолжает читать сокет и не теряет пакеты.
//
// Обработка сделана переключаемой, чтобы было видно: с выключенной фильтр
// почти не занимает CPU, с включённым (Sobel) — занимает, но окно остаётся
// отзывчивым. Это и есть наглядный ответ на вопрос «а зачем отдельный поток».
// ---------------------------------------------------------------------------
class FrameProcessor : public QObject {
    Q_OBJECT

public:
    enum Mode {
        Passthrough = 0,  // только декодировать
        Edges = 1,        // + Sobel (детектор границ)
    };

    explicit FrameProcessor(QObject* parent = nullptr);

public slots:
    void setMode(int mode);
    void processFrame(const proto::VideoFrame& frame);

signals:
    // Картинка уже готова к показу. QImage неявно разделяемый: передача между
    // потоками дешёвая (копируется только заголовок, данные — по требованию).
    void frameReady(const QImage& image, quint32 frameId, qint64 senderTimestampMs,
                    double processMs);
    // Подтверждение приёмнику, что кадр обработан (или отброшен). Снимает
    // backpressure в UdpReceiver.
    void frameProcessed(quint32 frameId);
    void log(const QString& msg);

private:
    static QImage sobel(const QImage& src);

    int m_mode = Passthrough;
};
