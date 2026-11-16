cmake_minimum_required(VERSION 3.20)
project(CovertRedux VERSION 0.1.0 LANGUAGES CXX)

# Covert Action, reimplemented on SDL2. Portable C++20: builds on macOS, Linux and
# Windows, and for the web with Emscripten (emcmake cmake ...). Game data is NOT
# part of the build; it is read at run time from an installed copy of the original
# game (see README.md).

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(EMSCRIPTEN)
  # Emscripten's own SDL2 port (downloaded on the first build). The engine uses
  # exceptions, everywhere, compile and link: Emscripten's JavaScript-based ones,
  # since native WebAssembly exceptions don't work with ASYNCIFY (the city loop's
  # fiber, see below).
  add_library(redux_sdl2 INTERFACE)
  target_compile_options(redux_sdl2 INTERFACE -sUSE_SDL=2)
  target_link_options(redux_sdl2 INTERFACE -sUSE_SDL=2)
  add_compile_options(-fexceptions)
  add_link_options(-fexceptions)
  set(REDUX_SDL2 redux_sdl2)
  # FOR LOCAL TESTING ONLY: packs resources/Covert (your copy of the original game,
  # which is copyrighted) into the build. A build made with this ON must never be
  # published or shared; the default build asks the player for their own files.
  option(REDUX_WEB_PRELOAD_DATA "Embed resources/Covert in the web build (local testing only, never publish)" OFF)
else()
  find_package(SDL2 REQUIRED CONFIG)
  set(REDUX_SDL2 SDL2::SDL2)
endif()

if(MSVC)
  set(REDUX_WARNINGS /W4 /permissive-)
else()
  set(REDUX_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

# Sound devices, emulated (third-party, fetched at configure time and pinned; built
# as libraries of their own, with their own flags):
#  - libmt32emu, Munt's MT-32 emulator (LGPL-2.1+). The player supplies the ROMs.
#  - Nuked-OPL3, the AdLib's OPL2/OPL3 (LGPL-2.1+).
# For an offline build, point FETCHCONTENT_SOURCE_DIR_MUNT / _NUKED_OPL3 at copies.
include(FetchContent)
FetchContent_Declare(munt
  URL https://github.com/munt/munt/archive/refs/tags/munt_2_8_4.tar.gz
  URL_HASH SHA256=75f29ed4b56c830e71005ce2d3ffc0b6b19a89ca8e3119054fa0d315b8bcc384
  SOURCE_SUBDIR mt32emu
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(nuked_opl3
  URL https://github.com/nukeykt/Nuked-OPL3/archive/765ec962e473aeb767e4cba74ffdc8f588ffbfe8.tar.gz
  URL_HASH SHA256=2fad908c3904d3ef51b55e0d6a8980c7142942e2d0baa7e99bf038cf0a41199d
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(libmt32emu_SHARED OFF CACHE BOOL "" FORCE)
set(libmt32emu_C_INTERFACE OFF CACHE BOOL "" FORCE)
set(libmt32emu_REQUIRE_ANSI OFF CACHE BOOL "" FORCE)
set(libmt32emu_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(libmt32emu_INSTALL_DEFAULT_LOCALE OFF CACHE BOOL "" FORCE)
set(libmt32emu_PKGCONFIG_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(munt nuked_opl3)
enable_language(C)
add_library(nuked_opl3 STATIC ${nuked_opl3_SOURCE_DIR}/opl3.c)
target_include_directories(nuked_opl3 PUBLIC ${nuked_opl3_SOURCE_DIR})

# The engine: file formats, the platform-independent core and the sound drivers.
# No SDL here, so the command-line tools and tests can use it without a window.
file(GLOB_RECURSE REDUX_CORE_SOURCES CONFIGURE_DEPENDS
  src/core/*.cpp src/formats/*.cpp src/game/*.cpp src/audio/*.cpp)
add_library(redux_core STATIC ${REDUX_CORE_SOURCES})
target_include_directories(redux_core PUBLIC src)
target_compile_options(redux_core PRIVATE ${REDUX_WARNINGS})
target_link_libraries(redux_core PUBLIC mt32emu nuked_opl3)

# The game application (SDL2 window, input, audio).
file(GLOB_RECURSE REDUX_APP_SOURCES CONFIGURE_DEPENDS src/app/*.cpp)
add_executable(covert-redux ${REDUX_APP_SOURCES})
target_link_libraries(covert-redux PRIVATE redux_core ${REDUX_SDL2})
# The city loop runs GAME's program on a thread of its own (app/GameThread.h).
if(NOT EMSCRIPTEN)
  find_package(Threads REQUIRED)
  target_link_libraries(covert-redux PRIVATE Threads::Threads)
endif()
if(TARGET SDL2::SDL2main)
  target_link_libraries(covert-redux PRIVATE SDL2::SDL2main)
endif()
target_compile_options(covert-redux PRIVATE ${REDUX_WARNINGS})

if(APPLE)
  set_target_properties(covert-redux PROPERTIES
    MACOSX_BUNDLE ON
    OUTPUT_NAME "Covert Redux"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME "Covert Redux"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.covert-redux.app"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
elseif(WIN32)
  set_target_properties(covert-redux PROPERTIES WIN32_EXECUTABLE ON)
elseif(EMSCRIPTEN)
  # build-web/index.html (+ index.js, index.wasm): the page in web/shell.html asks
  # for the player's game files, keeps them (and the saves) in IndexedDB, then calls
  # main. The shell is configured so it knows whether the data was preloaded.
  if(REDUX_WEB_PRELOAD_DATA)
    set(REDUX_WEB_PRELOADED true)
  else()
    set(REDUX_WEB_PRELOADED false)
  endif()
  set(REDUX_SHELL ${CMAKE_CURRENT_BINARY_DIR}/shell.html)
  configure_file(web/shell.html ${REDUX_SHELL} @ONLY)
  set_target_properties(covert-redux PROPERTIES
    OUTPUT_NAME index
    SUFFIX ".html"
    LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html)
  target_link_options(covert-redux PRIVATE
    --shell-file ${REDUX_SHELL}
    -lidbfs.js
    -sFORCE_FILESYSTEM=1
    # The city loop's GameThread runs GAME's code on a fiber (no threads on the web).
    -sASYNCIFY=1
    -sALLOW_MEMORY_GROWTH=1
    -sINVOKE_RUN=0
    -sEXIT_RUNTIME=0
    "-sEXPORTED_RUNTIME_METHODS=FS,IDBFS,callMain,addRunDependency,removeRunDependency,UTF8ToString")
  if(REDUX_WEB_PRELOAD_DATA)
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/resources/Covert)
      message(FATAL_ERROR "REDUX_WEB_PRELOAD_DATA needs the game files in resources/Covert")
    endif()
    message(WARNING "REDUX_WEB_PRELOAD_DATA: this web build embeds the original game's "
                    "files. Local testing only: never publish it.")
    target_link_options(covert-redux PRIVATE
      --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/resources/Covert@/Covert)
  endif()
endif()

# Developer tool: decode and dump game resources (pictures to BMP, etc.). Desktop only.
if(NOT EMSCRIPTEN)
  file(GLOB REDUX_TOOL_SOURCES CONFIGURE_DEPENDS tools/*.cpp)
  add_executable(redux-tool ${REDUX_TOOL_SOURCES})
  target_link_libraries(redux-tool PRIVATE redux_core)
  target_compile_options(redux-tool PRIVATE ${REDUX_WARNINGS})
endif()
