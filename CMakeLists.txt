cmake_minimum_required(VERSION 3.20)
project(CovertReaction VERSION 0.1.0 LANGUAGES CXX)
if(APPLE)
  enable_language(OBJCXX)   # (before any subproject: src/app/MacFolderPanel.mm)
endif()

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

# Distribution builds: compile SDL2 from source and link it in (see below), e.g.
#   cmake -S . -B build-dist -DCMAKE_BUILD_TYPE=Release -DREACTION_VENDORED_SDL=ON \
#         "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
option(REACTION_VENDORED_SDL "Build SDL2 from source and link it statically" OFF)

if(EMSCRIPTEN)
  # Emscripten's own SDL2 port (downloaded on the first build). The engine uses
  # exceptions, everywhere, compile and link: Emscripten's JavaScript-based ones,
  # since native WebAssembly exceptions don't work with ASYNCIFY (the city loop's
  # fiber, see below).
  add_library(reaction_sdl2 INTERFACE)
  target_compile_options(reaction_sdl2 INTERFACE -sUSE_SDL=2)
  target_link_options(reaction_sdl2 INTERFACE -sUSE_SDL=2)
  add_compile_options(-fexceptions)
  add_link_options(-fexceptions)
  set(REACTION_SDL2 reaction_sdl2)
  # FOR LOCAL TESTING ONLY: packs resources/Covert (your copy of the original game,
  # which is copyrighted) into the build. A build made with this ON must never be
  # published or shared; the default build asks the player for their own files.
  option(REACTION_WEB_PRELOAD_DATA "Embed resources/Covert in the web build (local testing only, never publish)" OFF)
