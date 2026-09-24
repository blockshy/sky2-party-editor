# 此诊断宿主既可由主工程 add_subdirectory，也可独立构建；不链接或复制游戏资源。
cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(Sky2PartyAsiDiagnostics LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "ASI integration diagnostics require Windows x64 and MSVC.")
endif()

add_executable(sky2_party_loader_host loader_host.cpp)
# 使用宝箱项目公开诊断的 ordinal 2/3 导入方式，让真实 UAL 处理宿主 IAT。
# 直接 GetProcAddress(ordinal) 不能替代游戏采用的 PE 导入流程。
set(PARTY_HOST_IMPORT "${CMAKE_CURRENT_BINARY_DIR}/party_host_xinput.lib")
add_custom_command(OUTPUT "${PARTY_HOST_IMPORT}"
    COMMAND "${CMAKE_AR}" /nologo /machine:x64
            "/def:${CMAKE_CURRENT_SOURCE_DIR}/xinput_host_imports.def"
            "/out:${PARTY_HOST_IMPORT}"
    DEPENDS xinput_host_imports.def VERBATIM)
add_custom_target(party_host_xinput_import DEPENDS "${PARTY_HOST_IMPORT}")
add_dependencies(sky2_party_loader_host party_host_xinput_import)
target_link_libraries(sky2_party_loader_host PRIVATE "${PARTY_HOST_IMPORT}")
target_compile_definitions(sky2_party_loader_host PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(sky2_party_loader_host PRIVATE /utf-8 /W4 /EHsc)

if(TARGET Sky2PartyEditorStandalone AND TARGET Sky2PartyEditor)
    # 这个宿主不链接 XInput 导入库，才能显式交换两个入口的加载先后次序。
    # 私有临时目录保留拒绝日志，既不启动游戏，也不访问游戏存档或真实手柄。
    add_executable(sky2_party_entry_host entry_host.cpp)
    target_compile_definitions(sky2_party_entry_host PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(sky2_party_entry_host PRIVATE /utf-8 /W4 /EHsc)
    foreach(entry_mode IN ITEMS standalone standalone-first asi-first)
        add_test(NAME party_entry_${entry_mode}
            COMMAND sky2_party_entry_host "$<TARGET_FILE:Sky2PartyEditorStandalone>"
                "$<TARGET_FILE:Sky2PartyEditor>" "${entry_mode}" "${CMAKE_CURRENT_BINARY_DIR}/entry-validation")
        set_tests_properties(party_entry_${entry_mode} PROPERTIES TIMEOUT 20)
    endforeach()
endif()
