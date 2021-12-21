cmake_minimum_required(VERSION 3.8 FATAL_ERROR)

find_program(CLANG_FORMAT_EXE NAMES "clang-format" DOC "Path to clang-format executable")

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

set(SDF_SHADOWS_SOURCES ${PROJECT_SOURCE_DIR}/src/main.cpp)
file(GLOB_RECURSE SHADER_SOURCES ${PROJECT_SOURCE_DIR}/src/*.glsl)

if (APPLE)
    add_executable(SDFShadows MACOSX_BUNDLE ${SDF_SHADOWS_SOURCES} ${SHADER_SOURCES} ${ASSET_SOURCES})
    set(MACOSX_BUNDLE_BUNDLE_NAME "SDFShadows") 
    set_source_files_properties(${SHADER_SOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/shader)
    set_source_files_properties(${ASSET_SOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
else()
    add_executable(SDFShadows ${SDF_SHADOWS_SOURCES}) 
endif()

target_link_libraries(SDFShadows dwSampleFramework)

if (NOT APPLE)
    add_custom_command(TARGET SDFShadows POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/src/shader $<TARGET_FILE_DIR:SDFShadows>/shader)
endif()

if(CLANG_FORMAT_EXE)
    add_custom_target(SDFShadows-clang-format COMMAND ${CLANG_FORMAT_EXE} -i -style=file ${SDF_SHADOWS_SOURCES} ${SHADER_SOURCES})
endif()

set_property(TARGET SDFShadows PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/$(Configuration)")