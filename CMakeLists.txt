cmake_minimum_required(VERSION 3.20)
project(MedBook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(MEDBOOK_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer" ON)

add_compile_options(-Wall -Werror)
if(MEDBOOK_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Core library shared by the executable and the tests.
add_library(medbook_core STATIC
    order/order.cpp
    price_level/price_level.cpp
    order_pool/order_pool.cpp
    order_book/order_book.cpp
    matching_engine/matching_engine.cpp
    feed_source/feed_source.cpp
)
target_include_directories(medbook_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Replay + terminal dashboard (POSIX terminal APIs: macOS and Linux).
add_executable(main main.cpp monitor/dashboard.cpp)
target_link_libraries(main PRIVATE medbook_core)

# ---------- tests ----------
option(MEDBOOK_BUILD_TESTS "Build unit tests" ON)
if(MEDBOOK_BUILD_TESTS)
    # Use an installed GoogleTest if there is one, otherwise download it.
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        message(STATUS "GoogleTest not installed; fetching it")
        include(FetchContent)
        FetchContent_Declare(googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    enable_testing()
    include(GoogleTest)

    foreach(name test_order test_price_level test_order_pool test_red_black_tree test_order_book test_matching_engine test_feed_source)
        add_executable(${name} tests/${name}.cpp)
        target_link_libraries(${name} PRIVATE medbook_core GTest::gtest_main)
        target_compile_definitions(${name} PRIVATE MEDBOOK_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        gtest_discover_tests(${name})
    endforeach()
endif()
