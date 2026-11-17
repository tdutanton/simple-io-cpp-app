#include "BatchFrameProcessor.h"

#include <QElapsedTimer>
#include <QThread>

#include "FrameCodec.h"
#include "SpscChannel.h"

namespace {
// Сколько кадров максимум забираем за одно пробуждение. Ограничение нужно,
// чтобы под лавиной не застрять в «разборе пачки» на секунды.
constexpr quint32 kMaxBatch = 64;

// Как часто проверять флаг остановки, если данных нет (мс).
constexpr int kIdleCheckMs = 100;
}  // namespace

BatchFrameProcessor::BatchFrameProcessor(SpscChannel* channel, QObject* parent)
    : QObject(parent)
    , m_channel(channel)
{
}

void BatchFrameProcessor::requestStop()
{
    m_stop.store(true, std::memory_order_relaxed);
    m_channel->ready.release();  // разбудить, если спим на tryAcquire
}

void BatchFrameProcessor::setMode(int mode)
{
    m_mode.store(mode, std::memory_order_relaxed);
}

void BatchFrameProcessor::setDelayMs(int ms)
{
    m_delayMs.store(ms, std::memory_order_relaxed);
}

void BatchFrameProcessor::run()
{
    proto::VideoFrame frame;

    while (!m_stop.load(std::memory_order_relaxed)) {
        // Блокирующее ожидание первого элемента. Таймаут — только чтобы
        // замечать requestStop(), когда данных нет вовсе.
        if (!m_channel->ready.tryAcquire(1, kIdleCheckMs))
            continue;

        if (!m_channel->queue.tryPop(frame))
            continue;  // токен был, а данных уже нет — не наш случай, но безопасно

        // Добираем всё, что успело накопиться. Это и есть батчинг: на пачку
        // приходится одно пробуждение и один проход по циклу.
        quint32 batch = 1;
        proto::VideoFrame next;
        while (batch < kMaxBatch && m_channel->ready.tryAcquire()) {
            if (!m_channel->queue.tryPop(next))
                break;
            frame = std::move(next);  // нужен самый свежий кадр, не самый старый
            ++batch;
        }

        // Политика «оставить последний»: промежуточные кадры уже устарели,
        // показывать их нет смысла. Это осознанный выбор, а не потеря.
        if (batch > 1)
            m_channel->stats.coalesced.fetch_add(batch - 1, std::memory_order_relaxed);
        m_channel->stats.batches.fetch_add(1, std::memory_order_relaxed);
        m_channel->stats.lastBatch.store(batch, std::memory_order_relaxed);
        if (batch > m_channel->stats.maxBatch.load(std::memory_order_relaxed))
            m_channel->stats.maxBatch.store(batch, std::memory_order_relaxed);

        QElapsedTimer timer;
        timer.start();

        QImage image = codec::decodeJpeg(frame.encoded);
        if (image.isNull()) {
            emit log(QStringLiteral("Кадр %1: не удалось декодировать JPEG").arg(frame.id));
            continue;
        }

        const int delay = m_delayMs.load(std::memory_order_relaxed);
        if (delay > 0)
            QThread::msleep(quint32(delay));

        if (m_mode.load(std::memory_order_relaxed) == 1)
            image = codec::sobel(image);

        const double ms = double(timer.nsecsElapsed()) / 1e6;
        emit frameReady(image, frame.id, frame.senderTimestampMs, ms);
    }
}
