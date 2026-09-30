cmake_minimum_required(VERSION 3.20)
project(Sky2PartyEditor VERSION 0.6.0 LANGUAGES C CXX)

# 纯逻辑测试不需要游戏资源；插件仅支持已审计的 Windows x64 构建。
option(PARTY_BUILD_MOD "同时构建独立 XInput 版与 ASI 队伍插件" ON)
set(PARTY_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps" CACHE PATH "锁定版本的第三方源码目录")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
include(CTest)
if(MSVC)
    # 正式包不携带开发者的工作目录或绝对 PDB 路径；第三方静态库也采用同一映射。
    add_compile_options(/Brepro /experimental:deterministic "/pathmap:${CMAKE_CURRENT_SOURCE_DIR}=.")
endif()

if(PARTY_BUILD_MOD)
    if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "Mod requires Windows x64 and MSVC; use -DPARTY_BUILD_MOD=OFF for portable tests.")
    endif()
    if(NOT EXISTS "${PARTY_DEPS_DIR}/minhook/CMakeLists.txt")
        message(FATAL_ERROR "MinHook source missing. Run tools/Setup-Dependencies.ps1 or set PARTY_DEPS_DIR.")
    endif()
    # 原生菜单函数挂钩复用锁定版本的 MinHook；静态链接，不额外安装运行时 DLL。
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    add_subdirectory("${PARTY_DEPS_DIR}/minhook" minhook)
    if(NOT EXISTS "${PARTY_DEPS_DIR}/imgui/imgui.cpp")
        message(FATAL_ERROR "ImGui source missing. Run tools/Setup-Dependencies.ps1.")
    endif()
    # ImGui只静态链接本插件所需核心与两个后端；独立上下文不共享宝箱Mod的内部状态。
    add_library(party_imgui STATIC
        "${PARTY_DEPS_DIR}/imgui/imgui.cpp" "${PARTY_DEPS_DIR}/imgui/imgui_draw.cpp"
        "${PARTY_DEPS_DIR}/imgui/imgui_tables.cpp" "${PARTY_DEPS_DIR}/imgui/imgui_widgets.cpp"
        "${PARTY_DEPS_DIR}/imgui/backends/imgui_impl_dx11.cpp"
        "${PARTY_DEPS_DIR}/imgui/backends/imgui_impl_win32.cpp")
    target_include_directories(party_imgui PUBLIC "${PARTY_DEPS_DIR}/imgui" "${PARTY_DEPS_DIR}/imgui/backends")
    target_compile_options(party_imgui PRIVATE /utf-8)
    # 手柄仅由游戏原有 IAT 链采样，禁止 Win32 后端加载 XInput 再读一套设备。
    target_compile_definitions(party_imgui PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN IMGUI_IMPL_WIN32_DISABLE_GAMEPAD)
    # 两种分发链接同一批功能对象，只有入口不同，防止修复仅进入某一种安装方式。
    add_library(party_runtime OBJECT
        native/runtime.cpp native/party_native.cpp native/anywhere_menu.cpp
        native/control_state.cpp native/roster_service.cpp native/roster_prepare.cpp native/fixed_member_guard.cpp
        native/panel.cpp native/panel_input.cpp native/panel_fonts.cpp native/party_page.cpp
        native/standalone_ui/input.cpp native/standalone_ui/ui.cpp
        native/standalone_ui/hotkeys.cpp native/standalone_ui/hotkeys_ui.cpp
        native/standalone_ui/ui_layout.cpp native/standalone_ui/ui_navigation.cpp
        native/standalone_ui/ui_shell.cpp
        native/game_language.cpp native/game_names.cpp)
    # 两种入口使用仓库自带的 UI 函数表和业务页面，不依赖其它工程。
    target_include_directories(party_runtime PRIVATE native native/standalone_ui)
    target_compile_definitions(party_runtime PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
        SKY2_PARTY_VERSION="${PROJECT_VERSION}")
    target_compile_options(party_runtime PRIVATE /utf-8 /W4 /EHsc)
    target_link_libraries(party_runtime PRIVATE minhook party_imgui)
    add_library(Sky2PartyEditor SHARED native/plugin_entry.cpp $<TARGET_OBJECTS:party_runtime>)
    add_library(Sky2PartyEditorStandalone SHARED
        native/xinput_proxy.cpp native/xinput_proxy.def $<TARGET_OBJECTS:party_runtime>)
    foreach(mod_target IN ITEMS Sky2PartyEditor Sky2PartyEditorStandalone)
        target_include_directories(${mod_target} PRIVATE native)
        target_compile_definitions(${mod_target} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_compile_options(${mod_target} PRIVATE /utf-8 /W4 /EHsc)
        target_link_options(${mod_target} PRIVATE /Brepro /DEBUG:NONE)
        target_link_libraries(${mod_target} PRIVATE bcrypt minhook party_imgui d3d11 dxgi d3dcompiler dwmapi user32)
    endforeach()
    set_target_properties(Sky2PartyEditor PROPERTIES OUTPUT_NAME Sky2PartyEditor PREFIX "" SUFFIX ".asi")
    set_target_properties(Sky2PartyEditorStandalone PROPERTIES OUTPUT_NAME xinput1_4 PREFIX "")
endif()

# 原生纯逻辑测试由各模块通过 add_test 注册；安装隔离测试无需连接游戏。
if(BUILD_TESTING)
    if(PARTY_BUILD_MOD)
        set(SKY2_SOLO_IMGUI_TARGET party_imgui)
        include(native/standalone_ui/tests.cmake)
        # 可选人工视觉检查：真实业务页配合合成快照离屏绘制 PNG，不自动接触游戏。
        add_executable(party_standalone_visual EXCLUDE_FROM_ALL tests/standalone_panel_visual.cpp
            native/standalone_ui/hotkeys.cpp native/standalone_ui/hotkeys_ui.cpp native/standalone_ui/input.cpp
            native/party_page.cpp native/panel_fonts.cpp native/standalone_ui/ui.cpp
            native/standalone_ui/ui_shell.cpp native/standalone_ui/ui_layout.cpp native/standalone_ui/ui_navigation.cpp)
        target_include_directories(party_standalone_visual PRIVATE native native/standalone_ui)
        target_compile_definitions(party_standalone_visual PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN SKY2_PARTY_VERSION="${PROJECT_VERSION}")
        target_compile_options(party_standalone_visual PRIVATE /utf-8 /W4 /EHsc)
        target_link_libraries(party_standalone_visual PRIVATE party_imgui d3d11 d3dcompiler dwmapi windowscodecs ole32)
    endif()
    add_executable(party_patch_tests tests/patch_tests.cpp)
    target_include_directories(party_patch_tests PRIVATE native)
    if(MSVC)
        target_compile_options(party_patch_tests PRIVATE /utf-8 /W4)
    endif()
    add_test(NAME party_patch_tests COMMAND party_patch_tests)
    add_executable(party_menu_route_tests tests/menu_route_tests.cpp)
    target_include_directories(party_menu_route_tests PRIVATE native)
    if(MSVC)
        target_compile_options(party_menu_route_tests PRIVATE /utf-8 /W4)
    endif()
    add_test(NAME party_menu_route_tests COMMAND party_menu_route_tests)
    # 可逆功能事务与角色名单策略均使用合成数据，不需要游戏安装或存档。
    foreach(logic_test IN ITEMS patch_toggle roster_policy roster_prepare fixed_member_guard localization_text language game_names)
        add_executable(party_${logic_test}_tests tests/${logic_test}_tests.cpp)
        # 名称测试只使用自建 FPAC/TBL；文字语言测试在 Windows 私有内存中复现
        # 设置布局，非 Windows 则仅验证纯枚举。两者均不依赖或修改游戏资源。
        if(logic_test STREQUAL "game_names")
            target_sources(party_${logic_test}_tests PRIVATE native/game_names.cpp)
        elseif(logic_test STREQUAL "language" AND WIN32)
            target_sources(party_${logic_test}_tests PRIVATE native/game_language.cpp)
            target_compile_definitions(party_${logic_test}_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        endif()
        target_include_directories(party_${logic_test}_tests PRIVATE native)
        if(MSVC)
            target_compile_options(party_${logic_test}_tests PRIVATE /utf-8 /W4)
        endif()
        add_test(NAME party_${logic_test}_tests COMMAND party_${logic_test}_tests)
    endforeach()
    if(WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        # 显式 UI 替身驱动生产业务页，验证切页/失焦/代次取消和双确认边界。
        add_executable(party_page_tests tests/party_page_tests.cpp native/party_page.cpp)
        target_include_directories(party_page_tests PRIVATE native native/standalone_ui)
        target_compile_definitions(party_page_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN SKY2_PARTY_VERSION="${PROJECT_VERSION}")
        target_compile_options(party_page_tests PRIVATE /utf-8 /W4 /EHsc)
        add_test(NAME party_page_tests COMMAND party_page_tests)
        add_executable(party_instruction_tests tests/instruction_tests.cpp)
        target_include_directories(party_instruction_tests PRIVATE native)
        target_compile_options(party_instruction_tests PRIVATE /utf-8 /W4)
        target_compile_definitions(party_instruction_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        add_test(NAME party_instruction_tests COMMAND party_instruction_tests)
        add_executable(party_field_entry_tests tests/field_entry_tests.cpp)
        target_include_directories(party_field_entry_tests PRIVATE native)
        target_compile_options(party_field_entry_tests PRIVATE /utf-8 /W4)
        target_compile_definitions(party_field_entry_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        add_test(NAME party_field_entry_tests COMMAND party_field_entry_tests)
        # 验证暂不可选条件的原始/修改后真实CPU语义，不执行游戏原生函数。
        add_executable(party_unavailable_instruction_tests tests/unavailable_instruction_tests.cpp)
        target_include_directories(party_unavailable_instruction_tests PRIVATE native)
        target_compile_options(party_unavailable_instruction_tests PRIVATE /utf-8 /W4)
        target_compile_definitions(party_unavailable_instruction_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        add_test(NAME party_unavailable_instruction_tests COMMAND party_unavailable_instruction_tests)
        add_executable(party_panel_input_tests tests/panel_input_tests.cpp)
        target_include_directories(party_panel_input_tests PRIVATE native)
        target_compile_options(party_panel_input_tests PRIVATE /utf-8 /W4)
        target_compile_definitions(party_panel_input_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        add_test(NAME party_panel_input_tests COMMAND party_panel_input_tests)
        # 真实输入接入层使用合成系统边界，覆盖 VK7 锁死、失焦保留、心跳失效、
        # 双槽镜像和合作 IAT 内外层；不加载游戏，也不挂真实输入或窗口过程。
        add_executable(party_panel_input_runtime_tests tests/panel_input_runtime_tests.cpp
            native/standalone_ui/input.cpp native/standalone_ui/hotkeys.cpp)
        target_include_directories(party_panel_input_runtime_tests PRIVATE native)
        target_compile_options(party_panel_input_runtime_tests PRIVATE /utf-8 /W4 /EHsc)
        target_compile_definitions(party_panel_input_runtime_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        add_test(NAME party_panel_input_runtime_tests COMMAND party_panel_input_runtime_tests)
        # 合成进程内对象验证队列与场景失效；准备函数由测试替身实现，绝不执行游戏代码。
        add_executable(party_roster_service_tests tests/roster_service_tests.cpp native/roster_service.cpp)
        target_include_directories(party_roster_service_tests PRIVATE native)
        target_compile_options(party_roster_service_tests PRIVATE /utf-8 /W4 /EHsc)
        target_compile_definitions(party_roster_service_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        add_test(NAME party_roster_service_tests COMMAND party_roster_service_tests)
        # 直接运行生产补足协调逻辑；只在测试私有页执行保留签名的原生替身。
        # 覆盖继承 S 选择、六参数 ABI、培养保留及部分失败，弥补纯策略 mock 的盲区。
        add_executable(party_roster_preparation_service_tests
            tests/roster_preparation_service_tests.cpp native/roster_prepare.cpp)
        target_include_directories(party_roster_preparation_service_tests PRIVATE native)
        target_compile_options(party_roster_preparation_service_tests PRIVATE /utf-8 /W4 /EHsc)
        target_compile_definitions(party_roster_preparation_service_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        add_test(NAME party_roster_preparation_service_tests COMMAND party_roster_preparation_service_tests)
        # 协调器使用假挂钩与服务替身，检查安全帧顺序、失败重试及应用/持久化边界。
        add_executable(party_control_state_tests tests/control_state_tests.cpp native/control_state.cpp)
        target_include_directories(party_control_state_tests PRIVATE native "${PARTY_DEPS_DIR}/minhook/include")
        target_compile_options(party_control_state_tests PRIVATE /utf-8 /W4 /EHsc)
        target_compile_definitions(party_control_state_tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        add_test(NAME party_control_state_tests COMMAND party_control_state_tests)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/asi/CMakeLists.txt")
            add_subdirectory(tests/asi)
        endif()
        # 真实 DXGI 双 DLL 回归覆盖独立 MinHook 实例的并发丢链与正确就绪协议。
        if(EXISTS "${PARTY_DEPS_DIR}/minhook/CMakeLists.txt")
            set(PRESENT_TEST_MINHOOK "${PARTY_DEPS_DIR}/minhook" CACHE PATH "Present regression dependency")
            add_subdirectory(tests/present_chain)
        endif()
    endif()
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND AND WIN32)
        add_test(NAME party_packaging_tests
            COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s tests/packaging -v)
        set_tests_properties(party_packaging_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
endif()
