#pragma once

#include <QImage>
#include <QObject>

#include <atomic>

#include "Protocol.h"

struct SpscChannel;

// ---------------------------------------------------------------------------
// BatchFrameProcessor — потребительская сторона SPSC.
//
// В отличие от FrameProcessor (сигнальный вариант) этот класс не живёт на
// цикле событий Qt: он запускает собственный цикл в своём потоке и блокируется
// на семафоре, когда работы нет. Так в горячем пути нет ни очереди событий,
// ни аллокации события на кадр, ни диспетчеризации по мета-системе.
//
// Управление (режим обработки) при этом не идёт через очередь: это атомик,
// который поток GUI меняет напрямую. Данные — отдельно, управление — отдельно.
// ---------------------------------------------------------------------------
class BatchFrameProcessor : public QObject {
    Q_OBJECT

public:
    explicit BatchFrameProcessor(SpscChannel* channel, QObject* parent = nullptr);

    // Можно вызывать из любого потока: поднимает флаг и будит цикл.
    void requestStop();
    void setMode(int mode);
    // Искусственная задержка обработки (мс) — только для демонстрации
    // перегрузки: при ней включается батчинг и видно, как ведёт себя очередь.
    void setDelayMs(int ms);

public slots:
    void run();  // занимает поток целиком до requestStop()

signals:
    void frameReady(const QImage& image, quint32 frameId, qint64 senderTimestampMs,
                    double processMs);
    void log(const QString& msg);

private:
    SpscChannel* m_channel;
    std::atomic<bool> m_stop{false};
    std::atomic<int> m_mode{0};     // 0 — только декод, 1 — с Sobel
    std::atomic<int> m_delayMs{0};  // искусственная задержка, только для демо
};
