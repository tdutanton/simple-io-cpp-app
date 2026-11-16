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
