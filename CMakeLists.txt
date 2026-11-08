cmake_minimum_required(VERSION 3.24)
project(TF2VRSeatedControls VERSION 0.2.3 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
include(CTest)

add_executable(control_tests tests/Controls.cpp)
target_include_directories(control_tests PRIVATE src/layer)
add_test(NAME controls COMMAND control_tests)
if(NOT WIN32)
    message(STATUS "Building portable control tests. Use Windows or the MinGW toolchain to build the add-on.")
    return()
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The add-on requires a 64-bit build.")
endif()

set(OPENXR_INCLUDE_DIR "" CACHE PATH "Folder containing openxr.h and openxr_loader_negotiation.h")
if(NOT OPENXR_INCLUDE_DIR)
    include(FetchContent)
    FetchContent_Declare(openxr_sdk
        GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
        GIT_TAG release-1.1.63 GIT_SHALLOW TRUE SOURCE_SUBDIR headers_only)
    FetchContent_MakeAvailable(openxr_sdk)
    set(OPENXR_INCLUDE_DIR "${openxr_sdk_SOURCE_DIR}/include/openxr")
endif()
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/minhook EXCLUDE_FROM_ALL)
if(MSVC)
    enable_language(ASM_MASM)
    set(THUNKS src/layer/Thunks.asm)
else()
    enable_language(ASM)
    set(THUNKS src/layer/Thunks.S)
endif()

add_library(seated_layer SHARED src/layer/Layer.cpp src/layer/Bridge.cpp src/layer/NativeComfort.cpp src/layer/Support.cpp ${THUNKS})
target_include_directories(seated_layer PRIVATE "${OPENXR_INCLUDE_DIR}")
target_compile_definitions(seated_layer PRIVATE XR_NO_PROTOTYPES)
target_link_libraries(seated_layer PRIVATE minhook bcrypt)
set_target_properties(seated_layer PROPERTIES PREFIX "" OUTPUT_NAME TF2VRSeatedControlsLayer)
add_library(seated_plugin SHARED src/plugin/Plugin.cpp src/plugin/Registration.cpp)
target_link_libraries(seated_plugin PRIVATE advapi32)
set_target_properties(seated_plugin PROPERTIES PREFIX "" OUTPUT_NAME TF2VRSeatedControls)
add_executable(native_probe tests/NativeProbe.cpp src/layer/Support.cpp src/layer/Bridge.cpp src/layer/NativeComfort.cpp ${THUNKS})
target_include_directories(native_probe PRIVATE src/layer)
target_link_libraries(native_probe PRIVATE bcrypt minhook)
add_executable(layer_tests tests/Layer.cpp)
target_include_directories(layer_tests PRIVATE "${OPENXR_INCLUDE_DIR}")
target_compile_definitions(layer_tests PRIVATE XR_NO_PROTOTYPES)
set_target_properties(layer_tests PROPERTIES OUTPUT_NAME Titanfall2VRLauncher RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
add_test(NAME openxr_routing COMMAND layer_tests)
add_library(bootstrap_host SHARED tests/BootstrapHost.cpp)
set_target_properties(bootstrap_host PROPERTIES PREFIX "")
add_executable(bootstrap_tests tests/Bootstrap.cpp src/plugin/Registration.cpp)
target_link_libraries(bootstrap_tests PRIVATE advapi32)
add_test(NAME plugin_bootstrap COMMAND bootstrap_tests $<TARGET_FILE:bootstrap_host> $<TARGET_FILE:seated_plugin>)
add_executable(registration_tool src/plugin/Setup.cpp src/plugin/Registration.cpp)
target_link_libraries(registration_tool PRIVATE advapi32)
set_target_properties(registration_tool PROPERTIES OUTPUT_NAME TF2VRSeatedControlsSetup)

foreach(target seated_layer seated_plugin native_probe control_tests layer_tests bootstrap_host bootstrap_tests registration_tool)
    target_compile_definitions(${target} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
    if(MSVC)
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:/W4;/WX;/permissive-;/utf-8>")
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -Wno-cast-function-type)
        target_link_options(${target} PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endforeach()

set(PACKAGE_NAME "Local-TF2VRSeatedControls-${PROJECT_VERSION}")
set(PACKAGE_DIR "${CMAKE_BINARY_DIR}/package/${PACKAGE_NAME}")
set(MOD_NAME "Local.TF2VRSeatedControls")
add_custom_target(dist_package
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${PACKAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_DIR}/plugins/layer" "${PACKAGE_DIR}/licenses"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/package/mods" "${PACKAGE_DIR}/mods"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:seated_plugin>" "${PACKAGE_DIR}/plugins/"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:registration_tool>" "${PACKAGE_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:seated_layer>"
        "${CMAKE_CURRENT_SOURCE_DIR}/manifest/XR_APILAYER_TF2VR_seated_controls.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/config/TF2VRSeatedControls.ini" "${PACKAGE_DIR}/plugins/layer/"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/README.md" "${CMAKE_CURRENT_SOURCE_DIR}/BUILD.md" "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        "${CMAKE_CURRENT_SOURCE_DIR}/THIRD-PARTY-NOTICES.md" "${PACKAGE_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/third_party/BetterBindings-LICENSE.txt" "${PACKAGE_DIR}/licenses/"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/third_party/minhook/LICENSE.txt" "${PACKAGE_DIR}/licenses/MinHook-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/scripts/unregister-layer.ps1" "${PACKAGE_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_BINARY_DIR}/package"
        ${CMAKE_COMMAND} -E tar cf "${CMAKE_BINARY_DIR}/${PACKAGE_NAME}.zip" --format=zip "${PACKAGE_NAME}"
    DEPENDS seated_layer seated_plugin registration_tool VERBATIM)
