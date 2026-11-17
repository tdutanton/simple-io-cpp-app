#pragma once

#include <QByteArray>
#include <QImage>

// Декодирование и обработка кадра, общие для обоих вариантов транспорта
// (сигналы и SPSC-очередь). Вынесено отдельно, чтобы при сравнении двух
// реализаций отличался только транспорт, а не код обработки.
namespace codec {

// JPEG-байты -> картинка. При ошибке возвращает пустой QImage.
QImage decodeJpeg(const QByteArray& data);

// Простой Sobel по grayscale. Смысл не в качестве, а в честной нагрузке на CPU.
QImage sobel(const QImage& src);

}  // namespace codec
