find_package(Qt6 COMPONENTS Test REQUIRED)

add_executable(RoundTripTest tst_roundtrip.cpp fixtures.h)
target_link_libraries(RoundTripTest PRIVATE Qt::Test StatusReplay)
target_compile_definitions(RoundTripTest PRIVATE
    STATUSREPLAY_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/minimal")
add_test(NAME RoundTripTest COMMAND RoundTripTest)

add_executable(SettingsTest tst_settings.cpp)
target_link_libraries(SettingsTest PRIVATE Qt::Test StatusReplay)
add_test(NAME SettingsTest COMMAND SettingsTest)

add_executable(MediaTest tst_media.cpp)
target_link_libraries(MediaTest PRIVATE Qt::Test StatusReplay)
add_test(NAME MediaTest COMMAND MediaTest)

add_executable(PlayerTest tst_player.cpp)
target_link_libraries(PlayerTest PRIVATE Qt::Test StatusReplay)
add_test(NAME PlayerTest COMMAND PlayerTest)

find_package(Qt6 COMPONENTS CorePrivate REQUIRED)
add_executable(RecorderEdgesTest tst_recorderedges.cpp fixtures.h)
target_link_libraries(RecorderEdgesTest PRIVATE Qt::Test Qt::Gui Qt::Qml Qt::CorePrivate StatusReplay)
add_test(NAME RecorderEdgesTest COMMAND RecorderEdgesTest)
set_tests_properties(RecorderEdgesTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(QmlConsumerTest tst_qmlconsumer.cpp fixtures.h)
target_link_libraries(QmlConsumerTest PRIVATE Qt::Test Qt::Gui Qt::Qml Qt::Quick StatusReplay)
add_test(NAME QmlConsumerTest COMMAND QmlConsumerTest)
set_tests_properties(QmlConsumerTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

find_package(Qt6 COMPONENTS QuickControls2 REQUIRED)
add_executable(ScriptRunnerTest tst_scriptrunner.cpp)
target_link_libraries(ScriptRunnerTest PRIVATE Qt::Test Qt::Gui Qt::Quick Qt::QuickControls2 StatusReplay)
add_test(NAME ScriptRunnerTest COMMAND ScriptRunnerTest)
set_tests_properties(ScriptRunnerTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

if(STATUSQ_LIBRARY)
    add_executable(SetupParityTest tst_setupparity.cpp)
    target_link_libraries(SetupParityTest PRIVATE Qt::Test Qt::Gui Qt::Qml StatusReplay)
    target_compile_definitions(SetupParityTest PRIVATE
        STATUSREPLAY_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/minimal"
        STATUSREPLAY_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../..")
    add_test(NAME SetupParityTest COMMAND SetupParityTest)
endif()
