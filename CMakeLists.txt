cmake_minimum_required(VERSION 3.15)
project(MarketSimulator VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Fetch nlohmann/json ────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# ── Collect sources ────────────────────────────────────────────────────────────
file(GLOB_RECURSE SOURCES "src/*.cpp")

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(market_sim ${SOURCES})

target_include_directories(market_sim PRIVATE src)
target_link_libraries(market_sim PRIVATE nlohmann_json::nlohmann_json)

# ── Compiler warnings ─────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(market_sim PRIVATE /W3)
else()
    target_compile_options(market_sim PRIVATE -Wall -Wextra -O2)
endif()

# ── Copy config to build dir ──────────────────────────────────────────────────
file(COPY config DESTINATION ${CMAKE_BINARY_DIR})
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/output)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/data)
