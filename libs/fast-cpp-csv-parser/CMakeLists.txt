# ~~~
# Summary:      Fast CSV c++ library wrapper
# License:      GPLv3+
# Copyright (c) 2025 Alec Leamas
#
# Exports:      csv-parser::csv-parser transitive link target
# ~~~

cmake_minimum_required(VERSION 3.10)

if (TARGET csv-parser::csv-parser)
  return ()
endif ()

add_library(_csv_parser INTERFACE)
add_library(csv-parser::csv-parser ALIAS _csv_parser)

target_include_directories(
  _csv_parser INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/fast-cpp-csv-parser
)
