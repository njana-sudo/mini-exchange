cmake_minimum_required(VERSION 3.16)
project(MiniExchange VERSION 2.0 LANGUAGES CXX)

# ── C++17 & compile_commands.json ─────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Default to Release if the user didn't pass -DCMAKE_BUILD_TYPE ─────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ── Build options ─────────────────────────────────────────────────────────────
option(BUILD_TESTING       "Build GoogleTest unit tests (pass OFF for offline builds)" ON)
option(ENABLE_MARCH_NATIVE "Enable -march=native for host CPU tuning"                OFF)
option(ENABLE_SANITIZERS   "Enable ASan+UBSan (or _GLIBCXX_DEBUG fallback on MinGW)"  OFF)
option(ENABLE_LTO          "Enable link-time optimization in Release builds"          ON)

# ── Project warning & sanitizer flags (scoped to our own targets) ─────────────
set(PROJECT_COMPILE_FLAGS "")
set(PROJECT_LINK_FLAGS "")

if(MSVC)
    list(APPEND PROJECT_COMPILE_FLAGS /W4)
else()
    list(APPEND PROJECT_COMPILE_FLAGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    if(ENABLE_MARCH_NATIVE)
        list(APPEND PROJECT_COMPILE_FLAGS -march=native)
    endif()
    if(ENABLE_SANITIZERS)
        include(CheckCXXCompilerFlag)
        set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=address,undefined")
        check_cxx_compiler_flag("-fsanitize=address,undefined" HAS_ASAN_UBSAN)
        unset(CMAKE_REQUIRED_LINK_OPTIONS)
        if(HAS_ASAN_UBSAN)
            list(APPEND PROJECT_COMPILE_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
            list(APPEND PROJECT_LINK_FLAGS -fsanitize=address,undefined)
        else()
            message(STATUS "ASan/UBSan runtime not available on this toolchain; enabling _GLIBCXX_DEBUG iterator checks")
            list(APPEND PROJECT_COMPILE_FLAGS -D_GLIBCXX_DEBUG -fno-omit-frame-pointer)
        endif()
    endif()
endif()

# ── Link-Time Optimization (LTO) for Release builds ───────────────────────────
if(ENABLE_LTO AND CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT ENABLE_SANITIZERS)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
    if(ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    else()
        message(STATUS "LTO not supported: ${ipo_error}")
    endif()
endif()

# ── Library: everything except main.cpp ───────────────────────────────────────
add_library(exchange_lib STATIC
    src/Order.cpp
    src/Trade.cpp
    src/TraderLedger.cpp
    src/TraderReport.cpp
    src/OrderJournal.cpp
    src/OrderBook.cpp
    src/Exchange.cpp
)
target_include_directories(exchange_lib PUBLIC include)
target_compile_options(exchange_lib PRIVATE ${PROJECT_COMPILE_FLAGS})
target_link_options(exchange_lib PUBLIC ${PROJECT_LINK_FLAGS})

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(exchange src/main.cpp)
target_link_libraries(exchange PRIVATE exchange_lib)
target_compile_options(exchange PRIVATE ${PROJECT_COMPILE_FLAGS})

# ── Unit Tests (GoogleTest: local first, FetchContent fallback) ───────────────
if(BUILD_TESTING)
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24")
            FetchContent_Declare(
                googletest
                URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
                DOWNLOAD_EXTRACT_TIMESTAMP TRUE
            )
        else()
            FetchContent_Declare(
                googletest
                URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
            )
        endif()
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    enable_testing()
    add_executable(exchange_tests tests/exchange_test.cpp)
    target_link_libraries(exchange_tests PRIVATE exchange_lib GTest::gtest_main)
    target_compile_options(exchange_tests PRIVATE ${PROJECT_COMPILE_FLAGS})

    include(GoogleTest)
    gtest_discover_tests(exchange_tests)
endif()

# ── Useful info at configure time ─────────────────────────────────────────────
message(STATUS "Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler   : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Tests      : ${BUILD_TESTING}")
message(STATUS "Sanitizers : ${ENABLE_SANITIZERS}")
