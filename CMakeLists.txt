cmake_minimum_required(VERSION 3.4...3.18)
project(cmake_example)

# 开启编译器输出编译命令，方便调试
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(pybind11)
pybind11_add_module(cmake_example src/main.cpp)

# EXAMPLE_VERSION_INFO is defined by setup.py and passed into the C++ code as a
# define (VERSION_INFO) here.
target_compile_definitions(cmake_example
    PRIVATE VERSION_INFO=${EXAMPLE_VERSION_INFO})

# 使用 FindPython 模块同时查找解释器、开发环境
# COMPONENTS: 列出需要查找的组件
# Interpreter: 查找 Python 解释器 (设置 Python_EXECUTABLE 等变量)
# Development: 查找 Python 头文件和库 (设置 Python_INCLUDE_DIRS, Python_LIBRARIES 等变量)
# REQUIRED: 表示所有列出的组件都是必需的，找不到就中止配置
find_package(Python COMPONENTS Interpreter Development REQUIRED)

# 检查是否成功找到所有组件，并打印信息
if(Python_FOUND)
    message(STATUS "Successfully found all required Python components:")
    message(STATUS "  Interpreter: ${Python_EXECUTABLE} (version ${Python_VERSION_STRING})")
    message(STATUS "  Includes:    ${Python_INCLUDE_DIRS}")
    message(STATUS "  Libraries:   ${Python_LIBRARIES}")
else()
    # 由于 REQUIRED，这里通常不会被执行
    message(FATAL_ERROR "Could not find required Python components (Interpreter, Development).")
endif()

# 添加一个可执行目标
add_executable(demo1 src/demo/demo1.cpp)

# 为可执行目标添加 Python 头文件目录
target_include_directories(demo1
    PRIVATE
    ${Python_INCLUDE_DIRS}
    pybind11/include # 使用新的 FindPython 模块设置的变量 # 使用新的 FindPython 模块设置的变量
)

# 将 Python 库链接到可执行目标
target_link_libraries(demo1
    PRIVATE
    ${Python_LIBRARIES} # 使用新的 FindPython 模块设置的变量
)

message(STATUS "CMAKE_CURRENT_BINARY_DIR: ${CMAKE_CURRENT_BINARY_DIR}, CMAKE_CURRENT_SOURCE_DIR: ${CMAKE_CURRENT_SOURCE_DIR}")

# 定义一个自定义命令，将 sum.py 从源目录复制到构建目录
# OUTPUT: 指定命令生成的输出文件
# COMMAND: 要执行的命令。CMAKE_COMMAND -E copy_if_different 是 CMake 内置的跨平台复制命令。
# DEPENDS: 指定该命令依赖的源文件。当源文件更改时，命令将重新运行。
add_custom_command(
    OUTPUT
    ${CMAKE_CURRENT_BINARY_DIR}/src/demo/sum.py # 输出文件路径 (构建目录)
    COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/src/demo/sum.py # 源文件路径
    ${CMAKE_CURRENT_BINARY_DIR}/src/demo/sum.py # 目标文件路径
    DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/demo/sum.py
    COMMENT "Copying sum.py to build directory" # 构建时显示的消息
)

# 将复制的 Python 文件添加为可执行目标的源文件之一。
# 这不是为了编译它，而是为了确保在构建 pureEmbedding 目标时，上面的 add_custom_command 会被触发。
# CMake 会自动识别这个依赖关系：目标 pureEmbedding 依赖于文件 sum.py，
# 而 sum.py 是由上面的 custom command 生成的。
target_sources(demo1
    PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/src/demo/sum.py
)


# 添加一个可执行目标
add_executable(demo2 src/demo/demo2.cpp)

# 为可执行目标添加 Python 头文件目录
target_include_directories(demo2
    PRIVATE
    ${Python_INCLUDE_DIRS}
    pybind11/include # 使用新的 FindPython 模块设置的变量 # 使用新的 FindPython 模块设置的变量
)

# 将 Python 库链接到可执行目标
target_link_libraries(demo2
    PRIVATE
    ${Python_LIBRARIES} # 使用新的 FindPython 模块设置的变量
)