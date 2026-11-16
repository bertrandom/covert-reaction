cmake_minimum_required(VERSION 3.20)
project(CovertRedux VERSION 0.1.0 LANGUAGES CXX)

# Covert Action, reimplemented on SDL2. Portable C++20: builds on macOS, Linux and
# Windows. Game data is NOT part of the build; it is read at run time from an
# installed copy of the original game (see README.md).

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(SDL2 REQUIRED CONFIG)

if(MSVC)
  set(REDUX_WARNINGS /W4 /permissive-)
else()
  set(REDUX_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

# The engine: file formats and the platform-independent core. No SDL here, so
# the command-line tools and tests can use it without a window.
file(GLOB_RECURSE REDUX_CORE_SOURCES CONFIGURE_DEPENDS
  src/core/*.cpp src/formats/*.cpp src/game/*.cpp)
add_library(redux_core STATIC ${REDUX_CORE_SOURCES})
target_include_directories(redux_core PUBLIC src)
target_compile_options(redux_core PRIVATE ${REDUX_WARNINGS})

# The game application (SDL2 window, input, audio).
file(GLOB_RECURSE REDUX_APP_SOURCES CONFIGURE_DEPENDS src/app/*.cpp)
add_executable(covert-redux ${REDUX_APP_SOURCES})
target_link_libraries(covert-redux PRIVATE redux_core SDL2::SDL2)
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
endif()

# Developer tool: decode and dump game resources (pictures to BMP, etc.).
file(GLOB REDUX_TOOL_SOURCES CONFIGURE_DEPENDS tools/*.cpp)
add_executable(redux-tool ${REDUX_TOOL_SOURCES})
target_link_libraries(redux-tool PRIVATE redux_core)
target_compile_options(redux-tool PRIVATE ${REDUX_WARNINGS})
