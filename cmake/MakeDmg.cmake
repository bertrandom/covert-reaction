# Builds "Covert Reaction.dmg": the app with its SDL libraries inside, ad-hoc
# signed, next to a shortcut to /Applications. Run by the `dmg` target
# (cmake --build build --target dmg); the variables come from CMakeLists.txt:
#   APP         the built .app bundle
#   SDL2_LIB    the SDL2 library the app links (a real file, e.g. Homebrew's)
#   SDL3_LIB    SDL3, if SDL2 is sdl2-compat (which loads it at run time), or empty
#   OUT_DIR     where the staging folder and the .dmg go
#   VOLUME      the disk image's name
cmake_minimum_required(VERSION 3.20)

get_filename_component(app_name "${APP}" NAME)
set(stage "${OUT_DIR}/dmg-root")
set(dmg "${OUT_DIR}/${VOLUME}.dmg")
set(app "${stage}/${app_name}")
get_filename_component(exe_name "${app_name}" NAME_WE)
set(exe "${app}/Contents/MacOS/${exe_name}")
set(fw "${app}/Contents/Frameworks")

function(run)
  execute_process(COMMAND ${ARGV} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${ARGV}\n${out}${err}")
  endif()
endfunction()

file(REMOVE_RECURSE "${stage}")
file(REMOVE "${dmg}")
file(MAKE_DIRECTORY "${stage}")
run(cp -R "${APP}" "${stage}/")
file(MAKE_DIRECTORY "${fw}")

# SDL2: copy it in under its install name and point the executable at the copy.
file(REAL_PATH "${SDL2_LIB}" sdl2_real)
execute_process(COMMAND otool -D "${sdl2_real}" OUTPUT_VARIABLE sdl2_id OUTPUT_STRIP_TRAILING_WHITESPACE)
string(REGEX REPLACE "^[^\n]*\n" "" sdl2_id "${sdl2_id}")   # (otool -D prints the file name first)
get_filename_component(sdl2_file "${sdl2_id}" NAME)
file(COPY_FILE "${sdl2_real}" "${fw}/${sdl2_file}")
file(CHMOD "${fw}/${sdl2_file}" PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
run(install_name_tool -id "@rpath/${sdl2_file}" "${fw}/${sdl2_file}")
run(install_name_tool -change "${sdl2_id}" "@executable_path/../Frameworks/${sdl2_file}" "${exe}")

# sdl2-compat dlopen()s SDL3, looking next to itself first (@loader_path/libSDL3.dylib).
if(SDL3_LIB)
  file(REAL_PATH "${SDL3_LIB}" sdl3_real)
  file(COPY_FILE "${sdl3_real}" "${fw}/libSDL3.dylib")
  file(CHMOD "${fw}/libSDL3.dylib" PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
  run(install_name_tool -id "@rpath/libSDL3.dylib" "${fw}/libSDL3.dylib")
endif()

# Nothing may still point outside the app or the system.
execute_process(COMMAND otool -L "${exe}" "${fw}/${sdl2_file}" OUTPUT_VARIABLE deps)
if(deps MATCHES "/opt/homebrew|/usr/local/(opt|lib|Cellar)")
  message(FATAL_ERROR "the app still depends on a library outside it:\n${deps}")
endif()

# Ad-hoc signature (install_name_tool invalidated the old one). A Developer ID
# signature and notarization would remove the "unidentified developer" warning.
run(codesign --force --sign - "${fw}/${sdl2_file}")
if(SDL3_LIB)
  run(codesign --force --sign - "${fw}/libSDL3.dylib")
endif()
run(codesign --force --deep --sign - "${app}")
run(codesign --verify --deep --strict "${app}")

run(ln -s /Applications "${stage}/Applications")
run(hdiutil create -volname "${VOLUME}" -srcfolder "${stage}" -ov -format UDZO "${dmg}")
message(STATUS "Made ${dmg}")
