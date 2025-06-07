cmake_minimum_required(VERSION 3.10)
project(shiftXORLRCsim)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# 源文件目录
file(GLOB SOURCES 
    src/*.cpp
)

# 启用优化
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# 检测并启用AVX2支持
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-mavx2 COMPILER_SUPPORTS_AVX2)
if(COMPILER_SUPPORTS_AVX2)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx2")
    add_definitions(-DUSE_AVX2)
endif()

# 主可执行文件
add_executable(shiftXORLRCsim 
    ${SOURCES}
)

# 添加专门的简单版本
add_executable(shift_xor_simple 
    src/shift_xor_simple.cpp
)

# 添加系统化版本
add_executable(shift_xor_systematic 
    src/shift_xor_systematic.cpp
)

# 添加LRC版本
add_executable(shift_xor_lrc 
    src/shift_xor_lrc.cpp
)


# 添加基准测试版本（带AVX2支持）
add_executable(shift_xor_bench 
    src/shift_xor_bench.cpp
    src/utils.cpp
)

# 输出目录
set_target_properties(shiftXORLRCsim shift_xor_simple shift_xor_bench
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# 针对不同构建类型的设置
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(shiftXORLRCsim PRIVATE DEBUG_MODE)
    message(STATUS "Debug mode enabled")
else()
    message(STATUS "Release mode enabled")
endif()

# 安装规则
install(TARGETS shiftXORLRCsim shift_xor_simple shift_xor_bench
    RUNTIME DESTINATION bin
)

# 测试数据文件生成
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/bin/dummy.txt
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin
    COMMAND dd if=/dev/urandom of=${CMAKE_BINARY_DIR}/bin/dummy.txt bs=1k count=512
    COMMENT "Generating test data file"
    VERBATIM
)
add_custom_target(test_data DEPENDS ${CMAKE_BINARY_DIR}/bin/dummy.txt)
add_dependencies(shift_xor_simple test_data)
add_dependencies(shift_xor_bench test_data) 