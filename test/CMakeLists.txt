cmake_minimum_required(VERSION 3.14)

project(vdr_pi_tests)
set(CMAKE_CXX_STANDARD 17)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
message(STATUS "Building VDR plugin tests")

add_library(win32_libs INTERFACE)
if (MSVC)
    target_link_libraries(win32_libs
        INTERFACE
            setupapi.lib psapi.lib
            ${CMAKE_SOURCE_DIR}/cache/buildwin/iphlpapi.lib
    )
endif ()

# Find required packages
find_package(GTest REQUIRED)
find_package(wxWidgets COMPONENTS core base net aui REQUIRED)

add_test(NAME vdr_tests COMMAND vdr_tests)

set(SRC
    time_tests.cpp
    plugin_tests.cpp
    record_tests.cpp
    mock_plugin_api.cpp
    mock_plugin_impl.cpp
    ${CMAKE_SOURCE_DIR}/src/vdr_pi_time.cpp
    ${CMAKE_SOURCE_DIR}/src/vdr_pi.cpp
    ${CMAKE_SOURCE_DIR}/src/vdr_pi_prefs.cpp
    ${CMAKE_SOURCE_DIR}/src/vdr_pi_prefs_net.cpp
    ${CMAKE_SOURCE_DIR}/src/vdr_pi_control.cpp
    ${CMAKE_SOURCE_DIR}/src/vdr_network.cpp
    ${CMAKE_SOURCE_DIR}/src/icons.cpp
    ${CMAKE_SOURCE_DIR}/src/record_play_mgr.cpp
    ${CMAKE_SOURCE_DIR}/src/dm_replay_mgr.cpp
)

add_executable(vdr_tests ${SRC})

target_compile_definitions(vdr_tests
    PUBLIC
        USE_MOCK_DEFS CMAKE_BINARY_DIR="${CMAKE_BINARY_DIR}"
        TESTDATA="${CMAKE_CURRENT_LIST_DIR}/testdata"
        UNIT_TESTS
)

# Include directories
target_include_directories(vdr_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/opencpn-libs/${PKG_API_LIB}/include
        ${GTEST_INCLUDE_DIRS}
        ${wxWidgets_INCLUDE_DIRS}
)

# Link against required libraries
target_link_libraries(vdr_tests
    PRIVATE
        GTest::GTest
        GTest::Main
        ${wxWidgets_LIBRARIES}
        ocpn::api
        csv-parser::csv-parser
)

# Set optimization level for debug builds
if (NOT MSVC)
    target_compile_options(vdr_tests PUBLIC "-O0")
endif ()

# Add the test

# Add a custom target to run tests with more details
add_custom_target(run-tests
    COMMAND ${CMAKE_CTEST_COMMAND} -V
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS vdr_tests
)
