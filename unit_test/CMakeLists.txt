# Copyright (c) 2016-2018 LG Electronics, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 2.8.7)

set(TEST_APP_NAME activitymanager_test)

project(${TEST_APP_NAME} C CXX)

# Added from src/activitymanager/CMakeLists.txt so the daemon's compiler
# flags, include directories and libraries are all in scope here.
get_property(ORIGIN_SOURCES TARGET activitymanager PROPERTY "SOURCES")
get_property(ORIGIN_INCLUDES TARGET activitymanager PROPERTY "INCLUDE_DIRECTORIES")
get_property(ORIGIN_LIBS TARGET activitymanager PROPERTY "LINK_LIBRARIES")

set(TARGET_SRC)
foreach(SRC ${ORIGIN_SOURCES})
    # Main.cpp carries the daemon's main(); the test has its own.
    if(NOT ${SRC} MATCHES "ServiceApp.cpp" AND NOT ${SRC} MATCHES "/Main.cpp$")
        set(TARGET_SRC ${TARGET_SRC} ${SRC})
    endif()
endforeach()

include_directories(${ORIGIN_INCLUDES} ${WEBOS_GTEST_INCLUDES})
add_definitions(-DUNITTEST)

file(GLOB_RECURSE TEST_SRC ./*.cpp)
file(GLOB_RECURSE TEST_HEADERS ./*.h)

SET(TEST_DIR "")
FOREACH(header ${TEST_HEADERS})
    GET_FILENAME_COMPONENT(dir_path ${header} PATH)
    SET(TEST_DIR ${TEST_DIR} ${dir_path})
ENDFOREACH()
LIST(REMOVE_DUPLICATES TEST_DIR)
include_directories(${TEST_DIR})

add_executable(${TEST_APP_NAME} ${TEST_SRC} ${TARGET_SRC})
set (TEST_DEP ${ORIGIN_LIBS} ${LIBS} ${ARGN} ${GTEST_LIBRARIES} )
target_link_libraries(${TEST_APP_NAME} ${TEST_DEP} -lpthread ${WEBOS_GTEST_LIBRARIES})
add_test(NAME ${TEST_APP_NAME} COMMAND ${TEST_APP_NAME})
install(TARGETS ${PROJECT_NAME} DESTINATION ${WEBOS_INSTALL_LIBEXEC}/tests/
        PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ)
