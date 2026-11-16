#include "VideoWidget.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>

VideoWidget::VideoWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(320, 180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAutoFillBackground(false);
}

void VideoWidget::setFrame(const QImage& image, quint32 frameId, qint64 senderTimestampMs)
{
    m_image = image;
    m_frameId = frameId;
    m_senderTs = senderTimestampMs;
    m_cacheSize = QSize();  // картинка новая — кеш масштаба невалиден
    update();               // перерисовка произойдёт в ближайшем цикле событий
}

void VideoWidget::clearFrame()
{
    m_image = QImage();
    m_scaledCache = QPixmap();
    m_cacheSize = QSize();
    update();
}

void VideoWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(10, 10, 12));

    if (m_image.isNull()) {
        p.setPen(QColor(110, 110, 118));
        p.drawText(rect(), Qt::AlignCenter, tr("НЕТ ВИДЕОСИГНАЛА"));
        return;
    }

    // Вписываем кадр в окно с сохранением пропорций (letterbox).
    const QSize target = m_image.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect targetRect(QPoint((width() - target.width()) / 2,
                                  (height() - target.height()) / 2),
                           target);

    // Масштабируем только когда изменился размер окна: SmoothTransformation
    // на больших картинках стоит миллисекунды, и делать его на каждый кадр
    // в потоке GUI — плохая идея. В проекте посерьёзнее это делает worker.
    if (m_scaledCache.isNull() || m_cacheSize != target) {
        m_scaledCache = QPixmap::fromImage(m_image.scaled(target, Qt::IgnoreAspectRatio,
                                                          Qt::SmoothTransformation));
        m_cacheSize = target;
    }

    p.drawPixmap(targetRect.topLeft(), m_scaledCache);

    // Небольшая служебная плашка поверх видео.
    if (m_senderTs > 0) {
        const qint64 latencyMs =
            QDateTime::currentMSecsSinceEpoch() - m_senderTs;  // только если часы синхронны
        const QString info = QStringLiteral("%1x%2  кадр #%3  задержка %4 мс")
                                 .arg(m_image.width())
                                 .arg(m_image.height())
                                 .arg(m_frameId)
                                 .arg(latencyMs);

        QFont f = p.font();
        f.setPointSizeF(f.pointSizeF() - 1.0);
        p.setFont(f);

        const QFontMetrics fm(f);
        const QRect textRect = fm.boundingRect(info).adjusted(-6, -3, 6, 3);
        // Плашку кладём в левый нижний угол: верх у кадра обычно занят
        // собственной служебной графикой отправителя.
        const QRect box(QPoint(targetRect.left() + 8,
                               targetRect.bottom() - textRect.height() - 8),
                        textRect.size());

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 140));
        p.drawRect(box);
        p.setPen(QColor(220, 220, 220));
        p.drawText(box, Qt::AlignCenter, info);
    }
}
