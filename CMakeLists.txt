# cmake file for building PandoraPFA
#-------------------------------------------------------------------------------------------------------------------------------------------
cmake_minimum_required(VERSION 2.8.2 FATAL_ERROR)

if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
    message(FATAL_ERROR "PandoraPFA requires an out-of-source build.")
endif()

project(PandoraPFA)

set(${PROJECT_NAME}_VERSION_MAJOR 03)
set(${PROJECT_NAME}_VERSION_MINOR 15)
set(${PROJECT_NAME}_VERSION_PATCH 01)
set(${PROJECT_NAME}_VERSION "${PandoraSDK_VERSION_MAJOR}.${PandoraSDK_VERSION_MINOR}.${PandoraSDK_VERSION_PATCH}")

#-------------------------------------------------------------------------------------------------------------------------------------------
# Dependencies
find_path(pandora_cmake_path "PandoraCMakeSettings.cmake" "${CMAKE_CURRENT_LIST_DIR}/cmakemodules")
if (pandora_cmake_path)
    list(APPEND CMAKE_MODULE_PATH ${pandora_cmake_path})
endif()
include(PandoraCMakeSettings)

#-------------------------------------------------------------------------------------------------------------------------------------------
# External project versions
set(git_repository_root "https://github.com/PandoraPFA")

set(PandoraSDK_repository "PandoraSDK.git")
set(PandoraMonitoring_repository "PandoraMonitoring.git")
set(LCContent_repository "LCContent.git")
set(LArContent_repository "LArContent.git")
set(ExampleContent_repository "ExampleContent.git")

set(PandoraSDK_version "origin/master")
set(PandoraMonitoring_version "origin/master")
set(LArContent_version "origin/master")
set(LCContent_version "origin/master")
set(ExampleContent_version "origin/master")
set(Eigen3_version "3.3.5")

#-------------------------------------------------------------------------------------------------------------------------------------------
# External project selection
include(CMakeDependentOption)
option(PANDORA_MONITORING "Build PandoraMonitoring library (requires ROOT)" OFF)
option(INSTALL_DOC "Set to OFF to skip build/install Documentation" OFF)
cmake_dependent_option(LAR_PANDORA_CONTENT "Build LAr Pandora content library" ON "PANDORA_LAR_CONTENT OR LAR_PANDORA_CONTENT" OFF)
cmake_dependent_option(LC_PANDORA_CONTENT "Build LC Pandora content library" ON "PANDORA_LC_CONTENT OR LC_PANDORA_CONTENT" OFF)
cmake_dependent_option(EXAMPLE_PANDORA_CONTENT "Build Example Pandora content library" ON "PANDORA_EXAMPLE_CONTENT OR EXAMPLE_PANDORA_CONTENT" OFF)

message(STATUS "PANDORA_SDK: ON")
message(STATUS "PANDORA_MONITORING: ${PANDORA_MONITORING}")
message(STATUS "LAR_PANDORA_CONTENT: ${LAR_PANDORA_CONTENT}")
message(STATUS "LC_PANDORA_CONTENT: ${LC_PANDORA_CONTENT}")
message(STATUS "EXAMPLE_PANDORA_CONTENT: ${EXAMPLE_PANDORA_CONTENT}")
message(STATUS "INSTALL_DOC: ${INSTALL_DOC}")

#-------------------------------------------------------------------------------------------------------------------------------------------
# Pass arguments to external projects
if(CMAKE_CXX_FLAGS)
    list(APPEND COMMON_CMAKE_ARGS "-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}")
endif()

if(CMAKE_C_COMPILER)
    list(APPEND COMMON_CMAKE_ARGS "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}")
endif()

if(CMAKE_CXX_COMPILER)
    list(APPEND COMMON_CMAKE_ARGS "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}")
endif()

if(NOT LAR_CONTENT_LIBRARY_NAME STREQUAL "LArPandoraContent")
    set(LAR_CONTENT_LIBRARY_NAME "LArContent")
endif()

if(NOT LC_CONTENT_LIBRARY_NAME STREQUAL "LCPandoraContent")
    set(LC_CONTENT_LIBRARY_NAME "LCContent")
endif()

if(NOT EXAMPLE_CONTENT_LIBRARY_NAME STREQUAL "ExamplePandoraContent")
    set(EXAMPLE_CONTENT_LIBRARY_NAME "ExampleContent")
endif()

if(PANDORA_MONITORING)
    set(MONITORING_DEPENDENCY "PandoraMonitoring")
    set(MONITORING_CMAKE_ARGS "-DPANDORA_MONITORING=ON")
    list(APPEND CMAKE_MODULE_PATH ${ROOT_DIR})
endif()

