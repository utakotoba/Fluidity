cmake_minimum_required(VERSION 3.16)

# use clang toolchain
set(IDF_TOOLCHAIN clang CACHE STRING "Force using the clang toolchain")

# omit idf clang toolchain warnings (temporarily suppress)
macro(_orig_message)
  _message(${ARGV})
endmacro()

function(message)
  if("${ARGV0}" STREQUAL "WARNING"
    AND "${ARGV1}" MATCHES "Building ESP-IDF with clang is an experimental feature")
  else()
    _orig_message(${ARGV})
  endif()
endfunction()

# setup idf
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# idf globals
idf_build_set_property(MINIMAL_BUILD ON)

# project meta
project(fluidity)
