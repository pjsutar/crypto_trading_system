cmake_minimum_required(VERSION 3.20)
project(crypto_trading_system VERSION 0.1.0 LANGUAGES CXX)

# ── C++17 standard, required across all targets ──────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler flags ────────────────────────────────────────────────
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wno-unused-parameter
    -O2
    $<$<CONFIG:Debug>:-g>
    $<$<CONFIG:Release>:-O3>
    $<$<CONFIG:Release>:-march=native>
    $<$<CONFIG:Release>:-DNDEBUG>
)

# ── Dependencies ──────────────────────────────────────────────────
find_package(Threads REQUIRED)

find_package(Boost 1.83 REQUIRED
    COMPONENTS
        system
        thread
)

find_package(OpenSSL REQUIRED)

find_package(nlohmann_json REQUIRED)

# SQLite3 — found via pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

# ── Source files per layer ────────────────────────────────────────
set(DATA_LAYER_SOURCES
    src/data/BinanceWebSocketClient.cpp
    src/data/OrderBook.cpp
    src/data/LatencyLogger.cpp
)

# Layers added here as we build them:
# set(FEATURE_ENGINE_SOURCES src/features/...)
# set(SIGNAL_LAYER_SOURCES   src/signal/...)
# set(EXECUTION_SOURCES      src/execution/...)

# ── Main executable ───────────────────────────────────────────────
add_executable(crypto_trader
    src/main.cpp
    ${DATA_LAYER_SOURCES}
)

target_include_directories(crypto_trader
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${Boost_INCLUDE_DIRS}
        ${SQLITE3_INCLUDE_DIRS}
)

target_link_libraries(crypto_trader
    PRIVATE
        Threads::Threads
        Boost::system
        Boost::thread
        OpenSSL::SSL
        OpenSSL::Crypto
        nlohmann_json::nlohmann_json
        ${SQLITE3_LIBRARIES}
)

# ── Tests ─────────────────────────────────────────────────────────
enable_testing()
add_subdirectory(tests)

# ── Build info output ─────────────────────────────────────────────
message(STATUS "Build type:    ${CMAKE_BUILD_TYPE}")
message(STATUS "Boost version: ${Boost_VERSION}")
message(STATUS "OpenSSL:       ${OPENSSL_VERSION}")