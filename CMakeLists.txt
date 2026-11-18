# SpeedBreaker HP2: NFS Hot Pursuit 2 (PS2, SLUS-20362) on macOS / Apple Silicon. GPL-3.0-or-later for this repository's own files.
#
# Builds ZloiKILLER's nfshp2-port-win runtime (upstream/, branch `mac`: pinned upstream + our changes) with the game's code as C++
# (upstream/work/recomp/gen, made by tools/gen.sh from your disc) and this repository's macOS host (mac/).
#
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build                    # hp2_headless (no picture; the determinism gate) and hp2 (the game)
#   ctest --test-dir build                 # eefloat_soft_test; demo replays with -DHP2_DEMO_TESTS=ON (minutes)
cmake_minimum_required(VERSION 3.24)
project(speedbreaker_hp2 C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(UPSTREAM ${CMAKE_CURRENT_SOURCE_DIR}/upstream CACHE PATH "nfshp2-port-win checkout (branch mac)")
set(RT ${UPSTREAM}/recomp/runtime)
set(GEN ${UPSTREAM}/work/recomp/gen)
set(MAC ${CMAKE_CURRENT_SOURCE_DIR}/mac)
file(GLOB GEN_SOURCES CONFIGURE_DEPENDS ${GEN}/*.cpp)
if(NOT GEN_SOURCES)
    message(FATAL_ERROR "${GEN} is empty: run tools/gen.sh first (it translates your disc's program to C++)")
endif()

# The runtime files every build has (as nfshp2-web's Makefile, plus 0.14's phase and cores)
set(RT_COMMON ps2 mem_block pcmenu pcsettings pclang pcplatform native native_tex hle_iop dma vif profile hot_math native_mem memwatch
              native_movie gs_regs pcconfig iop_cpu iop_kernel iop_libsd spu2 host disc window_host pad_host phase cores)
list(TRANSFORM RT_COMMON PREPEND ${RT}/)
list(TRANSFORM RT_COMMON APPEND .cpp)

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
add_executable(eefloat_soft_test tests/eefloat_soft_test.cpp)
target_compile_options(eefloat_soft_test PRIVATE -ffp-contract=off -frounding-math)
target_include_directories(eefloat_soft_test PRIVATE ${MAC}/web)
add_test(NAME eefloat_soft COMMAND eefloat_soft_test)
option(HP2_DEMO_TESTS "replay the game's 5 attract demos (needs game-hp2/files; minutes)" OFF)
if(HP2_DEMO_TESTS)
    add_test(NAME demo_replays COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/demo_check.sh $<TARGET_FILE:hp2_headless>)
    set_tests_properties(demo_replays PROPERTIES TIMEOUT 900)
endif()

# ---- hp2: the game (SDL3 window and sound, Vulkan on MoltenVK) ----
find_package(SDL3 REQUIRED CONFIG)
find_path(VULKAN_INCLUDE vulkan/vulkan.h HINTS /opt/homebrew/include REQUIRED)
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
        COMMAND ${CMAKE_COMMAND} -DIN=${out}.words -DOUT=${out} -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_wrap.cmake
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

add_executable(hp2 ${RT_COMMON} ${RT}/native_vk.cpp ${MAC}/host_sdl.cpp ${MAC}/audio_sdl.cpp ${MAC}/web/setup_host.cpp
               $<TARGET_OBJECTS:hp2_gen>)
add_dependencies(hp2 hp2_shaders)
target_compile_options(hp2 PRIVATE ${HP2_FLAGS})
target_compile_definitions(hp2 PRIVATE HP2_HOST_MAIN)
target_include_directories(hp2 PRIVATE ${HP2_INCLUDES} ${SHADER_OUT} ${VULKAN_INCLUDE})
target_link_libraries(hp2 PRIVATE SDL3::SDL3)
