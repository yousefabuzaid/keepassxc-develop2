# Copyright (C) 2023 KeePassXC Team <team@keepassxc.org>
# Copyright (C) 2010 Felix Geyer <debfx@fobos.de>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 or (at your option)
# version 3 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

# Set the minimum required version of CMake for this project
cmake_minimum_required(VERSION 3.28)

# Define the project and the languages used
project(keepassxc-develop)

# Include the necessary directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/srccm
    ${CMAKE_CURRENT_BINARY_DIR}/../src
    ${googletest_SOURCE_DIR}/include  # Include Google Test headers
)

# Add the test executable
add_executable(my_tests my_tests.cpp)

# Link against Google Test and KeePassXC core (or other necessary libraries)
target_link_libraries(my_tests gtest_main keepassx_core)

# Optionally, register your tests to be run with CMake's testing system, CTest
add_test(NAME MyTests COMMAND my_tests)

# Set any necessary compile definitions, if any
target_compile_definitions(my_tests PRIVATE KEEPASSX_BUILDING_TESTS)

# Set the C++ standard if not already set
set_target_properties(my_tests PROPERTIES CXX_STANDARD 17)

