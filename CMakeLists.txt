# Copyright (C) 2022 Jens Pots.
# License: AGPL-3.0-only.

cmake_minimum_required(VERSION 3.21)
set(CMAKE_C_STANDARD 11)

project(yip C)
add_executable(yip src/yip.c)
install(TARGETS yip)

