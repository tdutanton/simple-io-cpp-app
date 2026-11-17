#include "UdpReceiver.h"

#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUdpSocket>

#include <algorithm>

namespace {

// Сколько ждём подтверждения от обработчика, прежде чем считать его зависшим
// и снова начать принимать кадры. Без такой страховки одна ошибка в
// обработчике навсегда «замораживает» видеопоток.
constexpr int kWaitAckMs = 2000;

// Незавершённый кадр старше этого возраста выбрасываем: дырки в нумерации
// фрагментов уже не заполнятся, ждать бессмысленно.
constexpr int kAssemblyTimeoutMs = 250;

// Сколько кадров максимум может собираться одновременно. Нормальный
// отправитель не должен гнать больше 2-3 кадров вперёд.
constexpr int kMaxAssemblies = 4;

}  // namespace

UdpReceiver::UdpReceiver(quint16 port, QObject* parent)
    : QObject(parent)
    , m_port(port)
{
    m_buffer.resize(proto::kMaxDatagram);

    // Таймеры создаём здесь же: они «переедут» в поток приёмника вместе с
    // хозяином, потому что являются его детьми.
    m_sweepTimer = new QTimer(this);
    m_sweepTimer->setInterval(100);
    connect(m_sweepTimer, &QTimer::timeout, this, &UdpReceiver::onSweep);

    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(500);
    connect(m_statsTimer, &QTimer::timeout, this, &UdpReceiver::onStatsTimer);
}

UdpReceiver::~UdpReceiver() = default;

void UdpReceiver::start()
{
    m_socket = new QUdpSocket(this);

    // ShareAddress + ReuseAddressHint: позволяет запускать второй экземпляр
    // на том же порту (удобно при отладке). В бою обычно ShareAddress не нужен.
    if (!m_socket->bind(QHostAddress::AnyIPv4, m_port,
                        QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        const QString err = QStringLiteral("Не удалось открыть UDP-порт %1: %2")
                                .arg(m_port)
                                .arg(m_socket->errorString());
        qWarning().noquote() << err;
        emit log(err);
        return;
    }
    qInfo().noquote() << QStringLiteral("UDP: слушаю порт %1").arg(m_port);

    connect(m_socket, &QUdpSocket::readyRead, this, &UdpReceiver::onReadyRead);

    m_sweepTimer->start();
    m_statClock.start();
    m_statsTimer->start();

    emit log(QStringLiteral("Слушаю UDP :%1").arg(m_port));
}

void UdpReceiver::stop()
{
    m_sweepTimer->stop();
    m_statsTimer->stop();
    if (m_socket) {
        m_socket->close();
        delete m_socket;  // мы в своём потоке, сокет больше никто не трогает
        m_socket = nullptr;
    }
    m_assemblies.clear();
}

void UdpReceiver::onReadyRead()
{
    // Важно: за один readyRead в сокете может лежать пачка дейтаграмм.
    // Читаем всё, иначе следующие сигналы не придут, пока не опустеет буфер.
    while (m_socket && m_socket->hasPendingDatagrams()) {
        const qint64 n = m_socket->readDatagram(m_buffer.data(), m_buffer.size());
        if (n <= 0)
            break;
        handleDatagram(m_buffer.constData(), n);
    }
}

void UdpReceiver::handleDatagram(const char* data, qint64 len)
{
    ++m_packets;

    proto::Header h;
    const QByteArray packet = QByteArray::fromRawData(data, int(len));
    if (!proto::decodeHeader(packet, h)) {
        ++m_badPackets;
        return;
    }

    const QByteArray payload = packet.mid(proto::kHeaderSize, int(h.payloadBytes));

    switch (h.type) {
    case proto::PtTelemetry:
        handleTelemetry(h, payload);
        break;
    case proto::PtVideo:
        handleVideoFragment(h, payload);
        break;
    default:
        ++m_badPackets;
        break;
    }
}

void UdpReceiver::handleTelemetry(const proto::Header& h, const QByteArray& payload)
{
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        ++m_badPackets;
        return;
    }

    const QJsonObject o = doc.object();
    proto::Telemetry t;
    t.seq = h.frameId;
    t.latitude = o.value(QStringLiteral("lat")).toDouble();
    t.longitude = o.value(QStringLiteral("lon")).toDouble();
    t.altitude = o.value(QStringLiteral("alt")).toDouble();
    t.speed = o.value(QStringLiteral("speed")).toDouble();
    t.battery = o.value(QStringLiteral("batt")).toDouble();
    t.temperature = o.value(QStringLiteral("temp")).toDouble();
    t.satellites = o.value(QStringLiteral("sats")).toInt();
    t.senderTimestampMs = qint64(o.value(QStringLiteral("ts")).toDouble());
    t.receivedAt = QDateTime::currentDateTimeUtc();

    emit telemetryReceived(t);
}

