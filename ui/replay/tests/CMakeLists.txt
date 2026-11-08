find_package(Qt6 COMPONENTS Test REQUIRED)

add_executable(RoundTripTest tst_roundtrip.cpp fixtures.h)
target_link_libraries(RoundTripTest PRIVATE Qt::Test StatusReplay)
add_test(NAME RoundTripTest COMMAND RoundTripTest)
