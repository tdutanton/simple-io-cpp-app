#!/usr/bin/env python3
"""
Генератор тестового потока: телеметрия + «видеокадры» по UDP.

Нужен, чтобы пример можно было запустить без реального борта/камеры.
Формат пакетов совпадает с src/Protocol.h — держите их в согласии.

Запуск:
    python tools/fake_sender.py                     # 127.0.0.1:45454, 25 кадров/с
    python tools/fake_sender.py --loss 0.02         # 2% потерянных пакетов
    python tools/fake_sender.py --reorder 0.1       # переупорядочивание фрагментов
    python tools/fake_sender.py --fps 10 --width 320 --height 180

Требуется Pillow:  pip install pillow
"""

from __future__ import annotations

import argparse
import io
import json
import math
import random
import socket
import struct
import time

from PIL import Image, ImageDraw

MAGIC = 0x314D4C54
PT_TELEMETRY = 1
PT_VIDEO = 2

# <I 4 | H 2 | H 2 | I 4 | H 2 | H 2 | I 4  == 20 байт
HEADER = struct.Struct("<IHHIHHI")

FRAGMENT_PAYLOAD = 1200


def send_packet(sock: socket.socket, addr, packet_type: int, frame_id: int,
                frag_index: int, frag_count: int, payload: bytes) -> None:
    header = HEADER.pack(MAGIC, packet_type, 0, frame_id, frag_index, frag_count, len(payload))
    sock.sendto(header + payload, addr)


def build_background(w: int, h: int) -> Image.Image:
    """Неподвижный фон: градиент + сетка. Считается один раз при старте."""
    img = Image.new("RGB", (w, h))
    draw = ImageDraw.Draw(img)
    for y in range(h):
        k = y / max(1, h - 1)
        draw.line([(0, y), (w, y)],
                  fill=(int(16 + 26 * k), int(20 + 36 * k), int(36 + 58 * k)))

    step = 64
    for x in range(0, w, step):
        draw.line([(x, 0), (x, h)], fill=(42, 50, 68))
    for y in range(0, h, step):
        draw.line([(0, y), (w, y)], fill=(42, 50, 68))
    return img


