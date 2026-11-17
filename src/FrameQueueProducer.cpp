#include "FrameQueueProducer.h"

#include "SpscChannel.h"

FrameQueueProducer::FrameQueueProducer(SpscChannel* channel, QObject* parent)
    : QObject(parent)
    , m_channel(channel)
{
}

void FrameQueueProducer::onFrame(const proto::VideoFrame& frame)
{
    if (!m_channel->queue.tryPush(frame)) {
        // Очередь полна — потребитель не успевает. Для видео правильно
        // выбросить новый кадр, а не ждать: свежести это не добавит, зато
        // задержка не поползёт вверх.
        m_channel->stats.droppedFull.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    m_channel->stats.pushed.fetch_add(1, std::memory_order_relaxed);

    // Будим потребителя. Один release на кадр; потребитель забирает пачку
    // по одному токену на элемент, поэтому счётчик семафора остаётся
    // согласован с содержимым очереди.
    m_channel->ready.release();
}
