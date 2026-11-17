#include "FrameProcessor.h"

#include <QElapsedTimer>
#include <QThread>

#include "FrameCodec.h"

FrameProcessor::FrameProcessor(QObject* parent)
    : QObject(parent)
{
}

void FrameProcessor::setMode(int mode)
{
    m_mode = mode;
}

void FrameProcessor::setDelayMs(int ms)
{
    m_delayMs = ms;
}

void FrameProcessor::processFrame(const proto::VideoFrame& frame)
{
    QElapsedTimer timer;
    timer.start();

    // Декодирование JPEG — самая дорогая операция на кадр.
    QImage image = codec::decodeJpeg(frame.encoded);
    if (image.isNull()) {
        emit log(QStringLiteral("Кадр %1: не удалось декодировать JPEG").arg(frame.id));
        emit frameProcessed(frame.id);  // обязательно отвечаем, иначе приём встанет
        return;
    }

    if (m_delayMs > 0)
        QThread::msleep(quint32(m_delayMs));

    if (m_mode == Edges)
        image = codec::sobel(image);

    const double ms = double(timer.nsecsElapsed()) / 1e6;
    emit frameReady(image, frame.id, frame.senderTimestampMs, ms);
    emit frameProcessed(frame.id);
}
