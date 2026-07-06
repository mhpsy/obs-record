cmake_minimum_required(VERSION 3.20)
project(obs-record VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall -Wextra)

find_package(libobs REQUIRED)            # /usr/lib/cmake/libobs 已确认存在
find_package(nlohmann_json REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(INPUT REQUIRED IMPORTED_TARGET libinput libudev)
find_package(Threads REQUIRED)

# 纯逻辑核心:零 OBS 依赖,插件和测试共用
add_library(rec-core STATIC
    # 后续任务往这里追加: src/hypr-ipc.cpp
    src/effects-state.cpp
    src/commands.cpp)
target_include_directories(rec-core PUBLIC src)
target_link_libraries(rec-core PUBLIC nlohmann_json::nlohmann_json Threads::Threads)

add_library(obs-record MODULE
    src/plugin-main.cpp)
set_target_properties(obs-record PROPERTIES PREFIX "")
target_link_libraries(obs-record PRIVATE OBS::libobs rec-core PkgConfig::INPUT)

install(TARGETS obs-record
    LIBRARY DESTINATION "$ENV{HOME}/.config/obs-studio/plugins/obs-record/bin/64bit")
install(FILES data/spotlight.effect
    DESTINATION "$ENV{HOME}/.config/obs-studio/plugins/obs-record/data" OPTIONAL)

enable_testing()
function(rec_add_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE rec-core)
    target_include_directories(${name} PRIVATE tests)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

rec_add_test(test-sanity)
rec_add_test(test-geometry)
rec_add_test(test-commands)
rec_add_test(test-zoom)
rec_add_test(test-effects)
