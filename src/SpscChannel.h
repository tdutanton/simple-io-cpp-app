#pragma once

#include <QSemaphore>

#include <atomic>

#include "Protocol.h"
#include "SpscQueue.h"

// Счётчики транспорта. Атомики, потому что читает их поток GUI, а пишут
// producer и consumer — каждый из своего потока.
struct SpscStats {
    std::atomic<quint64> pushed{0};       // кадров положено в очередь
    std::atomic<quint64> droppedFull{0};  // не влезло — очередь была полна
    std::atomic<quint64> batches{0};      // сколько раз потребитель просыпался
    std::atomic<quint64> coalesced{0};    // кадров пропущено как устаревшие
    std::atomic<quint32> lastBatch{0};
    std::atomic<quint32> maxBatch{0};
};

// ---------------------------------------------------------------------------
// SpscChannel — «труба» между потоком сети и потоком обработки.
//
// Состоит из трёх частей:
//   1) сама очередь (данные);
//   2) семафор (сигнал «есть работа») — одно пробуждение на пачку;
//   3) статистика.
//
// Обратите внимание: семафор здесь не несёт данных, только факт «проснись».
// Это принципиально: уведомление дешёвое, а данные едут через кольцо.
// ---------------------------------------------------------------------------
struct SpscChannel {
    // Ёмкость 8: при 25-60 кадрах/с это 130-300 мс буфера. Больше не нужно —
    // иначе растёт задержка, а для живого видео она важнее полноты.
    static constexpr std::size_t kCapacity = 8;

    SpscQueue<proto::VideoFrame, kCapacity> queue;
    QSemaphore ready;
    SpscStats stats;
};
