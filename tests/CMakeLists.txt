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
add_executable(num_formatting_tests num_formatting.cpp)
add_executable(libfst_tests fstlib.cpp)
add_executable(fstwrapper_tests fstWrapper.cpp)
add_executable(circuit_tests circuit.cpp)
add_executable(factory_tests factory.cpp)
add_executable(logger_tests logger.cpp)
add_executable(libavoid_tests libavoid.cpp ${CMAKE_SOURCE_DIR}/src/ui/common/wireRouting.cpp)

foreach (target logic_tests arithmetic_tests utils_tests num_formatting_tests libfst_tests fstwrapper_tests circuit_tests factory_tests logger_tests)
    set_target_properties(${target} PROPERTIES
            AUTOMOC OFF
            AUTOUIC OFF
            AUTORCC OFF)
    target_link_libraries(${target} GTest::gtest_main GTest::gtest silicon_core silicon_logging)
    gtest_discover_tests(${target})
endforeach ()

set_target_properties(libavoid_tests PROPERTIES
        AUTOMOC OFF
        AUTOUIC OFF
        AUTORCC OFF)
target_link_libraries(libavoid_tests PRIVATE GTest::gtest_main GTest::gtest avoid Qt6::Core Qt6::Gui)
gtest_discover_tests(libavoid_tests DISCOVERY_MODE PRE_TEST)