elseif(REACTION_VENDORED_SDL)
  # SDL2 built from source as part of this build and linked statically: nothing
  # to bundle, and it follows CMAKE_OSX_ARCHITECTURES / CMAKE_OSX_DEPLOYMENT_TARGET,
  # so a distribution build can be universal (arm64 + x86_64) for older macOS.
  include(FetchContent)
  FetchContent_Declare(sdl2
    URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz
    URL_HASH SHA256=24b574f71c87a763f50704bbb630cbe38298d544a1f890f099a4696b1d6beba4
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(sdl2)
  set(REACTION_SDL2 SDL2::SDL2-static)
else()
  find_package(SDL2 REQUIRED CONFIG)
  set(REACTION_SDL2 SDL2::SDL2)
endif()

if(MSVC)
  set(REACTION_WARNINGS /W4 /permissive-)
else()
  set(REACTION_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
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

# Decompressors for importing the game from its releases (formats/GameImport.h),
# also fetched and pinned, and built here (neither ships a CMake build):
#  - miniz (MIT): deflate, for zip, gzip and the xar of GOG's macOS .pkg.
#  - Igor Pavlov's LZMA SDK (public domain), its C code: 7z archives (the floppy
#    sets) and LZMA / LZMA2 (GOG's Windows installer, Inno Setup). The official
#    .7z from 7-zip.org, which CMake unpacks itself.
# (SOURCE_SUBDIR names a folder that doesn't exist: download only, no add_subdirectory.)
FetchContent_Declare(miniz
  URL https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
  URL_HASH SHA256=ada38db0b703a56d3dd6d57bf84a9c5d664921d870d8fea4db153979fb5332c5
  SOURCE_SUBDIR none
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(lzma_sdk
  URL https://www.7-zip.org/a/lzma2501.7z
  URL_HASH SHA256=cbc3babd589d971e45971d787ff100be8aaa5eab15b2694497ec3e447009e1f2
  SOURCE_SUBDIR none
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(miniz lzma_sdk)
add_library(reaction_miniz STATIC ${miniz_SOURCE_DIR}/miniz.c)
target_include_directories(reaction_miniz SYSTEM PUBLIC ${miniz_SOURCE_DIR})
target_compile_definitions(reaction_miniz PUBLIC
  MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ARCHIVE_APIS MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
set(REACTION_LZMA_FILES 7zArcIn 7zBuf 7zBuf2 7zCrc 7zCrcOpt 7zDec 7zStream Bcj2 Bra Bra86 BraIA64
  CpuArch Delta Lzma2Dec LzmaDec Ppmd7 Ppmd7Dec)
list(TRANSFORM REACTION_LZMA_FILES PREPEND ${lzma_sdk_SOURCE_DIR}/C/)
list(TRANSFORM REACTION_LZMA_FILES APPEND .c)
add_library(reaction_lzma STATIC ${REACTION_LZMA_FILES})
target_include_directories(reaction_lzma SYSTEM PUBLIC ${lzma_sdk_SOURCE_DIR}/C)
target_compile_definitions(reaction_lzma PRIVATE Z7_PPMD_SUPPORT Z7_EXTRACT_ONLY)

# stb_truetype (Sean Barrett, public domain / MIT), in third_party/stb: the system
# font for the setup screen's own UI (core/UiFont.h). Its own flags, no warnings.
add_library(reaction_stb STATIC third_party/stb/stb_truetype.c)
target_include_directories(reaction_stb SYSTEM PUBLIC third_party/stb)

# The engine: file formats, the platform-independent core and the sound drivers.
# No SDL here, so the command-line tools and tests can use it without a window.
file(GLOB_RECURSE REACTION_CORE_SOURCES CONFIGURE_DEPENDS
  src/core/*.cpp src/formats/*.cpp src/game/*.cpp src/audio/*.cpp)
add_library(reaction_core STATIC ${REACTION_CORE_SOURCES})
target_include_directories(reaction_core PUBLIC src)
target_compile_options(reaction_core PRIVATE ${REACTION_WARNINGS})
target_link_libraries(reaction_core PUBLIC mt32emu nuked_opl3 PRIVATE reaction_miniz reaction_lzma reaction_stb)
if(NOT EMSCRIPTEN)   # (formats/GameImport.h's background job)
  find_package(Threads REQUIRED)
  target_link_libraries(reaction_core PUBLIC Threads::Threads)
endif()

# The game application (SDL2 window, input, audio).
file(GLOB_RECURSE REACTION_APP_SOURCES CONFIGURE_DEPENDS src/app/*.cpp)
if(APPLE AND NOT EMSCRIPTEN)
  # The setup screen's file dialogs use macOS's own open panel (Objective-C++,
  # enabled at the top).
  list(APPEND REACTION_APP_SOURCES src/app/MacFolderPanel.mm)
  set_source_files_properties(src/app/MacFolderPanel.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()
add_executable(covert-reaction ${REACTION_APP_SOURCES})
if(APPLE AND NOT EMSCRIPTEN)
  target_link_libraries(covert-reaction PRIVATE "-framework AppKit" "-framework UniformTypeIdentifiers")
endif()
target_link_libraries(covert-reaction PRIVATE reaction_core ${REACTION_SDL2})
# The city loop runs GAME's program on a thread of its own (app/GameThread.h).
if(NOT EMSCRIPTEN)
  find_package(Threads REQUIRED)
  target_link_libraries(covert-reaction PRIVATE Threads::Threads)
endif()
if(TARGET SDL2::SDL2main)
  # SDL2main uses SDL2's functions, so SDL2 must come after it on the link line
  # (static linkers, e.g. mingw-w64's, resolve left to right).
  target_link_libraries(covert-reaction PRIVATE SDL2::SDL2main ${REACTION_SDL2})
endif()
target_compile_options(covert-reaction PRIVATE ${REACTION_WARNINGS})

if(APPLE)
  set_target_properties(covert-reaction PROPERTIES
    MACOSX_BUNDLE ON
    OUTPUT_NAME "Covert Reaction"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME "Covert Reaction"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.bert.covertreaction"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
  # `cmake --build build --target dmg`: "Covert Reaction.dmg" with the app and its
  # SDL libraries inside, ad-hoc signed, next to an /Applications shortcut
  # (cmake/MakeDmg.cmake). sdl2-compat loads SDL3 at run time, so it goes in too.
  set(reaction_sdl2_lib "")
  set(reaction_sdl3_lib "")
  if(NOT REACTION_VENDORED_SDL)   # (a vendored SDL is linked in: nothing to bundle)
    get_target_property(reaction_sdl2_lib SDL2::SDL2 LOCATION)
    find_library(REACTION_SDL3_LIBRARY NAMES SDL3.0 SDL3)
    file(REAL_PATH "${reaction_sdl2_lib}" reaction_sdl2_real)   # (Homebrew: lib/ symlinks into Cellar/sdl2-compat)
    if(reaction_sdl2_real MATCHES "sdl2-compat" AND REACTION_SDL3_LIBRARY)
      set(reaction_sdl3_lib ${REACTION_SDL3_LIBRARY})
    endif()
  endif()
  add_custom_target(dmg
    COMMAND ${CMAKE_COMMAND}
      "-DAPP=$<TARGET_BUNDLE_DIR:covert-reaction>"
      "-DSDL2_LIB=${reaction_sdl2_lib}"
      "-DSDL3_LIB=${reaction_sdl3_lib}"
      "-DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
      "-DVOLUME=Covert Reaction"
      -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/MakeDmg.cmake
    DEPENDS covert-reaction
    VERBATIM)
elseif(WIN32)
  set_target_properties(covert-reaction PROPERTIES WIN32_EXECUTABLE ON)
  # The setup screen's file dialogs (app/FileDialog.cpp: IFileOpenDialog, COM).
  target_link_libraries(covert-reaction PRIVATE ole32 uuid shell32)
elseif(EMSCRIPTEN)
  # build-web/index.html (+ index.js, index.wasm): the page in web/shell.html asks
  # for the player's game files, keeps them (and the saves) in IndexedDB, then calls
  # main. The shell is configured so it knows whether the data was preloaded.
  if(REACTION_WEB_PRELOAD_DATA)
    set(REACTION_WEB_PRELOADED true)
  else()
    set(REACTION_WEB_PRELOADED false)
  endif()
  set(REACTION_SHELL ${CMAKE_CURRENT_BINARY_DIR}/shell.html)
  configure_file(web/shell.html ${REACTION_SHELL} @ONLY)
  set_target_properties(covert-reaction PROPERTIES
    OUTPUT_NAME index
    SUFFIX ".html"
    LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html)
  target_link_options(covert-reaction PRIVATE
    --shell-file ${REACTION_SHELL}
    -lidbfs.js
    -sFORCE_FILESYSTEM=1
    # The city loop's GameThread runs GAME's code on a fiber (no threads on the web).
    -sASYNCIFY=1
    -sALLOW_MEMORY_GROWTH=1
    -sINVOKE_RUN=0
    -sEXIT_RUNTIME=0
    "-sEXPORTED_RUNTIME_METHODS=FS,IDBFS,callMain,addRunDependency,removeRunDependency,UTF8ToString")
  if(REACTION_WEB_PRELOAD_DATA)
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/resources/Covert)
      message(FATAL_ERROR "REACTION_WEB_PRELOAD_DATA needs the game files in resources/Covert")
    endif()
    message(WARNING "REACTION_WEB_PRELOAD_DATA: this web build embeds the original game's "
                    "files. Local testing only: never publish it.")
    target_link_options(covert-reaction PRIVATE
      --preload-file ${CMAKE_CURRENT_SOURCE_DIR}/resources/Covert@/Covert)
  endif()
endif()

# Developer tool: decode and dump game resources (pictures to BMP, etc.). Desktop only.
if(NOT EMSCRIPTEN)
  file(GLOB REACTION_TOOL_SOURCES CONFIGURE_DEPENDS tools/*.cpp)
  add_executable(reaction-tool ${REACTION_TOOL_SOURCES})
  target_link_libraries(reaction-tool PRIVATE reaction_core)
  target_compile_options(reaction-tool PRIVATE ${REACTION_WARNINGS})
endif()
