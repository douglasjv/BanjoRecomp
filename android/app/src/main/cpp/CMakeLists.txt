cmake_minimum_required(VERSION 3.20)

project(BanjoRecompiledAndroid LANGUAGES C CXX)

if (NOT DEFINED BANJO_ANDROID_SDL_SOURCE_DIR AND DEFINED ENV{BANJO_ANDROID_SDL_SOURCE_DIR})
    set(BANJO_ANDROID_SDL_SOURCE_DIR "$ENV{BANJO_ANDROID_SDL_SOURCE_DIR}" CACHE PATH "Path to an SDL2 source tree for Android builds." FORCE)
endif()

get_filename_component(BANJO_RECOMP_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)

add_subdirectory("${BANJO_RECOMP_ROOT_DIR}" "${CMAKE_BINARY_DIR}/banjorecomp")
