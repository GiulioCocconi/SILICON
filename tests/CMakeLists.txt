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

enable_testing()
add_executable(logic_tests logic.cpp)
add_executable(arithmetic_tests arithmetic.cpp)
add_executable(utils_tests utils.cpp)
add_executable(libfst_tests fstlib.cpp)
add_executable(circuit_tests circuit.cpp)
add_executable(factory_tests factory.cpp)



target_sources(logic_tests
        PRIVATE
        ${src_dir}/extraComponents/arithmetic.cpp
        ${src_dir}/extraComponents/utils.cpp
        ${COMMON_SOURCE_FILES})


target_sources(arithmetic_tests
        PRIVATE
        ${src_dir}/extraComponents/arithmetic.cpp
        ${src_dir}/extraComponents/utils.cpp
        ${COMMON_SOURCE_FILES})

target_sources(utils_tests
        PRIVATE
        ${src_dir}/extraComponents/arithmetic.cpp
        ${src_dir}/extraComponents/utils.cpp
        ${COMMON_SOURCE_FILES})

target_sources(circuit_tests
        PRIVATE
        ${src_dir}/extraComponents/arithmetic.cpp
        ${src_dir}/extraComponents/utils.cpp
        ${COMMON_SOURCE_FILES})

target_sources(factory_tests
        PRIVATE
        ${src_dir}/extraComponents/arithmetic.cpp
        ${src_dir}/extraComponents/utils.cpp
        ${COMMON_SOURCE_FILES})

target_sources(libfst_tests
        PRIVATE
        ${src_dir}/extraComponents/arithmetic.cpp
        ${src_dir}/extraComponents/utils.cpp
        ${COMMON_SOURCE_FILES})

foreach (target logic_tests arithmetic_tests utils_tests libfst_tests circuit_tests factory_tests)
    target_link_libraries(${target} GTest::gtest_main GTest::gtest Boost::graph nlohmann_json)
    gtest_discover_tests(${target})
endforeach ()

target_link_libraries(libfst_tests fst)