#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QVector>

#include "Protocol.h"

class QUdpSocket;
class QTimer;

// ---------------------------------------------------------------------------
// UdpReceiver — всё, что связано с сетью: сокет, разбор пакетов, склейка
// видеокадров из фрагментов, счётчики потерь.
//
// Живёт в отдельном потоке (см. MainWindow::startThreads). Внутри QObject'а
// нет ни одного обращения к виджетам: наружу торчат только сигналы.
// ---------------------------------------------------------------------------
class UdpReceiver : public QObject {
    Q_OBJECT

public:
    explicit UdpReceiver(quint16 port, QObject* parent = nullptr);
    ~UdpReceiver() override;

    // Два способа ограничить поток кадров:
    //   true  (по умолчанию, сигнальный вариант) — ждём подтверждения от
    //         обработчика, пока его нет новые кадры отбрасываются;
    //   false (SPSC-вариант) — не ждём ничего: backpressure обеспечивает
    //         кольцевой буфер, который просто не примет кадр, когда полон.
    // Вызывать до старта потока приёмника.
    void setRequireAck(bool on);

public slots:
    // Вызывается уже в потоке приёмника (Qt::QueuedConnection от QThread::started).
    void start();
    void stop();

    // Подтверждение от обработчика: кадр принят в работу. Пока подтверждения
    // нет, новые кадры отбрасываются — это и есть backpressure, защита от
    // бесконечной очереди, когда обработка не успевает за сетью.
    void onFrameProcessed(quint32 frameId);

signals:
    void telemetryReceived(const proto::Telemetry& t);
    void frameReceived(const proto::VideoFrame& frame);
    void statsUpdated(const proto::RxStats& s);
    void log(const QString& msg);

private slots:
    void onReadyRead();
    void onSweep();
    void onStatsTimer();

private:
    struct Assembly {
        QVector<QByteArray> parts;  // parts.isEmpty() == "ещё не создан"
        int got = 0;
        QElapsedTimer age;          // когда пришёл первый фрагмент
    };

    void handleDatagram(const char* data, qint64 len);
    void handleTelemetry(const proto::Header& h, const QByteArray& payload);
    void handleVideoFragment(const proto::Header& h, const QByteArray& payload);
    void deliverFrame(quint32 frameId);

    quint16 m_port;
    QUdpSocket* m_socket = nullptr;
    QTimer* m_sweepTimer = nullptr;
    QTimer* m_statsTimer = nullptr;

    QByteArray m_buffer;  // переиспользуемый буфер под дейтаграмму
    QHash<quint32, Assembly> m_assemblies;
    quint32 m_lastDeliveredFrame = 0;

    // Backpressure: ждём подтверждения от обработчика не дольше kWaitAckMs.
    bool m_waitingForProcessor = false;
    bool m_requireAck = true;
    QElapsedTimer m_waitClock;

    // Счётчики
    quint64 m_packets = 0;
    quint64 m_framesOk = 0;
    quint64 m_framesLost = 0;
    quint64 m_framesDroppedBusy = 0;
    quint64 m_lateFragments = 0;
    quint64 m_badPackets = 0;
    quint64 m_packetsAtLastStat = 0;
    QElapsedTimer m_statClock;
};