void UdpReceiver::handleVideoFragment(const proto::Header& h, const QByteArray& payload)
{
    if (h.fragCount == 0 || h.fragIndex >= h.fragCount) {
        ++m_badPackets;
        return;
    }

    // Кадр из прошлого (или уже показанный) — он опоздал, показывать его
    // поверх более свежего нельзя. Это обычная ситуация при переупорядочении.
    if (h.frameId <= m_lastDeliveredFrame) {
        ++m_lateFragments;
        return;
    }

    // Слишком много параллельных сборок — выбрасываем самую старую.
    if (m_assemblies.size() >= kMaxAssemblies && !m_assemblies.contains(h.frameId)) {
        quint32 oldestId = 0;
        qint64 oldestAge = -1;
        for (auto it = m_assemblies.constBegin(); it != m_assemblies.constEnd(); ++it) {
            const qint64 age = it->age.elapsed();
            if (age > oldestAge) {
                oldestAge = age;
                oldestId = it.key();
            }
        }
        m_assemblies.remove(oldestId);
        ++m_framesLost;
    }

    Assembly& a = m_assemblies[h.frameId];
    if (a.parts.isEmpty()) {
        a.parts.resize(h.fragCount);
        a.age.start();
    }

    if (h.fragIndex >= a.parts.size()) {
        ++m_badPackets;
        return;
    }
    if (!a.parts[h.fragIndex].isEmpty())  // дубликат фрагмента (бывает при повторной отправке)
        return;

    a.parts[h.fragIndex] = payload;
    ++a.got;

    if (a.got == a.parts.size())
        deliverFrame(h.frameId);
}

void UdpReceiver::deliverFrame(quint32 frameId)
{
    auto it = m_assemblies.find(frameId);
    if (it == m_assemblies.end())
        return;

    QByteArray joined;
    for (const QByteArray& part : it->parts)
        joined.append(part);
    const int fragments = it->parts.size();
    m_assemblies.erase(it);

    m_lastDeliveredFrame = std::max(m_lastDeliveredFrame, frameId);

    if (joined.size() < 8) {
        ++m_badPackets;
        return;
    }

    // Первые 8 байт кадра — время отправителя. Нужно, чтобы показывать
    // реальную сквозную задержку, а не только скорость сети.
    qint64 ts = 0;
    QDataStream ds(joined.left(8));
    ds.setByteOrder(QDataStream::LittleEndian);
    ds >> ts;

    if (m_requireAck) {
        if (m_waitingForProcessor) {
            // Обработчик занят — не плодим очередь, просто теряем этот кадр.
            // Для видео это правильная стратегия: свежий кадр важнее прошлых.
            ++m_framesDroppedBusy;
            return;
        }
        m_waitingForProcessor = true;
        m_waitClock.start();
    }
    // В SPSC-варианте ждать некого: переполнение обработает кольцевой буфер.

    proto::VideoFrame frame;
    frame.id = frameId;
    frame.senderTimestampMs = ts;
    frame.encoded = joined.mid(8);
    frame.fragments = fragments;

    ++m_framesOk;

    emit frameReceived(frame);
}

void UdpReceiver::setRequireAck(bool on)
{
    m_requireAck = on;
}

void UdpReceiver::onFrameProcessed(quint32 /*frameId*/)
{
    m_waitingForProcessor = false;
}

void UdpReceiver::onSweep()
{
    // Досрочно выкидываем недособранные кадры: часть фрагментов потерялась.
    for (auto it = m_assemblies.begin(); it != m_assemblies.end();) {
        if (it->age.elapsed() > kAssemblyTimeoutMs) {
            it = m_assemblies.erase(it);
            ++m_framesLost;
        } else {
            ++it;
        }
    }

    // Страховка от «зависшего» обработчика.
    if (m_waitingForProcessor && m_waitClock.elapsed() > kWaitAckMs) {
        m_waitingForProcessor = false;
        emit log(QStringLiteral("Обработчик кадров не отвечает — снимаю блокировку"));
    }
}

void UdpReceiver::onStatsTimer()
{
    proto::RxStats s;
    s.packets = m_packets;
    s.framesOk = m_framesOk;
    s.framesLost = m_framesLost;
    s.framesDroppedBusy = m_framesDroppedBusy;
    s.lateFragments = m_lateFragments;
    s.badPackets = m_badPackets;
    s.assemblies = int(m_assemblies.size());
    s.packetsPerSec = double(m_packets - m_packetsAtLastStat) * 1000.0
                      / double(std::max<qint64>(1, m_statClock.elapsed()));

    m_packetsAtLastStat = m_packets;
    m_statClock.restart();

    emit statsUpdated(s);
}
