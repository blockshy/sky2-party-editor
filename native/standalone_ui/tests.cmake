# 由各独立项目设置 SKY2_SOLO_IMGUI_TARGET 后引入。只在合成进程执行生产壳与
# 合作输入协议；不会启动游戏、安装挂钩或读取玩家文件。
set(_solo_dir "${CMAKE_CURRENT_LIST_DIR}")
add_executable(sky2solo_window_tests "${_solo_dir}/window_tests.cpp"
    "${_solo_dir}/ui.cpp" "${_solo_dir}/ui_shell.cpp" "${_solo_dir}/ui_layout.cpp" "${_solo_dir}/ui_navigation.cpp")
target_link_libraries(sky2solo_window_tests PRIVATE ${SKY2_SOLO_IMGUI_TARGET})
target_compile_definitions(sky2solo_window_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(sky2solo_window_tests PRIVATE /utf-8 /EHsc)
add_test(NAME sky2solo_window_tests COMMAND sky2solo_window_tests)
foreach(_index RANGE 1 2)
    add_library(sky2solo_input_fixture${_index} SHARED "${_solo_dir}/input_fixture.cpp" "${_solo_dir}/input.cpp")
    target_compile_definitions(sky2solo_input_fixture${_index} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(sky2solo_input_fixture${_index} PRIVATE /utf-8 /EHsc)
endforeach()
add_executable(sky2solo_input_chain_tests "${_solo_dir}/input_chain_tests.cpp" "${_solo_dir}/input.cpp")
target_compile_definitions(sky2solo_input_chain_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(sky2solo_input_chain_tests PRIVATE /utf-8 /EHsc)
add_test(NAME sky2solo_input_chain_tests COMMAND sky2solo_input_chain_tests
    "$<TARGET_FILE:sky2solo_input_fixture1>" "$<TARGET_FILE:sky2solo_input_fixture2>")

# 快捷键配置与冲突事务使用真实文件和三份独立链接的引擎。测试目录由夹具
# 创建并清理，不接触游戏配置；每个启动场景另起进程，符合每 DLL 一次初始化。
foreach(_index RANGE 1 2)
    add_library(sky2solo_hotkeys_fixture${_index} SHARED "${_solo_dir}/hotkeys_fixture.cpp"
        "${_solo_dir}/hotkeys.cpp" "${_solo_dir}/input.cpp")
    target_compile_features(sky2solo_hotkeys_fixture${_index} PRIVATE cxx_std_17)
    target_compile_definitions(sky2solo_hotkeys_fixture${_index} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(sky2solo_hotkeys_fixture${_index} PRIVATE /utf-8 /EHsc)
endforeach()
add_executable(sky2solo_hotkeys_tests "${_solo_dir}/hotkeys_tests.cpp" "${_solo_dir}/hotkeys.cpp" "${_solo_dir}/input.cpp")
target_compile_features(sky2solo_hotkeys_tests PRIVATE cxx_std_17)
target_compile_definitions(sky2solo_hotkeys_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(sky2solo_hotkeys_tests PRIVATE /utf-8 /EHsc)
foreach(_case IN ITEMS config bad link input)
    add_test(NAME sky2solo_hotkeys_${_case} COMMAND sky2solo_hotkeys_tests ${_case})
endforeach()
add_test(NAME sky2solo_hotkeys_cross_dll COMMAND sky2solo_hotkeys_tests cross
    "$<TARGET_FILE:sky2solo_hotkeys_fixture1>" "$<TARGET_FILE:sky2solo_hotkeys_fixture2>")
