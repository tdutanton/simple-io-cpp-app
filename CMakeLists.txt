cmake_minimum_required(VERSION 3.16)

project(simple-io-app VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# AUTOMOC сам запустит moc для классов с Q_OBJECT — не нужно вызывать moc руками
# и следить за тем, чтобы он попадал в сборку.
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Network)

# По умолчанию — GUI-приложение без консольного окна. С -DSIMPLE_IO_CONSOLE=ON
# собирается консольный вариант: qDebug/qWarning видны в терминале, что сильно
# помогает при отладке проблем со стартом и плагинами.
option(SIMPLE_IO_CONSOLE "Оставить консоль для отладочного вывода" OFF)

add_executable(simple-io-app
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/VideoWidget.cpp
    src/VideoWidget.h
    src/UdpReceiver.cpp
    src/UdpReceiver.h
    src/FrameProcessor.cpp
    src/FrameProcessor.h
    src/BatchFrameProcessor.cpp
    src/BatchFrameProcessor.h
    src/FrameQueueProducer.cpp
    src/FrameQueueProducer.h
    src/FrameCodec.cpp
    src/FrameCodec.h
    src/SpscQueue.h
    src/SpscChannel.h
    src/Protocol.h
)

if(WIN32 AND NOT SIMPLE_IO_CONSOLE)
    set_target_properties(simple-io-app PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

target_link_libraries(simple-io-app PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

if(MSVC)
    # Чтобы русский текст в исходниках и выводе не превращался в кракозябры.
    target_compile_options(simple-io-app PRIVATE /utf-8)
endif()

# Замер стоимости сигналов/слотов (см. bench/signal_bench.cpp).
# Включается явно: -DSIMPLE_IO_BENCH=ON, чтобы не собирать лишнее по умолчанию.
option(SIMPLE_IO_BENCH "Собрать замер стоимости сигналов/слотов" OFF)
if(SIMPLE_IO_BENCH)
    add_executable(signal-bench bench/signal_bench.cpp)
    target_link_libraries(signal-bench PRIVATE Qt6::Core Qt6::Gui)
    if(MSVC)
        target_compile_options(signal-bench PRIVATE /utf-8)
    endif()
endif()
