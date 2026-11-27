cmake_minimum_required(VERSION 3.16)
project(MiniExchange VERSION 2.0 LANGUAGES CXX)

# ── C++17 everywhere ──────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Default to Release if the user didn't pass -DCMAKE_BUILD_TYPE ─────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ── Compiler warning flags ────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ── Library: everything except main.cpp ───────────────────────────────────────
# Building a static library lets us link it against tests later (next step).
add_library(exchange_lib STATIC
    src/Order.cpp
    src/Trade.cpp
    src/TraderReport.cpp
    src/OrderBook.cpp
    src/Exchange.cpp
)
target_include_directories(exchange_lib PUBLIC include)

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(exchange src/main.cpp)
target_link_libraries(exchange PRIVATE exchange_lib)

# ── Useful info at configure time ─────────────────────────────────────────────
message(STATUS "Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler   : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
