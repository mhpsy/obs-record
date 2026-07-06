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
    src/hypr-ipc.cpp
    src/effects-state.cpp
    src/commands.cpp
    src/cursor-tracker.cpp
    src/control-server.cpp)
target_include_directories(rec-core PUBLIC src)
target_link_libraries(rec-core PUBLIC nlohmann_json::nlohmann_json Threads::Threads)
# obs-record(MODULE, 需 -fPIC)静态链接本库;之前任务从未真正从 obs-record.so
# 拉取本库符号(旧 plugin-main.cpp 未引用),故此问题此前未暴露。Task 10 的
# filter.cpp 首次真正引用 CursorTracker/ControlServer/EffectsState 等符号,
# 链接器才会把这些 .o 拉入 obs-record.so,若无 -fPIC 会报重定位错误。
set_target_properties(rec-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(obs-record MODULE
    src/plugin-main.cpp
    src/filter.cpp
    src/renderer.cpp
    src/input-listener.cpp)
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
rec_add_test(test-hypr-parse)
rec_add_test(test-sockets)

add_executable(click-probe tests/click-probe.cpp src/input-listener.cpp)
target_link_libraries(click-probe PRIVATE rec-core PkgConfig::INPUT)
target_include_directories(click-probe PRIVATE src)
