cmake_minimum_required(VERSION 3.16)
project(Qode
        VERSION 0.1.0
        DESCRIPTION "High-performance multi-symbol electronic order matching engine"
        LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
                 Debug Release RelWithDebInfo MinSizeRel)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(OME_ENABLE_WARNINGS "Enable strict compiler warnings"   ON)
option(OME_ENABLE_WERROR   "Treat warnings as errors"          OFF)
option(OME_ENABLE_ASAN     "Enable AddressSanitizer"           OFF)
option(OME_ENABLE_UBSAN    "Enable UndefinedBehaviorSanitizer" OFF)
option(OME_ENABLE_TSAN     "Enable ThreadSanitizer"            OFF)
option(OME_BUILD_TESTS     "Build unit tests"                  ON)
option(OME_BUILD_BENCH     "Build benchmark program"           ON)

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Interface target carrying warning + sanitizer flags
# ---------------------------------------------------------------------------
add_library(ome_project_options INTERFACE)

if(OME_ENABLE_WARNINGS)
    if(MSVC)
        target_compile_options(ome_project_options INTERFACE /W4 /permissive-)
        if(OME_ENABLE_WERROR)
            target_compile_options(ome_project_options INTERFACE /WX)
        endif()
    else()
        target_compile_options(ome_project_options INTERFACE
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
        if(OME_ENABLE_WERROR)
            target_compile_options(ome_project_options INTERFACE -Werror)
        endif()
    endif()
endif()

if(NOT MSVC)
    set(_ome_san_flags "")
    if(OME_ENABLE_ASAN)
        list(APPEND _ome_san_flags -fsanitize=address -fno-omit-frame-pointer)
    endif()
    if(OME_ENABLE_UBSAN)
        list(APPEND _ome_san_flags -fsanitize=undefined -fno-omit-frame-pointer)
    endif()
    if(OME_ENABLE_TSAN)
        list(APPEND _ome_san_flags -fsanitize=thread -fno-omit-frame-pointer)
    endif()
    if(_ome_san_flags)
        target_compile_options(ome_project_options INTERFACE ${_ome_san_flags})
        target_link_options(ome_project_options INTERFACE ${_ome_san_flags})
    endif()
endif()

# ---------------------------------------------------------------------------
# Core engine library
# ---------------------------------------------------------------------------
add_library(engine STATIC
    src/OrderBook.cpp
    src/OrderIdGenerator.cpp
    src/MatchEngine.cpp
    src/SymbolWorker.cpp
    src/Exchange.cpp
    src/Sink.cpp
    src/OrderList.cpp
)
target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(engine PUBLIC cxx_std_20)
target_link_libraries(engine
    PUBLIC  Threads::Threads
    PRIVATE ome_project_options)

add_executable(engine_app app/main.cpp)
target_link_libraries(engine_app PRIVATE engine ome_project_options)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(OME_BUILD_TESTS)
    enable_testing()
    add_executable(engine_tests
        tests/test_main.cpp
        tests/test_framework.cpp
        tests/domain_tests.cpp
        tests/integration_tests.cpp
    )
    target_link_libraries(engine_tests PRIVATE engine ome_project_options)
    target_include_directories(engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    add_test(NAME engine_tests COMMAND engine_tests)
endif()

# ---------------------------------------------------------------------------
# Benchmark (source added in a later milestone)
# ---------------------------------------------------------------------------
if(OME_BUILD_BENCH AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/bench/benchmark.cpp)
    add_executable(engine_bench bench/benchmark.cpp)
    target_link_libraries(engine_bench PRIVATE engine ome_project_options)
endif()
