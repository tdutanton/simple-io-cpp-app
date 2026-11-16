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
class VideoWidget;

// ---------------------------------------------------------------------------
// MainWindow — поток GUI. Здесь только виджеты и «склейка»: он создаёт
// рабочие потоки, соединяет сигналы и обновляет интерфейс.
//
// Схема потоков:
//
//   [поток сети]  UdpReceiver   сокет, парсинг, сборка кадров
//        |  сигнал frameReceived (QueuedConnection)
//        v
//   [поток обработки] FrameProcessor   JPEG-декод + фильтр
//        |  сигнал frameReady (QueuedConnection)
//        v
//   [поток GUI]  MainWindow / VideoWidget   показ
// ---------------------------------------------------------------------------
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(quint16 port, QWidget* parent = nullptr);
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

private:
    void buildUi();
    void startThreads();
    void stopThreads();
    void updateStatus();

    quint16 m_port;

    QThread* m_rxThread = nullptr;
    UdpReceiver* m_rx = nullptr;
    QThread* m_procThread = nullptr;
    FrameProcessor* m_proc = nullptr;

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
    QTimer* m_logClearTimer = nullptr;

    QElapsedTimer m_lastTelemetryClock;
    QElapsedTimer m_lastFrameClock;
    QElapsedTimer m_fpsClock;
    int m_framesSinceFps = 0;
    double m_fps = 0.0;
    proto::RxStats m_lastStats;
    bool m_noData = true;    // сейчас данных нет (или ещё не было)
    bool m_videoIdle = true; // видео остановилось, кадр убран с экрана
};
