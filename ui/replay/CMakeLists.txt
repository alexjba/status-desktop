cmake_minimum_required(VERSION 3.19)

project(StatusReplay LANGUAGES CXX)

option(STATUSREPLAY_BUILD_TESTS "Build StatusReplay host tests" ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt6 6.11 COMPONENTS Core CorePrivate Qml QmlPrivate REQUIRED)

add_library(StatusReplay SHARED
    include/StatusReplay/recorder.h
    include/StatusReplay/replayer.h
    include/StatusReplay/statusreplay.h
    src/capi.cpp
    src/format.h
    src/recorder.cpp
    src/replayer.cpp
    src/replayobject.cpp
    src/replayobject.h
    src/schema.cpp
    src/schema.h
    src/valuecodec.cpp
    src/valuecodec.h
)

target_include_directories(StatusReplay PUBLIC include PRIVATE src)
target_link_libraries(StatusReplay
    PUBLIC Qt::Core Qt::Qml
    PRIVATE Qt::CorePrivate Qt::QmlPrivate)
target_compile_definitions(StatusReplay PRIVATE STATUSREPLAY_LIBRARY)

if(STATUSREPLAY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
