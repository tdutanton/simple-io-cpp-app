#pragma once

#include <QObject>

#include "Protocol.h"

struct SpscChannel;

// ---------------------------------------------------------------------------
// FrameQueueProducer — производственная сторона SPSC.
//
// Живёт в потоке сети. UdpReceiver отдаёт ему кадр обычным сигналом, но так
// как оба объекта в одном потоке, соединение прямое — это простой вызов
// функции, без очереди событий.
//
// Задача класса: положить кадр в кольцо и, если потребитель ждёт, разбудить
// его. Ровно один producer — это и есть требование SPSC.
// ---------------------------------------------------------------------------
class FrameQueueProducer : public QObject {
    Q_OBJECT

public:
    explicit FrameQueueProducer(SpscChannel* channel, QObject* parent = nullptr);

public slots:
    void onFrame(const proto::VideoFrame& frame);

private:
    SpscChannel* m_channel;
};
