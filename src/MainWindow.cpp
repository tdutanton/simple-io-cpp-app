#include "MainWindow.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include "FrameProcessor.h"
#include "UdpReceiver.h"
#include "VideoWidget.h"

MainWindow::MainWindow(quint16 port, QWidget* parent)
    : QMainWindow(parent)
    , m_port(port)
{
    buildUi();
    updateStatus();
    startThreads();

    // Сторож: если телеметрия перестала приходить, интерфейс обязан это
    // показать. Молча висящий «последний кадр» — классическая ловушка.
    auto* watchdog = new QTimer(this);
    watchdog->setInterval(500);
    connect(watchdog, &QTimer::timeout, this, &MainWindow::onWatchdog);
    watchdog->start();

    m_fpsClock.start();
    resize(1100, 640);
    setWindowTitle(tr("Телеметрия и видео по UDP — пример на Qt6"));
}

MainWindow::~MainWindow()
{
    stopThreads();
}

void MainWindow::buildUi()
{
    auto* central = new QWidget(this);
    auto* root = new QHBoxLayout(central);

    m_video = new VideoWidget(central);
    root->addWidget(m_video, 1);

    auto* side = new QWidget(central);
    side->setFixedWidth(320);
    auto* sideLayout = new QVBoxLayout(side);

    // --- Телеметрия -------------------------------------------------------
    auto* tlmBox = new QGroupBox(tr("Телеметрия"), side);
    auto* tlmForm = new QFormLayout(tlmBox);
    auto makeValue = [tlmBox]() {
        auto* l = new QLabel(QStringLiteral("—"), tlmBox);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return l;
    };
    m_latValue = makeValue();
    m_lonValue = makeValue();
    m_altValue = makeValue();
    m_speedValue = makeValue();
    m_battValue = makeValue();
    m_tempValue = makeValue();
    m_satsValue = makeValue();
    m_ageValue = makeValue();

    tlmForm->addRow(tr("Широта"), m_latValue);
    tlmForm->addRow(tr("Долгота"), m_lonValue);
    tlmForm->addRow(tr("Высота"), m_altValue);
    tlmForm->addRow(tr("Скорость"), m_speedValue);
    tlmForm->addRow(tr("Батарея"), m_battValue);
    tlmForm->addRow(tr("Температура"), m_tempValue);
    tlmForm->addRow(tr("Спутники"), m_satsValue);
    tlmForm->addRow(tr("Возраст данных"), m_ageValue);
    sideLayout->addWidget(tlmBox);

    // --- Обработка --------------------------------------------------------
    auto* procBox = new QGroupBox(tr("Обработка кадра"), side);
    auto* procLayout = new QVBoxLayout(procBox);
    m_edgesCheck = new QCheckBox(tr("Фильтр Собеля (нагрузка на CPU)"), procBox);
    procLayout->addWidget(m_edgesCheck);
    sideLayout->addWidget(procBox);

    sideLayout->addStretch(1);
    root->addWidget(side);

    setCentralWidget(central);

    // --- Строка состояния --------------------------------------------------
    // Основной статус (потери, пакеты) и отдельное поле под сообщения, чтобы
    // временный текст не перекрывал важное «нет связи».
    m_statusLabel = new QLabel(tr("Ожидание данных…"), this);
    statusBar()->addWidget(m_statusLabel, 1);

    m_logLabel = new QLabel(this);
    m_logLabel->setStyleSheet(QStringLiteral("color: #7f8c8d;"));
    statusBar()->addPermanentWidget(m_logLabel);

    m_fpsLabel = new QLabel(QStringLiteral("—"), this);
    m_fpsLabel->setMinimumWidth(80);
    m_fpsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    statusBar()->addPermanentWidget(m_fpsLabel);

    m_logClearTimer = new QTimer(this);
    m_logClearTimer->setSingleShot(true);
    m_logClearTimer->setInterval(4000);
    connect(m_logClearTimer, &QTimer::timeout, this, [this]() { m_logLabel->clear(); });

    // QCheckBox::toggled(bool) есть во всех версиях Qt6 (checkStateChanged
    // появился только в 6.7), поэтому берём его.
    connect(m_edgesCheck, &QCheckBox::toggled, this, &MainWindow::onModeEnabled);
}

