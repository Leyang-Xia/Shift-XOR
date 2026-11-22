cmake_minimum_required(VERSION 3.16)
project(shiftxor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SHIFT_XOR_ENABLE_AVX2 "Build the AVX2 backend when supported" ON)
option(SHIFT_XOR_ENABLE_ISAL "Build the optional ISA-L adapter" OFF)
option(SHIFT_XOR_ENABLE_LONGHAIR "Build the optional Longhair adapter" OFF)
option(SHIFT_XOR_BUILD_LEGACY "Build the historical standalone experiments" ON)

enable_testing()

add_library(shiftxor
    src/shiftxor/matrix.cpp
    src/shiftxor/codec.cpp
    src/shiftxor/backend_scalar.cpp
)
target_include_directories(shiftxor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(shiftxor PUBLIC cxx_std_17)

include(CheckCXXCompilerFlag)
if(MSVC)
    set(SHIFT_XOR_AVX2_FLAG "/arch:AVX2")
else()
    set(SHIFT_XOR_AVX2_FLAG "-mavx2")
endif()
check_cxx_compiler_flag("${SHIFT_XOR_AVX2_FLAG}" SHIFT_XOR_COMPILER_SUPPORTS_AVX2)

if(SHIFT_XOR_ENABLE_AVX2 AND SHIFT_XOR_COMPILER_SUPPORTS_AVX2)
    target_sources(shiftxor PRIVATE src/shiftxor/backend_avx2.cpp)
    target_compile_definitions(shiftxor PUBLIC SHIFT_XOR_HAS_AVX2=1)
    set_source_files_properties(src/shiftxor/backend_avx2.cpp PROPERTIES COMPILE_OPTIONS "${SHIFT_XOR_AVX2_FLAG}")
endif()

add_executable(shiftxor_tests
    tests/test_matrix.cpp
)
target_link_libraries(shiftxor_tests PRIVATE shiftxor)
add_test(NAME shiftxor_tests COMMAND shiftxor_tests)

add_executable(shiftxor_codec_tests
    tests/test_codec.cpp
)
target_link_libraries(shiftxor_codec_tests PRIVATE shiftxor)
add_test(NAME shiftxor_codec_tests COMMAND shiftxor_codec_tests)

add_executable(shiftxor_roundtrip_tests tests/test_roundtrip_cases.cpp)
target_link_libraries(shiftxor_roundtrip_tests PRIVATE shiftxor)
add_test(NAME shiftxor_roundtrip_cases COMMAND shiftxor_roundtrip_tests)

add_executable(shiftxor_backend_tests
    tests/test_backend.cpp
)
target_link_libraries(shiftxor_backend_tests PRIVATE shiftxor)
add_test(NAME shiftxor_backend_tests COMMAND shiftxor_backend_tests)

add_library(shiftxor_bench_lib
    bench/shiftxor_adapter.cpp
    bench/isa_l_adapter.cpp
    bench/longhair_adapter.cpp
)
target_include_directories(shiftxor_bench_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(shiftxor_bench_lib PUBLIC shiftxor)

if(SHIFT_XOR_ENABLE_ISAL)
    find_path(SHIFT_XOR_ISAL_INCLUDE_DIR erasure_code.h
              HINTS ${ISA_L_ROOT} ENV ISA_L_ROOT
              PATH_SUFFIXES include)
    find_library(SHIFT_XOR_ISAL_LIBRARY NAMES isal
                 HINTS ${ISA_L_ROOT} ENV ISA_L_ROOT
                 PATH_SUFFIXES lib)
    if(SHIFT_XOR_ISAL_INCLUDE_DIR AND SHIFT_XOR_ISAL_LIBRARY)
        target_compile_definitions(shiftxor_bench_lib PRIVATE SHIFT_XOR_HAVE_ISAL=1)
        target_include_directories(shiftxor_bench_lib PRIVATE ${SHIFT_XOR_ISAL_INCLUDE_DIR})
        target_link_libraries(shiftxor_bench_lib PRIVATE ${SHIFT_XOR_ISAL_LIBRARY})
    else()
        message(STATUS "ISA-L not found, building an unavailable adapter")
    endif()
endif()

if(SHIFT_XOR_ENABLE_LONGHAIR)
    find_path(SHIFT_XOR_LONGHAIR_INCLUDE_DIR cauchy_256.h
              HINTS ${LONGHAIR_ROOT} ENV LONGHAIR_ROOT
              PATH_SUFFIXES include)
    find_library(SHIFT_XOR_LONGHAIR_LIBRARY NAMES longhair
                 HINTS ${LONGHAIR_ROOT} ENV LONGHAIR_ROOT
                 PATH_SUFFIXES lib)
    if(SHIFT_XOR_LONGHAIR_INCLUDE_DIR AND SHIFT_XOR_LONGHAIR_LIBRARY)
        target_compile_definitions(shiftxor_bench_lib PRIVATE SHIFT_XOR_HAVE_LONGHAIR=1)
        target_include_directories(shiftxor_bench_lib PRIVATE ${SHIFT_XOR_LONGHAIR_INCLUDE_DIR})
        target_link_libraries(shiftxor_bench_lib PRIVATE ${SHIFT_XOR_LONGHAIR_LIBRARY})
    else()
        message(STATUS "Longhair not found, building an unavailable adapter")
    endif()
endif()

add_executable(shiftxor_bench bench/shiftxor_bench.cpp)
target_link_libraries(shiftxor_bench PRIVATE shiftxor_bench_lib)
set_target_properties(shiftxor_bench PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(shiftxor_benchmark_tests tests/test_benchmark.cpp)
target_link_libraries(shiftxor_benchmark_tests PRIVATE shiftxor_bench_lib)
add_test(NAME shiftxor_benchmark_tests COMMAND shiftxor_benchmark_tests)

add_executable(shift_xor_9_6 examples/shift_xor_9_6.cpp)
target_link_libraries(shift_xor_9_6 PRIVATE shiftxor)
set_target_properties(shift_xor_9_6 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(shiftxor_example_tests tests/test_examples.cpp)
add_test(NAME shiftxor_example COMMAND shiftxor_example_tests $<TARGET_FILE:shift_xor_9_6>)

if(SHIFT_XOR_BUILD_LEGACY)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/bin/dummy.txt
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/legacy/dummy.txt
                ${CMAKE_BINARY_DIR}/bin/dummy.txt
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_SOURCE_DIR}/legacy/dummy.txt
                ${CMAKE_BINARY_DIR}/dummy.txt
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/legacy/dummy.txt
        VERBATIM)
    add_custom_target(shiftxor_legacy_data DEPENDS ${CMAKE_BINARY_DIR}/bin/dummy.txt)

    add_executable(shift_xor_simple legacy/shift_xor_simple.cpp)
    add_executable(shift_xor_systematic legacy/shift_xor_systematic.cpp)
    add_executable(shift_xor_lrc legacy/shift_xor_lrc.cpp)
    add_executable(shift_xor_9_6_simple legacy/shift_xor_9_6_simple.cpp)
    set(legacy_targets shift_xor_simple shift_xor_systematic shift_xor_lrc shift_xor_9_6_simple)
    if(SHIFT_XOR_ENABLE_AVX2 AND SHIFT_XOR_COMPILER_SUPPORTS_AVX2)
        add_executable(shift_xor_bench legacy/shift_xor_bench.cpp legacy/utils.cpp)
        add_executable(shift_xor_lrc_avx2 legacy/shift_xor_lrc_avx2.cpp)
        add_executable(shift_xor_9_6_avx2 legacy/shift_xor_9_6_avx2.cpp)
        list(APPEND legacy_targets shift_xor_bench shift_xor_lrc_avx2 shift_xor_9_6_avx2)
        target_compile_options(shift_xor_bench PRIVATE "${SHIFT_XOR_AVX2_FLAG}")
        target_compile_options(shift_xor_lrc_avx2 PRIVATE "${SHIFT_XOR_AVX2_FLAG}")
        target_compile_options(shift_xor_9_6_avx2 PRIVATE "${SHIFT_XOR_AVX2_FLAG}")
    endif()
    foreach(target_name IN LISTS legacy_targets)
        target_compile_features(${target_name} PRIVATE cxx_std_17)
        add_dependencies(${target_name} shiftxor_legacy_data)
    endforeach()
endif()


