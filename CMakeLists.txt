cmake_minimum_required(VERSION 3.20)

if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum OS X deployment version")
endif()

project(BanjoRecompiled LANGUAGES C CXX)
set(BANJO_RECOMP_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(BANJO_RECOMP_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(BANJO_FILE_TO_C_SCRIPT ${BANJO_RECOMP_SOURCE_DIR}/cmake/file_to_c.cmake)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-everything /W4")
endif()

# Opt out of constexpr mutex constructor on windows to prevent vcredist issues
if (WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR")
endif()

if (APPLE)
    enable_language(OBJC OBJCXX)
endif()

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    option(RECOMP_FLATPAK "Configure the build for Flatpak compatibility." OFF)
endif()

# Avoid warning about DOWNLOAD_EXTRACT_TIMESTAMP in CMake 3.24:
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
    cmake_policy(SET CMP0135 NEW)
endif()

set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${BANJO_RECOMP_BINARY_DIR})
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${BANJO_RECOMP_BINARY_DIR})

if (WIN32)
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${BANJO_RECOMP_SOURCE_DIR}/lib/")
endif()

set(RT64_STATIC TRUE)
set(RT64_SDL_WINDOW_VULKAN TRUE)
add_compile_definitions(HLSL_CPU)

set(BANJO_ANDROID_SDL_SOURCE_DIR "" CACHE PATH "Path to an SDL2 source tree with android-project/ for Android builds.")

if (ANDROID)
    if (NOT BANJO_ANDROID_SDL_SOURCE_DIR)
        message(FATAL_ERROR "ANDROID builds require BANJO_ANDROID_SDL_SOURCE_DIR to point to an SDL2 source tree that includes android-project/ and CMake support.")
    endif()

    if (NOT EXISTS "${BANJO_ANDROID_SDL_SOURCE_DIR}/android-project/app/src/main/java/org/libsdl/app/SDLActivity.java")
        message(FATAL_ERROR "BANJO_ANDROID_SDL_SOURCE_DIR must contain SDL's android-project template (missing android-project/app/src/main/java/org/libsdl/app/SDLActivity.java).")
    endif()

    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)

    add_subdirectory(${BANJO_ANDROID_SDL_SOURCE_DIR} ${BANJO_RECOMP_BINARY_DIR}/SDL EXCLUDE_FROM_ALL)

    set(SDL2_INCLUDE_DIRS ${BANJO_ANDROID_SDL_SOURCE_DIR}/include)
    set(SDL2_LIBRARIES SDL2::SDL2 SDL2::SDL2main)
endif()

if (RECOMP_FLATPAK)
    add_compile_definitions(RECOMP_FLATPAK)
endif()

add_subdirectory(${BANJO_RECOMP_SOURCE_DIR}/lib/rt64 ${BANJO_RECOMP_BINARY_DIR}/rt64)

set(BUILD_SHARED_LIBS OFF)

add_subdirectory(${BANJO_RECOMP_SOURCE_DIR}/lib/N64ModernRuntime)

target_include_directories(rt64 PRIVATE ${BANJO_RECOMP_BINARY_DIR}/rt64/src)

# RecompiledFuncs - Library containing the primary recompiler output
add_library(RecompiledFuncs STATIC)

target_compile_options(RecompiledFuncs PRIVATE
    # -Wno-unused-but-set-variable
    -fno-strict-aliasing
    -Wno-unused-variable
    -Wno-implicit-function-declaration
)

