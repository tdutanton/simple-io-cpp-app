#include "FrameProcessor.h"

#include <QElapsedTimer>

#include <cstdlib>

FrameProcessor::FrameProcessor(QObject* parent)
    : QObject(parent)
{
}

void FrameProcessor::setMode(int mode)
{
    m_mode = mode;
}

void FrameProcessor::processFrame(const proto::VideoFrame& frame)
{
    QElapsedTimer timer;
    timer.start();

    // Декодирование JPEG — самая дорогая операция на кадр.
    QImage image = QImage::fromData(frame.encoded, "JPG");
    if (image.isNull()) {
        emit log(QStringLiteral("Кадр %1: не удалось декодировать JPEG").arg(frame.id));
        emit frameProcessed(frame.id);  // обязательно отвечаем, иначе приём встанет
        return;
    }

    if (m_mode == Edges)
        image = sobel(image);

    const double ms = double(timer.nsecsElapsed()) / 1e6;
    emit frameReady(image, frame.id, frame.senderTimestampMs, ms);
    emit frameProcessed(frame.id);
}

// Простой Sobel. Смысл не в качестве фильтра, а в том, что это честная
// нагрузка на CPU: ~200 тысяч пикселей на кадр.
QImage FrameProcessor::sobel(const QImage& src)
{
    const QImage gray = src.convertToFormat(QImage::Format_Grayscale8);
    const int w = gray.width();
    const int h = gray.height();
    if (w < 3 || h < 3)
        return gray;

    QImage out(w, h, QImage::Format_Grayscale8);
    for (int y = 1; y < h - 1; ++y) {
        const uchar* r0 = gray.constScanLine(y - 1);
        const uchar* r1 = gray.constScanLine(y);
        const uchar* r2 = gray.constScanLine(y + 1);
        uchar* dst = out.scanLine(y);

        for (int x = 1; x < w - 1; ++x) {
            const int gx = -int(r0[x - 1]) + int(r0[x + 1]) - 2 * int(r1[x - 1])
                           + 2 * int(r1[x + 1]) - int(r2[x - 1]) + int(r2[x + 1]);
            const int gy = -int(r0[x - 1]) - 2 * int(r0[x]) - int(r0[x + 1])
                           + int(r2[x - 1]) + 2 * int(r2[x]) + int(r2[x + 1]);
            const int v = std::abs(gx) + std::abs(gy);
            dst[x] = uchar(v > 255 ? 255 : v);
        }
    }

    // Края отдельно: у них нет полных соседей.
    for (int x = 0; x < w; ++x) {
        out.scanLine(0)[x] = 0;
        out.scanLine(h - 1)[x] = 0;
    }
    for (int y = 0; y < h; ++y) {
        out.scanLine(y)[0] = 0;
        out.scanLine(y)[w - 1] = 0;
    }
    return out;
}