def make_frame(bg: Image.Image, frame_id: int, elapsed: float) -> bytes:
    """Рисует «кадр» и возвращает JPEG-байты."""
    w, h = bg.size
    img = bg.copy()
    draw = ImageDraw.Draw(img)

    # Движущийся объект — чтобы на видео было видно, что оно живое.
    cx = w * 0.5 + math.cos(elapsed * 1.1) * w * 0.28
    cy = h * 0.5 + math.sin(elapsed * 1.7) * h * 0.26
    r = max(10, min(w, h) * 0.07)
    draw.ellipse([cx - r, cy - r, cx + r, cy + r],
                 fill=(235, 96, 44), outline=(255, 210, 140), width=3)

    # Перекрестие и угловые метки — дают чёткие границы для фильтра Собеля.
    draw.line([(w // 2 - 45, h // 2), (w // 2 + 45, h // 2)], fill=(120, 230, 150))
    draw.line([(w // 2, h // 2 - 45), (w // 2, h // 2 + 45)], fill=(120, 230, 150))
    for x0, y0 in ((6, 6), (w - 26, 6), (6, h - 26), (w - 26, h - 26)):
        draw.rectangle([x0, y0, x0 + 20, y0 + 20], outline=(200, 220, 255))

    draw.text((12, 8), f"FRAME {frame_id:06d}", fill=(235, 242, 255))
    draw.text((12, 24), f"T+{elapsed:7.1f}s", fill=(185, 205, 235))

    buf = io.BytesIO()
    img.save(buf, format="JPEG", quality=75)
    return buf.getvalue()


def fragment_order(count: int, reorder: float) -> list[int]:
    order = list(range(count))
    if reorder > 0:
        # Локальные перестановки — так обычно и выглядит переупорядочивание
        # в реальной сети, а не полный случайный шафл.
        for i in range(count - 1):
            if random.random() < reorder:
                order[i], order[i + 1] = order[i + 1], order[i]
    return order


def main() -> None:
    p = argparse.ArgumentParser(description="Тестовый UDP-отправитель телеметрии и видео")
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=45454)
    p.add_argument("--fps", type=float, default=25.0, help="кадров в секунду")
    p.add_argument("--width", type=int, default=640)
    p.add_argument("--height", type=int, default=360)
    p.add_argument("--telemetry-hz", type=float, default=10.0)
    p.add_argument("--loss", type=float, default=0.0, help="вероятность потерять пакет 0..1")
    p.add_argument("--reorder", type=float, default=0.0, help="вероятность переставить фрагменты")
    p.add_argument("--dup", type=float, default=0.0, help="вероятность продублировать пакет")
    p.add_argument("--duration", type=float, default=0.0, help="секунд работы, 0 = бесконечно")
    p.add_argument("--no-video", action="store_true")
    p.add_argument("--no-telemetry", action="store_true")
    args = p.parse_args()

    addr = (args.host, args.port)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    # Для локальных тестов: не собирать огромные буферы, слать сразу.
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1 << 20)

    bg = build_background(args.width, args.height)
    frame_id = 0
    seq = 0
    started = time.monotonic()
    next_frame = started
    next_tlm = started
    last_report = started
    sent_frames = sent_packets = 0

    print(f"Отправляю на {addr[0]}:{addr[1]}  {args.width}x{args.height} @ {args.fps} fps, "
          f"телеметрия {args.telemetry_hz} Гц")
    print("Ctrl+C — остановить")

    try:
        while True:
            now = time.monotonic()
            if args.duration > 0 and now - started > args.duration:
                break

            # --- телеметрия ---
            if not args.no_telemetry and now >= next_tlm:
                seq += 1
                t = now - started
                payload = json.dumps({
                    "seq": seq,
                    "ts": int(time.time() * 1000),
                    "lat": 55.7558 + 0.0004 * math.sin(t / 7.0),
                    "lon": 37.6173 + 0.0006 * math.cos(t / 9.0),
                    "alt": 120.0 + 8.0 * math.sin(t / 5.0),
                    "speed": 14.0 + 3.0 * math.cos(t / 4.0),
                    "batt": max(0.0, 100.0 - t * 0.05),
                    "temp": 36.5 + 1.5 * math.sin(t / 11.0),
                    "sats": 9 + int(2 * math.sin(t / 13.0)),
                }).encode("utf-8")
                send_packet(sock, addr, PT_TELEMETRY, seq, 0, 1, payload)
                sent_packets += 1
                next_tlm += 1.0 / max(0.1, args.telemetry_hz)

            # --- видео ---
            if not args.no_video and now >= next_frame:
                jpeg = make_frame(bg, frame_id, now - started)
                # Внутри кадра: 8 байт времени отправителя + JPEG.
                payload = struct.pack("<q", int(time.time() * 1000)) + jpeg
                count = max(1, (len(payload) + FRAGMENT_PAYLOAD - 1) // FRAGMENT_PAYLOAD)
                chunks = [payload[i * FRAGMENT_PAYLOAD:(i + 1) * FRAGMENT_PAYLOAD]
                          for i in range(count)]

                for index in fragment_order(count, args.reorder):
                    if args.loss > 0 and random.random() < args.loss:
                        continue  # «потеряли» пакет
                    chunk = chunks[index]
                    send_packet(sock, addr, PT_VIDEO, frame_id, index, count, chunk)
                    sent_packets += 1
                    if args.dup > 0 and random.random() < args.dup:
                        send_packet(sock, addr, PT_VIDEO, frame_id, index, count, chunk)
                        sent_packets += 1

                frame_id += 1
                sent_frames += 1
                next_frame += 1.0 / max(0.1, args.fps)

                # Не пытаемся «догнать» график, если отстали: иначе накопится
                # лавина кадров и всё поедет.
                if next_frame < now - 0.25:
                    next_frame = now

            if now - last_report >= 1.0:
                print(f"кадров {sent_frames:6d}  пакетов {sent_packets:7d}  "
                      f"(~{sent_packets / (now - last_report):.0f} пак/с)")
                sent_frames = sent_packets = 0
                last_report = now

            sleep_for = min(next_frame, next_tlm) - time.monotonic()
            if sleep_for > 0:
                time.sleep(min(sleep_for, 0.01))

    except KeyboardInterrupt:
        print("\nОстановлено")


if __name__ == "__main__":
    main()