target_include_directories(RecompiledFuncs PRIVATE
    ${BANJO_RECOMP_SOURCE_DIR}/include
    ${BANJO_RECOMP_SOURCE_DIR}/lib/N64ModernRuntime/ultramodern/include
    ${BANJO_RECOMP_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include
    ${BANJO_RECOMP_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include
)

file(GLOB FUNC_C_SOURCES ${BANJO_RECOMP_SOURCE_DIR}/RecompiledFuncs/*.c)
file(GLOB FUNC_CXX_SOURCES ${BANJO_RECOMP_SOURCE_DIR}/RecompiledFuncs/*.cpp)

target_sources(RecompiledFuncs PRIVATE ${FUNC_C_SOURCES} ${FUNC_CXX_SOURCES})

# PatchesLib - Library containing the recompiled output for any custom function patches
add_library(PatchesLib STATIC)

target_compile_options(PatchesLib PRIVATE
    # -Wno-unused-but-set-variable
    -fno-strict-aliasing
    -Wno-unused-variable
    -Wno-implicit-function-declaration
)

target_include_directories(PatchesLib PRIVATE
    ${BANJO_RECOMP_SOURCE_DIR}/include
    ${BANJO_RECOMP_SOURCE_DIR}/lib/N64ModernRuntime/ultramodern/include
    ${BANJO_RECOMP_SOURCE_DIR}/lib/N64ModernRuntime/librecomp/include
    ${BANJO_RECOMP_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include
)

target_sources(PatchesLib PRIVATE
    ${BANJO_RECOMP_SOURCE_DIR}/RecompiledPatches/patches.c
    ${BANJO_RECOMP_SOURCE_DIR}/RecompiledPatches/patches_bin.c
)

set_source_files_properties(${BANJO_RECOMP_SOURCE_DIR}/RecompiledPatches/patches.c PROPERTIES COMPILE_FLAGS -fno-strict-aliasing)

# Build patches elf
set(BANJO_PATCHES_TOOL_HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin)

if(ANDROID)
    set(BANJO_PATCHES_NDK_ROOT "")
    if(DEFINED CMAKE_ANDROID_NDK)
        set(BANJO_PATCHES_NDK_ROOT ${CMAKE_ANDROID_NDK})
    elseif(DEFINED ANDROID_NDK)
        set(BANJO_PATCHES_NDK_ROOT ${ANDROID_NDK})
    endif()

    if(BANJO_PATCHES_NDK_ROOT)
        file(GLOB BANJO_PATCHES_NDK_TOOL_HINTS
            LIST_DIRECTORIES true
            "${BANJO_PATCHES_NDK_ROOT}/toolchains/llvm/prebuilt/*/bin"
        )
        list(APPEND BANJO_PATCHES_TOOL_HINTS ${BANJO_PATCHES_NDK_TOOL_HINTS})
    endif()
endif()

find_program(BANJO_PATCHES_CLANG
    NAMES clang
    PATHS ${BANJO_PATCHES_TOOL_HINTS}
    NO_DEFAULT_PATH
)
find_program(BANJO_PATCHES_LLD
    NAMES ld.lld
    PATHS ${BANJO_PATCHES_TOOL_HINTS}
    NO_DEFAULT_PATH
)

if(NOT DEFINED PATCHES_C_COMPILER)
    if (BANJO_PATCHES_CLANG)
        set(PATCHES_C_COMPILER ${BANJO_PATCHES_CLANG})
    else()
        set(PATCHES_C_COMPILER clang)
    endif()
endif()

if(NOT DEFINED PATCHES_LD)
    if (BANJO_PATCHES_LLD)
        set(PATCHES_LD ${BANJO_PATCHES_LLD})
    else()
        set(PATCHES_LD ld.lld)
    endif()
endif()

find_program(BANJO_N64RECOMP_EXECUTABLE
    NAMES N64Recomp
    PATHS
        ${BANJO_RECOMP_SOURCE_DIR}
        ${BANJO_RECOMP_SOURCE_DIR}/build-n64recomp
    NO_DEFAULT_PATH
)

if(NOT BANJO_N64RECOMP_EXECUTABLE)
    set(BANJO_N64RECOMP_EXECUTABLE ${BANJO_RECOMP_SOURCE_DIR}/N64Recomp)
endif()

add_custom_target(PatchesBin
    COMMAND ${CMAKE_COMMAND} -E env CC=${PATCHES_C_COMPILER} LD=${PATCHES_LD} make
    WORKING_DIRECTORY ${BANJO_RECOMP_SOURCE_DIR}/patches
    BYPRODUCTS ${BANJO_RECOMP_SOURCE_DIR}/patches/patches.elf
)

# Generate patches_bin.c from patches.bin
add_custom_command(OUTPUT ${BANJO_RECOMP_SOURCE_DIR}/RecompiledPatches/patches_bin.c
    COMMAND ${CMAKE_COMMAND}
        -DINPUT_FILE=${BANJO_RECOMP_SOURCE_DIR}/patches/patches.bin
        -DARRAY_NAME=bk_patches_bin
        -DOUTPUT_C=${BANJO_RECOMP_SOURCE_DIR}/RecompiledPatches/patches_bin.c
        -DOUTPUT_H=${BANJO_RECOMP_SOURCE_DIR}/RecompiledPatches/patches_bin.h
        -P ${BANJO_FILE_TO_C_SCRIPT}
    DEPENDS ${BANJO_RECOMP_SOURCE_DIR}/patches/patches.bin ${BANJO_FILE_TO_C_SCRIPT}
)

# Recompile patches elf into patches.c and patches.bin
add_custom_command(OUTPUT
                       ${BANJO_RECOMP_SOURCE_DIR}/patches/patches.bin
                       ${BANJO_RECOMP_SOURCE_DIR}/RecompiledPatches/patches.c
                       ${BANJO_RECOMP_SOURCE_DIR}/RecompiledPatches/recomp_overlays.inl
                       ${BANJO_RECOMP_SOURCE_DIR}/RecompiledPatches/funcs.h
                   # TODO: Look into why modifying patches requires two builds to take
                    COMMAND ${BANJO_N64RECOMP_EXECUTABLE} patches.toml
                    WORKING_DIRECTORY ${BANJO_RECOMP_SOURCE_DIR}
                    DEPENDS ${BANJO_RECOMP_SOURCE_DIR}/patches/patches.elf
)

# Main executable / Android shared library
if (ANDROID)
    add_library(BanjoRecompiled SHARED)
else()
    add_executable(BanjoRecompiled)
endif()

set (SOURCES
    ${BANJO_RECOMP_SOURCE_DIR}/src/main/launcher_animation.cpp
    ${BANJO_RECOMP_SOURCE_DIR}/src/main/main.cpp
    ${BANJO_RECOMP_SOURCE_DIR}/src/main/register_overlays.cpp
    ${BANJO_RECOMP_SOURCE_DIR}/src/main/register_patches.cpp
    ${BANJO_RECOMP_SOURCE_DIR}/src/main/touch_controls.cpp
    ${BANJO_RECOMP_SOURCE_DIR}/src/main/theme.cpp

    ${BANJO_RECOMP_SOURCE_DIR}/src/game/config.cpp
    ${BANJO_RECOMP_SOURCE_DIR}/src/game/debug.cpp
    ${BANJO_RECOMP_SOURCE_DIR}/src/game/recomp_api.cpp
    ${BANJO_RECOMP_SOURCE_DIR}/src/game/recomp_extension_api.cpp
    ${BANJO_RECOMP_SOURCE_DIR}/src/game/recomp_data_api.cpp
    ${BANJO_RECOMP_SOURCE_DIR}/src/game/rom_decompression.cpp

    ${BANJO_RECOMP_SOURCE_DIR}/rsp/n_aspMain.cpp
)

target_include_directories(BanjoRecompiled PRIVATE
    ${BANJO_RECOMP_SOURCE_DIR}/include
    ${BANJO_RECOMP_SOURCE_DIR}/lib/N64ModernRuntime/N64Recomp/include
    ${BANJO_RECOMP_SOURCE_DIR}/lib/N64ModernRuntime/thirdparty/sse2neon
    ${BANJO_RECOMP_SOURCE_DIR}/lib/rt64/src/contrib
    ${BANJO_RECOMP_SOURCE_DIR}/lib/rt64/src/contrib/hlslpp/include
    ${BANJO_RECOMP_SOURCE_DIR}/lib/rt64/src/contrib/dxc/inc
    ${BANJO_RECOMP_SOURCE_DIR}/lib/rt64/src
    ${BANJO_RECOMP_SOURCE_DIR}/lib/rt64/src/rhi
    ${BANJO_RECOMP_SOURCE_DIR}/lib/rt64/src/render
    ${BANJO_RECOMP_SOURCE_DIR}/lib/RecompFrontend/recompinput/include
    ${BANJO_RECOMP_SOURCE_DIR}/lib/RecompFrontend/recompui/include
    ${BANJO_RECOMP_SOURCE_DIR}/lib/RecompFrontend/recompui/src
    ${BANJO_RECOMP_SOURCE_DIR}/lib/rt64/src/contrib/nativefiledialog-extended/src/include
    ${BANJO_RECOMP_SOURCE_DIR}/lib/SlotMap
    ${CMAKE_CURRENT_BINARY_DIR}
)

# Generate icon_bytes.c from the app icon PNG.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.c ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.h
    COMMAND ${CMAKE_COMMAND}
        -DINPUT_FILE=${BANJO_RECOMP_SOURCE_DIR}/icons/app.png
        -DARRAY_NAME=icon_bytes
        -DOUTPUT_C=${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.c
        -DOUTPUT_H=${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.h
        -P ${BANJO_FILE_TO_C_SCRIPT}
    DEPENDS ${BANJO_RECOMP_SOURCE_DIR}/icons/app.png ${BANJO_FILE_TO_C_SCRIPT}
)

target_sources(BanjoRecompiled PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/icon_bytes.c)

if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    target_compile_options(BanjoRecompiled PRIVATE
        -march=nehalem
        -fno-strict-aliasing
        -fms-extensions
    )
else()
    target_compile_options(BanjoRecompiled PRIVATE
        -fno-strict-aliasing
        -fms-extensions
    )
endif()

if (MSVC)
    # Disable identical code folding, since this breaks mod function patching as multiple functions can get merged into one.
    target_link_options(BanjoRecompiled PRIVATE /OPT:NOICF)
elseif (APPLE)
    # Use a wrapper around ld64 that respects segprot's `max_prot` value in order
    # to make our executable memory writable (required for mod function patching)
    target_link_options(BanjoRecompiled PRIVATE
        "-fuse-ld=${BANJO_RECOMP_SOURCE_DIR}/.github/macos/ld64"
    )
endif()

if (DEFINED ENV{SDL2_VERSION})
    set(SDL2_VERSION $ENV{SDL2_VERSION})
else()
    set(SDL2_VERSION "2.30.3")
endif()

if (WIN32)
    include(FetchContent)

    # Fetch SDL2 on windows
    FetchContent_Declare(
        sdl2
        URL https://github.com/libsdl-org/SDL/releases/download/release-${SDL2_VERSION}/SDL2-devel-${SDL2_VERSION}-VC.zip
    )
    FetchContent_MakeAvailable(sdl2)
    target_include_directories(BanjoRecompiled PRIVATE
        ${sdl2_SOURCE_DIR}/include
    )
    target_link_directories(BanjoRecompiled PRIVATE
        ${sdl2_SOURCE_DIR}/lib/x64
    )

    # Copy SDL2 and dxc DLLs to output folder as post build step
    add_custom_command(TARGET BanjoRecompiled POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${sdl2_SOURCE_DIR}/lib/x64/SDL2.dll"
            "${BANJO_RECOMP_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxil.dll"
            "${BANJO_RECOMP_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxcompiler.dll"
            $<TARGET_FILE_DIR:BanjoRecompiled>)

    set_target_properties(
        BanjoRecompiled
        PROPERTIES
            LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE"
            LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
            LINK_FLAGS_RELWITHDEBINFO  "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
            LINK_FLAGS_MINSIZEREL "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
    )

    target_sources(BanjoRecompiled PRIVATE ${BANJO_RECOMP_SOURCE_DIR}/icons/app.rc)
    target_link_libraries(BanjoRecompiled PRIVATE SDL2 Winmm.lib)
elseif (ANDROID)
    add_compile_definitions("PLUME_SDL_VULKAN_ENABLED")
    add_compile_definitions("RT64_SDL_WINDOW_VULKAN")

    target_include_directories(BanjoRecompiled PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(BanjoRecompiled PRIVATE ${SDL2_LIBRARIES} log)
elseif (APPLE)
    find_package(SDL2 REQUIRED)
    target_include_directories(BanjoRecompiled PRIVATE ${SDL2_INCLUDE_DIRS})

    set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
    set(THREADS_PREFER_PTHREAD_FLAG TRUE)
    find_package(Threads REQUIRED)

    target_link_libraries(BanjoRecompiled PRIVATE ${CMAKE_DL_LIBS} Threads::Threads SDL2::SDL2)

    include(${BANJO_RECOMP_SOURCE_DIR}/.github/macos/apple_bundle.cmake)
endif()

if (CMAKE_SYSTEM_NAME MATCHES "Linux")
    find_package(SDL2 REQUIRED)
    find_package(X11 REQUIRED)

    add_compile_definitions("PLUME_SDL_VULKAN_ENABLED")
    add_compile_definitions("RT64_SDL_WINDOW_VULKAN")

    message(STATUS "SDL2_FOUND = ${SDL2_FOUND}")
    message(STATUS "SDL2_INCLUDE_DIRS = ${SDL2_INCLUDE_DIRS}")

    target_include_directories(BanjoRecompiled PRIVATE ${SDL2_INCLUDE_DIRS})

    message(STATUS "X11_FOUND = ${X11_FOUND}")
    message(STATUS "X11_Xrandr_FOUND = ${X11_Xrandr_FOUND}")
    message(STATUS "X11_INCLUDE_DIR = ${X11_INCLUDE_DIR}")
    message(STATUS "X11_LIBRARIES = ${X11_LIBRARIES}")

    target_include_directories(BanjoRecompiled PRIVATE ${X11_INCLUDE_DIR} ${X11_Xrandr_INCLUDE_PATH})
    target_link_libraries(BanjoRecompiled PRIVATE ${X11_LIBRARIES} ${X11_Xrandr_LIB})

    find_package(Freetype REQUIRED)

    message(STATUS "FREETYPE_FOUND = ${FREETYPE_FOUND}")
    message(STATUS "FREETYPE_INCLUDE_DIRS = ${FREETYPE_INCLUDE_DIRS}")
    message(STATUS "FREETYPE_LIBRARIES = ${FREETYPE_LIBRARIES}")

    include_directories(${FREETYPE_LIBRARIES})
    target_link_libraries(BanjoRecompiled PRIVATE ${FREETYPE_LIBRARIES} SDL2::SDL2)

    set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
    set(THREADS_PREFER_PTHREAD_FLAG TRUE)
    find_package(Threads REQUIRED)

    target_link_libraries(BanjoRecompiled PRIVATE "-latomic -static-libstdc++" ${CMAKE_DL_LIBS} Threads::Threads)
endif()

# TODO fix the rt64 CMake script so that this doesn't need to be duplicated here
# For DXC
set (DXC_COMMON_OPTS "-I${BANJO_RECOMP_SOURCE_DIR}/src")
set (DXC_DXIL_OPTS "-Wno-ignored-attributes")
set (DXC_SPV_OPTS "-spirv" "-fspv-target-env=vulkan1.0" "-fvk-use-dx-layout")
set (DXC_PS_OPTS "${DXC_COMMON_OPTS}" "-E" "PSMain" "-T ps_6_0" "-D DYNAMIC_RENDER_PARAMS")
set (DXC_VS_OPTS "${DXC_COMMON_OPTS}" "-E" "VSMain" "-T vs_6_0" "-D DYNAMIC_RENDER_PARAMS" "-fvk-invert-y")
set (DXC_CS_OPTS "${DXC_COMMON_OPTS}" "-E" "CSMain" "-T cs_6_0")
set (DXC_GS_OPTS "${DXC_COMMON_OPTS}" "-E" "GSMain" "-T gs_6_0")
set (DXC_RT_OPTS "${DXC_COMMON_OPTS}" "-D" "RT_SHADER" "-T" "lib_6_3" "-fspv-target-env=vulkan1.1spirv1.4" "-fspv-extension=SPV_KHR_ray_tracing" "-fspv-extension=SPV_EXT_descriptor_indexing")

if (${WIN32})
    set (DXC "${BANJO_RECOMP_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxc.exe")
    add_compile_definitions(NOMINMAX)
elseif (ANDROID AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
    set (DXC "${BANJO_RECOMP_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxc.exe")
elseif (APPLE OR (ANDROID AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin"))
    # Apple's binary is universal, so it'll work on both x86_64 and arm64
    set (DXC "DYLD_LIBRARY_PATH=${BANJO_RECOMP_SOURCE_DIR}/lib/rt64/src/contrib/dxc/lib/arm64" "${BANJO_RECOMP_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/arm64/dxc-macos")
else()
    if(ANDROID)
        set(BANJO_HOST_PROCESSOR ${CMAKE_HOST_SYSTEM_PROCESSOR})
    else()
        set(BANJO_HOST_PROCESSOR ${CMAKE_SYSTEM_PROCESSOR})
    endif()

    if(BANJO_HOST_PROCESSOR MATCHES "x86_64|amd64|AMD64")
        set (DXC "LD_LIBRARY_PATH=${BANJO_RECOMP_SOURCE_DIR}/lib/rt64/src/contrib/dxc/lib/x64" "${BANJO_RECOMP_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/x64/dxc-linux")
    else()
        set (DXC "LD_LIBRARY_PATH=${BANJO_RECOMP_SOURCE_DIR}/lib/rt64/src/contrib/dxc/lib/arm64" "${BANJO_RECOMP_SOURCE_DIR}/lib/rt64/src/contrib/dxc/bin/arm64/dxc-linux")
    endif()
endif()

set(RECOMP_FRONTEND_N64MODERNRUNTIME_PATH ${BANJO_RECOMP_SOURCE_DIR}/lib/N64ModernRuntime)
set(RECOMP_FRONTEND_RT64_PATH ${BANJO_RECOMP_SOURCE_DIR}/lib/rt64)

add_subdirectory(${BANJO_RECOMP_SOURCE_DIR}/lib/RecompFrontend)

target_link_libraries(BanjoRecompiled PRIVATE
    PatchesLib
    RecompiledFuncs
    recompui
    recompinput
    librecomp
    ultramodern
    rt64
    nfd
)

# Embed all .nrm files in the "mods" directory
file(GLOB NRM_FILES "${BANJO_RECOMP_SOURCE_DIR}/mods/*.nrm")

set(GENERATED_NRM_SOURCES "")

foreach(NRM_FILE ${NRM_FILES})
    get_filename_component(NRM_NAME ${NRM_FILE} NAME_WE)
    set(OUT_C "${CMAKE_CURRENT_BINARY_DIR}/mods/${NRM_NAME}.c")
    set(OUT_H "${CMAKE_CURRENT_BINARY_DIR}/mods/${NRM_NAME}.h")

    add_custom_command(
        OUTPUT ${OUT_C} ${OUT_H}
        COMMAND ${CMAKE_COMMAND}
            -DINPUT_FILE=${NRM_FILE}
            -DARRAY_NAME=${NRM_NAME}
            -DOUTPUT_C=${OUT_C}
            -DOUTPUT_H=${OUT_H}
            -P ${BANJO_FILE_TO_C_SCRIPT}
        DEPENDS ${NRM_FILE} ${BANJO_FILE_TO_C_SCRIPT}
    )

    list(APPEND GENERATED_NRM_SOURCES ${OUT_C})
endforeach()

target_sources(BanjoRecompiled PRIVATE ${GENERATED_NRM_SOURCES})

target_sources(BanjoRecompiled PRIVATE ${SOURCES})

set_property(TARGET BanjoRecompiled PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${BANJO_RECOMP_SOURCE_DIR}")
