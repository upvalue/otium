cmake_minimum_required(VERSION 3.20)
project(otium_ui LANGUAGES C CXX)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL REQUIRED IMPORTED_TARGET sdl3>=3.4.12)
pkg_check_modules(TTF REQUIRED IMPORTED_TARGET sdl3-ttf>=3.2.2)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(GLSLC glslc REQUIRED)
set(YOGA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../build/deps/yoga-3.2.1")
if(NOT EXISTS "${YOGA_ROOT}/yoga/CMakeLists.txt")
  message(FATAL_ERROR "Run make ui-setup to install pinned Yoga sources")
endif()
add_subdirectory("${YOGA_ROOT}/yoga" yoga)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders.h"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/embed.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/shaders" "${CMAKE_CURRENT_BINARY_DIR}/shaders.h" "${GLSLC}"
  DEPENDS shaders/embed.py shaders/rect.metal shaders/rect.vert shaders/rect.frag)
add_library(otium-ui SHARED layout.cpp renderer.c text.c application.c "${CMAKE_CURRENT_BINARY_DIR}/shaders.h")
target_include_directories(otium-ui PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(otium-ui PRIVATE yogacore PkgConfig::SDL PkgConfig::TTF)
target_compile_features(otium-ui PRIVATE c_std_11 cxx_std_20)
target_compile_options(otium-ui PRIVATE -Wall -Wextra -Werror)
set_target_properties(otium-ui PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/..")

# Installation data only; the Otium application is checked-in source.
list(GET SDL_LINK_LIBRARIES 0 SDL_LIBRARY)
file(GENERATE OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/../build/ui-config.ot"
  CONTENT "((library \"$<TARGET_FILE:otium-ui>\") (sdl \"${SDL_LIBRARY}\"))\n")

target_compile_definitions(otium-ui PRIVATE OTUI_FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fonts")

add_executable(otium-ui-text-reference ../tests/ui/text-reference.c)
add_executable(otium-ui-caret-checks ../tests/ui/caret-checks.c)
target_link_libraries(otium-ui-caret-checks PRIVATE otium-ui PkgConfig::TTF)
target_compile_options(otium-ui-caret-checks PRIVATE -Wall -Wextra -Werror)
target_link_libraries(otium-ui-text-reference PRIVATE PkgConfig::SDL PkgConfig::TTF)
target_compile_definitions(otium-ui-text-reference PRIVATE OTUI_FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fonts")

add_library(otium-ui-test-events SHARED ../tests/ui/events.c)
target_link_libraries(otium-ui-test-events PRIVATE PkgConfig::SDL)
target_compile_options(otium-ui-test-events PRIVATE -Wall -Wextra -Werror)
file(GENERATE OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/../build/ui-test-config.ot"
  CONTENT "((library \"$<TARGET_FILE:otium-ui-test-events>\"))\n")
