#pragma once

#include <array>
#include <atomic>
#include <cstddef>

// ---------------------------------------------------------------------------
// SpscQueue — кольцевой буфер SPSC (single producer / single consumer).
//
// Ключевые свойства:
//   * ровно один поток пишет (tryPush) и ровно один читает (tryPop) — только
//     при этом условии корректны обычные атомики без блокировок;
//   * нет аллокаций в горячем пути: буфер фиксированного размера на стеке/в
//     составе объекта, элементы создаются на месте;
//   * нет мьютексов: producer пишет m_tail, consumer — m_head, каждый читает
//     чужой указатель с acquire-семантикой.
//
// Индексы — «свободно бегущие» счётчики по модулю 2^64; маска Capacity-1
// превращает их в позицию в массиве. Поэтому Capacity обязана быть степенью
// двойки.
//
// Ёмкость N означает, что одновременно в очереди помещается N-1 элементов:
// один слот всегда пуст, чтобы отличать «полно» от «пусто» без отдельного
// счётчика.
// ---------------------------------------------------------------------------
template <typename T, std::size_t Capacity>
class SpscQueue {
    static_assert(Capacity >= 2, "нужно хотя бы два слота");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity должна быть степенью двойки");

public:
    static constexpr std::size_t capacity() { return Capacity; }

    // Пишет только producer. false — очередь полна, элемент не принят.
    bool tryPush(const T& value)
    {
        const std::size_t tail = m_tail.load(std::memory_order_relaxed);
        const std::size_t next = (tail + 1) & (Capacity - 1);

        // acquire: гарантирует, что потребитель уже закончил читать слот,
        // который мы собираемся перезаписать, и что m_slots[next] виден целиком.
        if (next == m_head.load(std::memory_order_acquire))
            return false;

        m_slots[tail] = value;
        // release: запись элемента в m_slots[tail] должна быть видна
        // потребителю до того, как он увидит новый m_tail.
        m_tail.store(next, std::memory_order_release);
        return true;
    }

    // Читает только consumer. false — очередь пуста.
    bool tryPop(T& out)
    {
        const std::size_t head = m_head.load(std::memory_order_relaxed);

        // acquire: данные, записанные producer'ом, видны целиком.
        if (head == m_tail.load(std::memory_order_acquire))
            return false;

        out = m_slots[head];
        // release: слот свободен для перезаписи только после того, как мы
        // действительно скопировали из него значение.
        m_head.store((head + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }

    // Приблизительный размер. Полезен только для статистики/UI:
    // между вызовами producer и consumer значение уже устарело.
    std::size_t size() const
    {
        const std::size_t tail = m_tail.load(std::memory_order_acquire);
        const std::size_t head = m_head.load(std::memory_order_acquire);
        return (tail - head) & (Capacity - 1);
    }

private:
    std::array<T, Capacity> m_slots{};

    // Разносим указатели по разным кэш-линиям: иначе producer и consumer
    // постоянно инвалидируют кэш друг друга (false sharing).
    alignas(64) std::atomic<std::size_t> m_head{0};  // пишет consumer
    alignas(64) std::atomic<std::size_t> m_tail{0};  // пишет producer
};
