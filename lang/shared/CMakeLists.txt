cmake_minimum_required(VERSION 3.20)
project(otium_native LANGUAGES C)

option(OTIUM_WITH_UI "Build the SDL/TTF/Yoga bridge" OFF)
set(OTIUM_OUTPUT_DIR "${CMAKE_BINARY_DIR}/.." CACHE PATH "Native artifacts")
get_filename_component(OTIUM_OUTPUT_DIR "${OTIUM_OUTPUT_DIR}" ABSOLUTE)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OTIUM_OUTPUT_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${OTIUM_OUTPUT_DIR}")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
include(CTest)

# Export objects as well as libraries for the C runtime's standalone archive.
# Both runtimes use these compilation rules; no runtime owns shared sources.
function(otium_object name source)
  add_library(${name}-object OBJECT ${source})
  target_compile_features(${name}-object PRIVATE c_std_11)
  target_compile_options(${name}-object PRIVATE -Wall -Wextra -Werror)
  set(output "${OTIUM_OUTPUT_DIR}/objects/${name}.o")
  add_custom_command(OUTPUT "${output}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${OTIUM_OUTPUT_DIR}/objects"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different $<TARGET_OBJECTS:${name}-object> "${output}"
    DEPENDS ${name}-object $<TARGET_OBJECTS:${name}-object>
    VERBATIM)
  add_custom_target(${name}-export DEPENDS "${output}")
  add_dependencies(native-objects ${name}-export)
endfunction()

add_custom_target(native-objects)
otium_object(utf8 support/utf8.c)
otium_object(ffi-native ffi/native.c)
otium_object(dev-transport dev/transport.c)
otium_object(line-edit line-edit.c)
otium_object(bestline vendor/bestline/bestline.c)
target_include_directories(line-edit-object PRIVATE vendor/bestline)
target_compile_options(bestline-object PRIVATE -w -std=c11)

add_library(otium-line SHARED $<TARGET_OBJECTS:line-edit-object> $<TARGET_OBJECTS:bestline-object>)
add_library(otium-line-static STATIC $<TARGET_OBJECTS:line-edit-object> $<TARGET_OBJECTS:bestline-object>)
set_target_properties(otium-line-static PROPERTIES OUTPUT_NAME otium-line)
add_library(otium-dev SHARED $<TARGET_OBJECTS:dev-transport-object> $<TARGET_OBJECTS:utf8-object>)
add_library(otium-ffi SHARED EXCLUDE_FROM_ALL $<TARGET_OBJECTS:ffi-native-object> $<TARGET_OBJECTS:utf8-object>)
target_link_libraries(otium-ffi PRIVATE ${CMAKE_DL_LIBS})

if(BUILD_TESTING)
  add_executable(otium-utf8-checks tests/native/utf8.c $<TARGET_OBJECTS:utf8-object>)
  target_include_directories(otium-utf8-checks PRIVATE support)
  target_compile_options(otium-utf8-checks PRIVATE -Wall -Wextra -Werror)
  add_test(NAME utf8 COMMAND otium-utf8-checks)
endif()

if(OTIUM_WITH_UI)
  enable_language(CXX)
  add_subdirectory(ui)
endif()
