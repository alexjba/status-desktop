find_package(Qt6 COMPONENTS Test REQUIRED)

add_executable(RoundTripTest tst_roundtrip.cpp fixtures.h)
target_link_libraries(RoundTripTest PRIVATE Qt::Test StatusReplay)
target_compile_definitions(RoundTripTest PRIVATE
    STATUSREPLAY_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/minimal")
add_test(NAME RoundTripTest COMMAND RoundTripTest)

add_executable(QmlConsumerTest tst_qmlconsumer.cpp fixtures.h)
target_link_libraries(QmlConsumerTest PRIVATE Qt::Test Qt::Gui Qt::Qml Qt::Quick StatusReplay)
add_test(NAME QmlConsumerTest COMMAND QmlConsumerTest)
set_tests_properties(QmlConsumerTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

if(STATUSQ_LIBRARY)
    add_executable(SetupParityTest tst_setupparity.cpp)
    target_link_libraries(SetupParityTest PRIVATE Qt::Test Qt::Gui Qt::Qml StatusReplay)
    target_compile_definitions(SetupParityTest PRIVATE
        STATUSREPLAY_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/minimal"
        STATUSREPLAY_REPO_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../../..")
    add_test(NAME SetupParityTest COMMAND SetupParityTest)
endif()
