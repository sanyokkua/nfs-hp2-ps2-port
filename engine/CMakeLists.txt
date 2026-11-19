# NFS Hot Pursuit 2 for Mac: the PS2 game (SLUS-20362) on Apple Silicon. GPL-3.0-or-later.
#
# Builds ZloiKILLER's nfshp2-port-win runtime (recomp/: upstream 0.14 with docs/upstream-patches/ applied, then this project's
# changes) with the game's code as C++ (work/recomp/gen, made by tools/gen.sh from your disc) and the macOS host (src/mac/).
#
#   tools/build_release.sh                 # gen + configure + build into build/release
#   cmake -S engine -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/release
#   ctest --test-dir build/release         # eefloat_soft_test; demo replays with -DHP2_DEMO_TESTS=ON (minutes)
cmake_minimum_required(VERSION 3.24)
set(CMAKE_OSX_DEPLOYMENT_TARGET 15.0 CACHE STRING "the oldest macOS the game runs on")
project(nfs_hp2 C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(RT ${CMAKE_CURRENT_SOURCE_DIR}/recomp/runtime)
set(GEN ${CMAKE_CURRENT_SOURCE_DIR}/work/recomp/gen)
set(MAC ${CMAKE_CURRENT_SOURCE_DIR}/src/mac)
file(GLOB GEN_SOURCES CONFIGURE_DEPENDS ${GEN}/*.cpp)
if(NOT GEN_SOURCES)
    message(FATAL_ERROR "${GEN} is empty: run tools/gen.sh first (it translates your disc's program to C++)")
endif()

# The runtime files: one list with the players' build (src/mac/sources.txt, mac/build_game.sh). The headless build has no window,
# renderer or sound of its own: it leaves out the macOS host, the settings menu (src/overlay, third_party/imgui) and native_vk, and
# takes nfshp2-web's null backends.
file(STRINGS src/mac/sources.txt HP2_SOURCES REGEX "^[^#]")
list(TRANSFORM HP2_SOURCES PREPEND ${CMAKE_CURRENT_SOURCE_DIR}/)
set(RT_COMMON ${HP2_SOURCES})
list(FILTER RT_COMMON EXCLUDE REGEX "/src/|/third_party/|native_vk")

# EE floats: the runtime models the EE's rounding itself; the host must not fuse a*b+c (arm64 clang does by default)
set(HP2_FLAGS -w -fno-strict-aliasing -ffp-contract=off -include ${MAC}/web/compat_posix.h)
set(HP2_INCLUDES ${MAC} ${MAC}/web ${RT} ${GEN})
set_source_files_properties(${RT}/native_movie.cpp PROPERTIES COMPILE_OPTIONS -fwrapv)
# the generated code: big, flat functions; -O1 keeps the compile in minutes (as the reference builds)
set_source_files_properties(${GEN_SOURCES} PROPERTIES COMPILE_OPTIONS -O1)

add_library(hp2_gen OBJECT ${GEN_SOURCES})
target_compile_options(hp2_gen PRIVATE ${HP2_FLAGS})
target_include_directories(hp2_gen PRIVATE ${HP2_INCLUDES})

# ---- hp2_headless: no picture, sound pulled in real time and thrown away (tools/demo_check.sh) ----
add_executable(hp2_headless ${RT_COMMON} ${MAC}/web/setup_host.cpp ${MAC}/web/native_null.cpp ${MAC}/web/audio_null.cpp
               ${MAC}/web/host_native.cpp ${MAC}/headless_stubs.cpp $<TARGET_OBJECTS:hp2_gen>)
target_compile_options(hp2_headless PRIVATE ${HP2_FLAGS})
target_include_directories(hp2_headless PRIVATE ${HP2_INCLUDES})

# ---- tests ----
enable_testing()
add_executable(eefloat_soft_test ${MAC}/eefloat_soft_test.cpp)
target_compile_options(eefloat_soft_test PRIVATE -ffp-contract=off -frounding-math)
target_include_directories(eefloat_soft_test PRIVATE ${MAC}/web)
add_test(NAME eefloat_soft COMMAND eefloat_soft_test)
option(HP2_DEMO_TESTS "replay the game's 5 attract demos (needs game/extracted; minutes)" OFF)
if(HP2_DEMO_TESTS)
    add_test(NAME demo_replays COMMAND ${REPO}/tools/demo_check.sh $<TARGET_FILE:hp2_headless>)
    set_tests_properties(demo_replays PROPERTIES TIMEOUT 900)
endif()

# ---- hp2: the game (SDL3 window and sound, Vulkan on MoltenVK) ----
# SDL3 and the headers as the players get them: tools/build_deps.sh (macOS 15+)
set(HP2_DEPS ${REPO}/build/deps CACHE PATH "tools/build_deps.sh's output")
if(NOT EXISTS ${HP2_DEPS}/lib/libSDL3.0.dylib)
    message(FATAL_ERROR "run tools/build_deps.sh first (${HP2_DEPS})")
endif()
find_program(GLSLANG glslangValidator REQUIRED)
set(SHADER_DIR ${RT}/shaders)
set(SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(SHADER_HEADERS)
# name: the header's name (<name>.spv.h); source; defines
function(hp2_shader name source)
    set(out ${SHADER_OUT}/${name}.spv.h)
    add_custom_command(OUTPUT ${out}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_OUT}
        COMMAND ${GLSLANG} -V --target-env vulkan1.1 ${ARGN} -x -o ${out}.words ${SHADER_DIR}/${source}
        COMMAND ${CMAKE_COMMAND} -DIN=${out}.words -DOUT=${out} -P ${MAC}/spv_wrap.cmake
        DEPENDS ${SHADER_DIR}/${source} ${SHADER_DIR}/smaa/common.glsl ${SHADER_DIR}/smaa/SMAA.hlsl
        COMMENT "glslang ${name}" VERBATIM)
    set(SHADER_HEADERS ${SHADER_HEADERS} ${out} PARENT_SCOPE)
endfunction()
foreach(s native.vert native.frag native_fold.vert native_fold.frag native_post.vert native_post.frag native_restore.frag native_grade.frag
          native_opaque.frag native_extra.frag native_shadow.frag native_ao.frag native_ao_apply.frag native_smaa_edge.vert
          native_smaa_edge.frag native_smaa_weight.vert native_smaa_weight.frag native_smaa_blend.vert native_smaa_blend.frag)
    hp2_shader(${s} ${s})
endforeach()
hp2_shader(clip_native.vert native.vert -DCLIP_FAR)
hp2_shader(clip_native.frag native.frag -DCLIP_FAR)
hp2_shader(soft_native.frag native.frag -DSOFT)
hp2_shader(ms_native_shadow.frag native_shadow.frag -DMSAA)
add_custom_target(hp2_shaders DEPENDS ${SHADER_HEADERS})

# The settings menu (src/overlay): Dear ImGui 1.92.9 (third_party/imgui, MIT) on Vulkan, with the Roboto Medium font
# (third_party/fonts, Apache-2.0) compiled in as overlay_font.inc, its bytes as text (mac/build_game.sh makes it with xxd -i).
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/Roboto-Medium.ttf OVERLAY_FONT_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," OVERLAY_FONT_BYTES "${OVERLAY_FONT_HEX}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/generated/overlay_font.inc "${OVERLAY_FONT_BYTES}\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fonts/Roboto-Medium.ttf)
set(OVERLAY_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/src/overlay ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui/backends ${CMAKE_CURRENT_BINARY_DIR}/generated)

add_executable(hp2 ${HP2_SOURCES} $<TARGET_OBJECTS:hp2_gen>)
add_dependencies(hp2 hp2_shaders)
target_compile_options(hp2 PRIVATE ${HP2_FLAGS})
target_compile_definitions(hp2 PRIVATE HP2_HOST_MAIN IMGUI_IMPL_VULKAN_NO_PROTOTYPES)
target_include_directories(hp2 PRIVATE ${HP2_INCLUDES} ${OVERLAY_INCLUDES} ${SHADER_OUT} ${HP2_DEPS}/include)
target_link_libraries(hp2 PRIVATE ${HP2_DEPS}/lib/libSDL3.0.dylib)
set_target_properties(hp2 PROPERTIES BUILD_RPATH "${HP2_DEPS}/lib" INSTALL_RPATH "@executable_path/../Frameworks")
target_link_options(hp2 PRIVATE -Wl,-rpath,@executable_path/../Frameworks)