void MainWindow::startThreads()
{
    // -----------------------------------------------------------------------
    // Поток 1: сеть.
    // -----------------------------------------------------------------------
    m_rxThread = new QThread;
    m_rx = new UdpReceiver(m_port);
    m_rx->moveToThread(m_rxThread);

    // ВАЖНО: сокет создаётся внутри UdpReceiver::start(), то есть уже в
    // потоке приёмника. Создавать QUdpSocket в конструкторе, который
    // выполняется в GUI-потоке, — значит потом работать с ним из чужого потока.
    connect(m_rxThread, &QThread::started, m_rx, &UdpReceiver::start);

    // -----------------------------------------------------------------------
    // Поток 2: обработка.
    // -----------------------------------------------------------------------
    m_procThread = new QThread;
    m_proc = new FrameProcessor;
    m_proc->moveToThread(m_procThread);

    // -----------------------------------------------------------------------
    // Соединения. Тип Auto сам становится Queued, потому что объекты живут
    // в разных потоках: сигнал кладёт событие в очередь получателя, и слот
    // выполняется уже в его потоке.
    // -----------------------------------------------------------------------
    connect(m_rx, &UdpReceiver::frameReceived, m_proc, &FrameProcessor::processFrame);
    connect(m_proc, &FrameProcessor::frameProcessed, m_rx, &UdpReceiver::onFrameProcessed);

    connect(m_proc, &FrameProcessor::frameReady, this, &MainWindow::onFrameReady);
    connect(m_rx, &UdpReceiver::telemetryReceived, this, &MainWindow::onTelemetry);
    connect(m_rx, &UdpReceiver::statsUpdated, this, &MainWindow::onStats);
    connect(m_rx, &UdpReceiver::log, this, &MainWindow::onLog);
    connect(m_proc, &FrameProcessor::log, this, &MainWindow::onLog);

    m_rxThread->start();
    m_procThread->start();

    onModeEnabled(m_edgesCheck->isChecked());
}

void MainWindow::stopThreads()
{
    // Сокет и таймеры приёмника принадлежат его потоку — закрывать их надо
    // тоже там. BlockingQueuedConnection дождётся, пока stop() выполнится.
    if (m_rx && m_rxThread && m_rxThread->isRunning())
        QMetaObject::invokeMethod(m_rx, "stop", Qt::BlockingQueuedConnection);

    // Порядок важен: сначала останавливаем сеть (больше никто не пишет в
    // обработчик), потом обработку, и только затем удаляем объекты.
    if (m_rxThread) {
        m_rxThread->quit();
        m_rxThread->wait(2000);
    }
    if (m_procThread) {
        m_procThread->quit();
        m_procThread->wait(2000);
    }

    delete m_rx;
    m_rx = nullptr;
    delete m_proc;
    m_proc = nullptr;

    delete m_rxThread;
    m_rxThread = nullptr;
    delete m_procThread;
    m_procThread = nullptr;
}

void MainWindow::setEdgesEnabled(bool on)
{
    if (m_edgesCheck)
        m_edgesCheck->setChecked(on);  // дальше сработает сигнал toggled
}

void MainWindow::onModeEnabled(bool enabled)
{
    if (!m_proc)
        return;
    // Вызов слота «чужого» объекта обязан идти через очередь: invokeMethod
    // с Qt::QueuedConnection выполнит его в потоке обработчика.
    QMetaObject::invokeMethod(m_proc, "setMode", Qt::QueuedConnection,
                              Q_ARG(int, enabled ? FrameProcessor::Edges
                                                 : FrameProcessor::Passthrough));
}

