cmake_minimum_required(VERSION 3.19)

project(StatusReplay LANGUAGES CXX)

option(STATUSREPLAY_BUILD_TESTS "Build StatusReplay host tests" ON)
# The replay entry point mirrors the app setup and needs libStatusQ (STATUSQ_LIBRARY).
set(STATUSQ_LIBRARY "" CACHE FILEPATH "libStatusQ to link the replay entry point against")

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 6.11 COMPONENTS Core CorePrivate Gui Network Qml QmlPrivate Quick REQUIRED)

add_library(StatusReplay SHARED
    include/StatusReplay/fingerprint.h
    include/StatusReplay/media.h
    include/StatusReplay/player.h
    include/StatusReplay/probe.h
    include/StatusReplay/recorder.h
    include/StatusReplay/replayer.h
    include/StatusReplay/scriptrunner.h
    include/StatusReplay/settings.h
    include/StatusReplay/statusreplay.h
    src/capi.cpp
    src/fingerprint.cpp
    src/media.cpp
    src/player.cpp
    src/probe.cpp
    src/format.h
    src/recorder.cpp
    src/replayer.cpp
    src/replayobject.cpp
    src/replayobject.h
    src/schema.cpp
    src/schema.h
    src/scriptrunner.cpp
    src/settings.cpp
    src/valuecodec.cpp
    src/valuecodec.h
)

target_include_directories(StatusReplay PUBLIC include PRIVATE src)
target_link_libraries(StatusReplay
    PUBLIC Qt::Core Qt::Gui Qt::Network Qt::Qml Qt::Quick
    PRIVATE Qt::CorePrivate Qt::QmlPrivate)
target_compile_definitions(StatusReplay PRIVATE STATUSREPLAY_LIBRARY)

if(STATUSQ_LIBRARY)
    target_sources(StatusReplay PRIVATE
        include/StatusReplay/replayapp.h
        app/replaymain.cpp
        app/replaysetup.cpp
    )
    target_link_libraries(StatusReplay PRIVATE ${STATUSQ_LIBRARY})
    target_compile_definitions(StatusReplay PRIVATE STATUSREPLAY_WITH_STATUSQ)

    if(NOT ANDROID)
        add_executable(status-replay app/main.cpp)
        target_link_libraries(status-replay PRIVATE StatusReplay)
        set_target_properties(status-replay PROPERTIES BUILD_RPATH "$<TARGET_FILE_DIR:StatusReplay>")
    endif()
endif()

if(STATUSREPLAY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
