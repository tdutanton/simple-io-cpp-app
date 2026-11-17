#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QMainWindow>

#include "Protocol.h"

class QCheckBox;
class QLabel;
class QThread;
class QTimer;
class UdpReceiver;
class FrameProcessor;
class BatchFrameProcessor;
class FrameQueueProducer;
class VideoWidget;
struct SpscChannel;

// Как кадр едет из потока сети в поток обработки.
enum class Transport {
    Signals,  // сигнал + QueuedConnection (вариант 1, «канонический» Qt)
    Spsc,     // кольцевой буфер SPSC + семафор + батчинг (вариант 2)
};

// ---------------------------------------------------------------------------
// MainWindow — поток GUI. Здесь только виджеты и «склейка»: он создаёт
// рабочие потоки, соединяет сигналы и обновляет интерфейс.
//
// Вариант 1 (Transport::Signals):
//
//   [поток сети]  UdpReceiver
//        |  frameReceived (QueuedConnection: событие + копия аргументов)
//        v
//   [поток обработки] FrameProcessor (слот на цикле событий Qt)
//        |  frameReady (QueuedConnection)
//        v
//   [поток GUI]  MainWindow / VideoWidget
//
// Вариант 2 (Transport::Spsc):
//
//   [поток сети]  UdpReceiver --(прямое соединение, обычный вызов)-->
//                 FrameQueueProducer --> SpscQueue (без блокировок)
//                                              |  семафор "есть работа"
//   [поток обработки] BatchFrameProcessor <----+  свой цикл, пачками
//        |  frameReady (QueuedConnection — только наружу, в GUI)
//        v
//   [поток GUI]  MainWindow / VideoWidget
// ---------------------------------------------------------------------------
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(quint16 port, Transport transport, int processDelayMs = 0,
                        QWidget* parent = nullptr);
    ~MainWindow() override;

    // Включить фильтр сразу при старте (используется флагом --edges).
    void setEdgesEnabled(bool on);

private slots:
    void onTelemetry(const proto::Telemetry& t);
    void onFrameReady(const QImage& image, quint32 frameId, qint64 senderTimestampMs,
                      double processMs);
    void onStats(const proto::RxStats& s);
    void onLog(const QString& msg);
    void onWatchdog();
    void onModeEnabled(bool enabled);
    void onTransportStats();

private:
    void buildUi();
    void startThreads();
    void stopThreads();
    void updateStatus();

    quint16 m_port;
    Transport m_transport;
    int m_processDelayMs;

    QThread* m_rxThread = nullptr;
    UdpReceiver* m_rx = nullptr;
    QThread* m_procThread = nullptr;

    // Вариант 1
    FrameProcessor* m_proc = nullptr;

    // Вариант 2
    SpscChannel* m_channel = nullptr;
    FrameQueueProducer* m_producer = nullptr;
    BatchFrameProcessor* m_batch = nullptr;

    VideoWidget* m_video = nullptr;
    QCheckBox* m_edgesCheck = nullptr;

    // Телеметрия
    QLabel* m_latValue = nullptr;
    QLabel* m_lonValue = nullptr;
    QLabel* m_altValue = nullptr;
    QLabel* m_speedValue = nullptr;
    QLabel* m_battValue = nullptr;
    QLabel* m_tempValue = nullptr;
    QLabel* m_satsValue = nullptr;
    QLabel* m_ageValue = nullptr;

    // Состояние канала
    QLabel* m_statusLabel = nullptr;
    QLabel* m_logLabel = nullptr;
    QLabel* m_fpsLabel = nullptr;
    QLabel* m_transportLabel = nullptr;
    QTimer* m_logClearTimer = nullptr;
    QTimer* m_transportStatsTimer = nullptr;

    QElapsedTimer m_lastTelemetryClock;
    QElapsedTimer m_lastFrameClock;
    QElapsedTimer m_fpsClock;
    int m_framesSinceFps = 0;
    double m_fps = 0.0;
    proto::RxStats m_lastStats;
    bool m_noData = true;    // сейчас данных нет (или ещё не было)
    bool m_videoIdle = true; // видео остановилось, кадр убран с экрана
};