void MainWindow::onTelemetry(const proto::Telemetry& t)
{
    m_lastTelemetryClock.restart();
    if (m_noData) {
        m_noData = false;
        updateStatus();
    }

    m_latValue->setText(QString::number(t.latitude, 'f', 6));
    m_lonValue->setText(QString::number(t.longitude, 'f', 6));
    m_altValue->setText(QStringLiteral("%1 м").arg(t.altitude, 0, 'f', 1));
    m_speedValue->setText(QStringLiteral("%1 м/с").arg(t.speed, 0, 'f', 1));
    m_battValue->setText(QStringLiteral("%1 %").arg(t.battery, 0, 'f', 1));
    m_tempValue->setText(QStringLiteral("%1 °C").arg(t.temperature, 0, 'f', 1));
    m_satsValue->setText(QString::number(t.satellites));
    m_ageValue->setText(QStringLiteral("0 мс"));
}

void MainWindow::onFrameReady(const QImage& image, quint32 frameId, qint64 senderTimestampMs,
                              double processMs)
{
    m_video->setFrame(image, frameId, senderTimestampMs);
    m_lastFrameClock.restart();
    m_videoIdle = false;

    ++m_framesSinceFps;
    const qint64 elapsed = m_fpsClock.elapsed();
    // Окно 2 секунды, а не 1: кадры приходят пачками (сеть + таймеры ОС), и
    // на секундном окне цифра заметно прыгает вокруг реального значения.
    if (elapsed >= 2000) {
        m_fps = double(m_framesSinceFps) * 1000.0 / double(elapsed);
        m_framesSinceFps = 0;
        m_fpsClock.restart();
        m_fpsLabel->setText(QStringLiteral("%1 к/с · %2 мс")
                                .arg(m_fps, 0, 'f', 1)
                                .arg(processMs, 0, 'f', 1));
    }
}

void MainWindow::onStats(const proto::RxStats& s)
{
    m_lastStats = s;
    updateStatus();
}

// Единая точка сборки строки состояния: и счётчики, и сторож идут сюда,
// поэтому «нет связи» не может быть затёрто очередной порцией статистики.
void MainWindow::updateStatus()
{
    if (m_noData) {
        m_statusLabel->setStyleSheet(QStringLiteral("color: #c0392b;"));
        m_statusLabel->setText(
            tr("Нет телеметрии с порта %1 — проверьте отправитель").arg(m_port));
        return;
    }

    m_statusLabel->setStyleSheet(QString());
    const proto::RxStats& s = m_lastStats;
    const quint64 totalFrames = s.framesOk + s.framesLost;
    const double lossPct = totalFrames > 0 ? 100.0 * double(s.framesLost) / double(totalFrames)
                                           : 0.0;

    m_statusLabel->setText(
        tr("UDP :%1 | %2 пак/с | потерь кадров %3% | отброшено (занято) %4 | "
           "поздних фрагментов %5 | брак %6")
            .arg(m_port)
            .arg(qint64(s.packetsPerSec))
            .arg(lossPct, 0, 'f', 1)
            .arg(s.framesDroppedBusy)
            .arg(s.lateFragments)
            .arg(s.badPackets));
}

void MainWindow::onWatchdog()
{
    const bool stale = !m_lastTelemetryClock.isValid() || m_lastTelemetryClock.elapsed() > 1500;
    if (stale && !m_noData) {
        m_noData = true;
        updateStatus();
    }
    if (m_lastTelemetryClock.isValid())
        m_ageValue->setText(QStringLiteral("%1 мс").arg(m_lastTelemetryClock.elapsed()));

    // Сторож для видео: последний кадр не должен висеть на экране вечно —
    // иначе оператор уверен, что поток есть, а на самом деле он встал.
    if (!m_videoIdle && m_lastFrameClock.isValid() && m_lastFrameClock.elapsed() > 1500) {
        m_videoIdle = true;
        m_video->clearFrame();
        m_fpsLabel->setText(QStringLiteral("—"));
    }
}

void MainWindow::onLog(const QString& msg)
{
    m_logLabel->setText(msg);
    m_logClearTimer->start();
}