# In order to pass semicolon-separated lists to all packages, need to separate elements with '%' instead of standard separator ';'
foreach(_path ${CMAKE_MODULE_PATH})
    set(CMAKE_MODULE_PATH_FIXED ${CMAKE_MODULE_PATH_FIXED}%${_path})
endforeach()

list(APPEND COMMON_CMAKE_ARGS
    "-DCMAKE_MODULE_PATH=${CMAKE_MODULE_PATH_FIXED}"
    "-DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}"
    "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
    "-DINSTALL_DOC=${INSTALL_DOC}")

#-------------------------------------------------------------------------------------------------------------------------------------------
# External projects
include(ExternalProject)

ExternalProject_Add(PandoraSDK
    GIT_REPOSITORY ${git_repository_root}/${PandoraSDK_repository}
    GIT_TAG ${PandoraSDK_version}
    CMAKE_ARGS ${COMMON_CMAKE_ARGS}
    PREFIX PandoraSDK-${PandoraSDK_version}
    LIST_SEPARATOR %
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/PandoraSDK-${PandoraSDK_version}
)

if(PANDORA_MONITORING)
    ExternalProject_Add(PandoraMonitoring
        DEPENDS PandoraSDK
        GIT_REPOSITORY ${git_repository_root}/${PandoraMonitoring_repository}
        GIT_TAG ${PandoraMonitoring_version}
        CMAKE_ARGS ${COMMON_CMAKE_ARGS}
        PREFIX PandoraMonitoring-${PandoraMonitoring_version}
        LIST_SEPARATOR %
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/PandoraMonitoring-${PandoraMonitoring_version}
    )
endif()

if(LAR_PANDORA_CONTENT)
    ExternalProject_Add(Eigen3
        URL "http://bitbucket.org/eigen/eigen/get/${Eigen3_version}.tar.gz"
        CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_SOURCE_DIR}/Eigen3-${Eigen3_version}
        LIST_SEPARATOR %
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Eigen3-${Eigen3_version}
    )

    ExternalProject_Add(${LAR_CONTENT_LIBRARY_NAME}
        DEPENDS PandoraSDK ${MONITORING_DEPENDENCY} Eigen3
        GIT_REPOSITORY ${git_repository_root}/${LArContent_repository}
        GIT_TAG ${LArContent_version}
        CMAKE_ARGS ${COMMON_CMAKE_ARGS} ${MONITORING_CMAKE_ARGS} -DLAR_CONTENT_LIBRARY_NAME=${LAR_CONTENT_LIBRARY_NAME} -DEigen3_DIR=${CMAKE_CURRENT_SOURCE_DIR}/Eigen3-${Eigen3_version}/share/eigen3/cmake/
        PREFIX ${LAR_CONTENT_LIBRARY_NAME}-${LArContent_version}
        LIST_SEPARATOR %
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${LAR_CONTENT_LIBRARY_NAME}-${LArContent_version}
    )
endif()

if(LC_PANDORA_CONTENT)
    ExternalProject_Add(${LC_CONTENT_LIBRARY_NAME}
        DEPENDS PandoraSDK ${MONITORING_DEPENDENCY}
        GIT_REPOSITORY ${git_repository_root}/${LCContent_repository}
        GIT_TAG ${LCContent_version}
        CMAKE_ARGS ${COMMON_CMAKE_ARGS} ${MONITORING_CMAKE_ARGS} -DLC_CONTENT_LIBRARY_NAME=${LC_CONTENT_LIBRARY_NAME}
        PREFIX ${LC_CONTENT_LIBRARY_NAME}-${LCContent_version}
        LIST_SEPARATOR %
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${LC_CONTENT_LIBRARY_NAME}-${LCContent_version}
    )
endif()

if(EXAMPLE_PANDORA_CONTENT)
    ExternalProject_Add(${EXAMPLE_CONTENT_LIBRARY_NAME}
        DEPENDS PandoraSDK ${MONITORING_DEPENDENCY}
        GIT_REPOSITORY ${git_repository_root}/${ExampleContent_repository}
        GIT_TAG ${ExampleContent_version}
        CMAKE_ARGS ${COMMON_CMAKE_ARGS} ${MONITORING_CMAKE_ARGS} -DEXAMPLE_CONTENT_LIBRARY_NAME=${EXAMPLE_CONTENT_LIBRARY_NAME}
        PREFIX ${EXAMPLE_CONTENT_LIBRARY_NAME}-${ExampleContent_version}
        LIST_SEPARATOR %
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${EXAMPLE_CONTENT_LIBRARY_NAME}-${ExampleContent_version}
    )
endif()

install(DIRECTORY "cmakemodules/" DESTINATION "cmakemodules")

#-------------------------------------------------------------------------------------------------------------------------------------------
# display some variables and write them to cache
PANDORA_DISPLAY_STD_VARIABLES()

install(FILES "ChangeLog.txt" DESTINATION "doc")
