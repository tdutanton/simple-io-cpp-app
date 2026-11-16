#pragma once

// ---------------------------------------------------------------------------
// Соглашения о данных, которые ходят по сети между отправителем и приёмником.
//
// Всё, что касается "проводов", собрано в одном месте: и приёмник, и
// тестовый отправитель (tools/fake_sender.py) должны понимать один и тот же
// формат. Держите такие вещи отдельным заголовком, а не размазанными по
// парсингу в глубине приложения.
//
// Формат пакета (все числа little-endian, заголовок ровно 20 байт):
//
//   смещ. разм. поле          смысл
//   0     4    magic         0x314D4C54 ("TLM1") — защита от чужих пакетов
//   4     2    type          1 = телеметрия, 2 = фрагмент видеокадра
//   6     2    flags         зарезервировано (флаги протокола)
//   8     4    frameId       номер кадра (видео) / счётчик (телеметрия)
//   12    2    fragIndex     индекс фрагмента 0..fragCount-1
//   14    2    fragCount     сколько фрагментов в кадре всего
//   16    4    payloadBytes  длина полезной нагрузки
//   20    N    payload       данные
//
// Телеметрия: payload — JSON-объект (см. структуру Telemetry).
// Видео:       склеенный payload кадра = 8 байт senderTimestampMs + JPEG.
//              По сети кадр режется на фрагменты по kFragmentPayload байт.
// ---------------------------------------------------------------------------

#include <QByteArray>
#include <QDataStream>
#include <QDateTime>
#include <QIODevice>
#include <QMetaType>
#include <QString>

namespace proto {

constexpr quint32 kMagic = 0x314D4C54u;  // "TLM1"
constexpr quint16 kDefaultPort = 45454;

constexpr int kHeaderSize = 20;

// Почему 1200, а не 1400: типичный MTU Ethernet — 1500 байт, минус 20 байт
// IPv4 и 8 байт UDP остаётся 1472. Берём с запасом, чтобы не полагаться на
// фрагментацию IP: фрагментированный по IP пакет теряется целиком, если
// пропал хоть один кусок, и приёмка становится непредсказуемой.
constexpr int kFragmentPayload = 1200;

// Максимальный размер UDP-дейтаграммы.
constexpr int kMaxDatagram = 65507;

enum PacketType : quint16 {
    PtTelemetry = 1,
    PtVideo = 2,
};

struct Header {
    quint32 magic = kMagic;
    quint16 type = 0;
    quint16 flags = 0;
    quint32 frameId = 0;
    quint16 fragIndex = 0;
    quint16 fragCount = 0;
    quint32 payloadBytes = 0;
};

inline QByteArray encodeHeader(const Header& h)
{
    QByteArray out;
    out.reserve(kHeaderSize);
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << h.magic << h.type << h.flags << h.frameId << h.fragIndex << h.fragCount
       << h.payloadBytes;
    return out;
}

// Проверяет магию и минимальную длину. Никакой интерпретации payload здесь нет.
inline bool decodeHeader(const QByteArray& buf, Header& h)
{
    if (buf.size() < kHeaderSize)
        return false;

    QDataStream ds(buf);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds >> h.magic >> h.type >> h.flags >> h.frameId >> h.fragIndex >> h.fragCount
       >> h.payloadBytes;

    if (ds.status() != QDataStream::Ok || h.magic != kMagic)
        return false;

    // Заголовок не должен обещать больше данных, чем реально пришло.
    if (h.payloadBytes > quint32(buf.size() - kHeaderSize))
        return false;

    return true;
}

// ---------------------------------------------------------------------------
// Телеметрия. Отдельная структура, а не QJsonObject: остальное приложение
// не должно знать, что по проводу летит JSON. Захотите бинарный формат —
// поменяется только парсер в UdpReceiver.
// ---------------------------------------------------------------------------
struct Telemetry {
    quint32 seq = 0;
    double latitude = 0.0;
    double longitude = 0.0;
    double altitude = 0.0;
    double speed = 0.0;
    double battery = 0.0;
    double temperature = 0.0;
    int satellites = 0;
    qint64 senderTimestampMs = 0;  // время отправителя, мс от epoch
    QDateTime receivedAt;          // локальное время приёма (часы приёмника)
};

// Собранный из фрагментов видеокадр.
struct VideoFrame {
    quint32 id = 0;
    qint64 senderTimestampMs = 0;
    QByteArray encoded;  // JPEG как есть; декодирование — в FrameProcessor
    int fragments = 0;
};

// Счётчики приёмника для панели состояния (снимаются ~2 раза в секунду).
struct RxStats {
    double packetsPerSec = 0.0;
    quint64 packets = 0;
    quint64 framesOk = 0;
    quint64 framesLost = 0;
    quint64 framesDroppedBusy = 0;
    quint64 lateFragments = 0;
    quint64 badPackets = 0;
    int assemblies = 0;
};

}  // namespace proto

// Регистрация типов для соединений между потоками (QueuedConnection копирует
// аргументы через мета-систему, поэтому нестандартные типы надо объявить).
Q_DECLARE_METATYPE(proto::Telemetry)
Q_DECLARE_METATYPE(proto::VideoFrame)
Q_DECLARE_METATYPE(proto::RxStats)
