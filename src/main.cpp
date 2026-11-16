#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QPixmap>
#include <QTimer>

#include "MainWindow.h"
#include "Protocol.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("simple-io-app"));
    QApplication::setApplicationVersion(QStringLiteral("1.0"));

    // Регистрируем наши типы до первого соединения: QueuedConnection копирует
    // аргументы через мета-систему и без регистрации молча ничего не доставит
    // (в консоль прилетит предупреждение "Cannot queue arguments of type ...").
    qRegisterMetaType<proto::Telemetry>("proto::Telemetry");
    qRegisterMetaType<proto::VideoFrame>("proto::VideoFrame");
    qRegisterMetaType<proto::RxStats>("proto::RxStats");

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Приём телеметрии и видео по UDP"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption portOption({QStringLiteral("p"), QStringLiteral("port")},
                                  QStringLiteral("UDP-порт для приёма (по умолчанию 45454)."),
                                  QStringLiteral("port"),
                                  QString::number(proto::kDefaultPort));
    QCommandLineOption shotOption({QStringLiteral("s"), QStringLiteral("screenshot")},
                                  QStringLiteral("Сохранить снимок окна в файл и выйти."),
                                  QStringLiteral("file"));
    QCommandLineOption afterOption({QStringLiteral("a"), QStringLiteral("after")},
                                   QStringLiteral("Через сколько секунд делать снимок."),
                                   QStringLiteral("seconds"), QStringLiteral("3"));
    QCommandLineOption edgesOption(QStringLiteral("edges"),
                                   QStringLiteral("Включить фильтр Собеля при старте."));

    parser.addOption(portOption);
    parser.addOption(shotOption);
    parser.addOption(afterOption);
    parser.addOption(edgesOption);
    parser.process(app);

    bool ok = false;
    const quint16 port = quint16(parser.value(portOption).toUShort(&ok));
    if (!ok || port == 0) {
        qCritical("Некорректный порт");
        return 2;
    }

    MainWindow window(port);
    window.show();

    if (parser.isSet(edgesOption))
        window.setEdgesEnabled(true);

    const QString shotPath = parser.value(shotOption);
    if (!shotPath.isEmpty()) {
        // Режим автоснимка: удобно для проверки, что труба «сеть -> обработка ->
        // показ» реально работает, без ручного тыканья мышкой.
        const int seconds = qBound(1, parser.value(afterOption).toInt(), 60);
        QTimer::singleShot(seconds * 1000, &window, [&window, shotPath]() {
            window.grab().save(shotPath);
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
