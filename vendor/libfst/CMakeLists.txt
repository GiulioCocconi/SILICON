#  Copyright (C) 2026 Giulio Cocconi

#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.

#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.

#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <http://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.25)

project(libfst)

# ZLIB is required by FST
find_package(BZip2 REQUIRED)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

# TODO: Use `fst_win_unistd.h` when compiling with MSVC
set(FST_SOURCES
        fstapi.c
        lz4.c
        fastlz.c
)

add_library(fst STATIC ${FST_SOURCES})

target_include_directories(fst PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# --- 4. Link Dependencies ---

# Link ZLIB
target_link_libraries(fst PRIVATE ZLIB::ZLIB
        BZip2::BZip2
        Threads::Threads)
