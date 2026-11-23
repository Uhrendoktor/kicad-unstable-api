cmake_minimum_required(VERSION 3.20)
project(kicad_unstable_api VERSION 0.1.0 LANGUAGES C CXX)
include(CTest)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    set(KUA_KIFACE_SUFFIX ".dll")
else()
    set(KUA_KIFACE_SUFFIX ".kiface")
endif()

set(KUA_MODULE_DIR "${CMAKE_BINARY_DIR}/modules")
set(KUA_RUNTIME_MODULE_DIR "${KUA_MODULE_DIR}/$<CONFIG>")
set(KUA_EXTENSION_DIR "${KUA_RUNTIME_MODULE_DIR}/kicad-unstable-api/extensions")

add_library(kua_test_extension SHARED tests/native/test_extension.cpp)
target_include_directories(kua_test_extension PRIVATE include)
set_target_properties(kua_test_extension PROPERTIES
    PREFIX "" OUTPUT_NAME "kua_test_extension"
    LIBRARY_OUTPUT_DIRECTORY "${KUA_EXTENSION_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${KUA_EXTENSION_DIR}")

# Probe module used by tools/smoke_real_kicad.py. It remains separate from
# the synthetic test extension so a real editor can exercise its actual GAL.
add_library(kua_real_smoke_extension MODULE tests/native/test_real_smoke_extension.cpp)
target_include_directories(kua_real_smoke_extension PRIVATE include)
set_target_properties(kua_real_smoke_extension PROPERTIES
    PREFIX "" OUTPUT_NAME "kua_real_smoke_extension")

foreach(editor eeschema pcbnew)
    add_library(kua_original_${editor} MODULE tests/native/original_kiface.cpp)
    target_include_directories(kua_original_${editor} PRIVATE tests/native)
    set_target_properties(kua_original_${editor} PROPERTIES
        PREFIX "" OUTPUT_NAME "_${editor}.original" SUFFIX "${KUA_KIFACE_SUFFIX}"
        LIBRARY_OUTPUT_DIRECTORY "${KUA_RUNTIME_MODULE_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${KUA_RUNTIME_MODULE_DIR}")

    add_library(kua_proxy_${editor} MODULE src/kiface_proxy.cpp)
    target_include_directories(kua_proxy_${editor} PRIVATE include tests/native)
    target_compile_definitions(kua_proxy_${editor} PRIVATE KUA_SYNTHETIC_ABI)
    if(editor STREQUAL "pcbnew")
        target_compile_definitions(kua_proxy_${editor} PRIVATE KUA_BUILD_EDITOR_PCB)
    endif()
    target_link_libraries(kua_proxy_${editor} PRIVATE ${CMAKE_DL_LIBS})
    set_target_properties(kua_proxy_${editor} PROPERTIES
        PREFIX "" OUTPUT_NAME "_${editor}" SUFFIX "${KUA_KIFACE_SUFFIX}"
        LIBRARY_OUTPUT_DIRECTORY "${KUA_RUNTIME_MODULE_DIR}"
        RUNTIME_OUTPUT_DIRECTORY "${KUA_RUNTIME_MODULE_DIR}")
    add_dependencies(kua_proxy_${editor} kua_original_${editor} kua_test_extension)
endforeach()

add_executable(kua_proxy_test_host tests/native/test_host.cpp)
target_include_directories(kua_proxy_test_host PRIVATE include tests/native)
if(NOT WIN32)
    target_link_libraries(kua_proxy_test_host PRIVATE ${CMAKE_DL_LIBS})
endif()

# Compile the extension-facing header as C as well as C++. The extension ABI
# deliberately contains no C++ types and must remain consumable by C plugins.
add_executable(kua_extension_api_c_test tests/native/test_extension_api_c.c)
target_include_directories(kua_extension_api_c_test PRIVATE include)

if(MSVC)
    foreach(target kua_original_eeschema kua_original_pcbnew kua_test_extension
                   kua_proxy_eeschema kua_proxy_pcbnew kua_proxy_test_host
                   kua_real_smoke_extension)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    endforeach()
    target_compile_options(kua_extension_api_c_test PRIVATE /W4)
else()
    foreach(target kua_original_eeschema kua_original_pcbnew kua_test_extension
                   kua_proxy_eeschema kua_proxy_pcbnew kua_proxy_test_host)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endforeach()
    target_compile_options(kua_extension_api_c_test PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME c_extension_api_header COMMAND kua_extension_api_c_test)

add_test(NAME eeschema_kiface_proxy_forwarding
    COMMAND kua_proxy_test_host
        "$<TARGET_FILE:kua_proxy_eeschema>"
        "$<TARGET_FILE:kua_test_extension>" "1")
set_tests_properties(eeschema_kiface_proxy_forwarding PROPERTIES TIMEOUT 30)

add_test(NAME pcbnew_kiface_proxy_forwarding
    COMMAND kua_proxy_test_host
        "$<TARGET_FILE:kua_proxy_pcbnew>"
        "$<TARGET_FILE:kua_test_extension>" "2")
set_tests_properties(pcbnew_kiface_proxy_forwarding PROPERTIES TIMEOUT 30)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME python_modloader_unit
    COMMAND ${Python3_EXECUTABLE} -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/tests" -p "test_modloader.py" -v)
add_test(NAME python_real_kicad_build_tool_unit
    COMMAND ${Python3_EXECUTABLE} -m unittest discover
        -s "${CMAKE_SOURCE_DIR}/tests" -p "test_real_kicad_build.py" -v)
add_test(NAME installed_proxy_roundtrip
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tests/test_installed_proxy.py"
        --proxy "$<TARGET_FILE:kua_proxy_eeschema>"
        --original "$<TARGET_FILE:kua_original_eeschema>"
        --pcb-proxy "$<TARGET_FILE:kua_proxy_pcbnew>"
        --pcb-original "$<TARGET_FILE:kua_original_pcbnew>"
        --extension "$<TARGET_FILE:kua_test_extension>"
        --test-host "$<TARGET_FILE:kua_proxy_test_host>"
        --suffix "${KUA_KIFACE_SUFFIX}")
set_tests_properties(installed_proxy_roundtrip PROPERTIES TIMEOUT 45)

# Production proxies are compiled with tools/build_real_kicad_proxy.py using the
# exact KiCad build's compile_commands.json, so generated headers, wxWidgets
# configuration, target definitions and include paths are reused.
