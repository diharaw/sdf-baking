cmake_minimum_required(VERSION 3.8 FATAL_ERROR)

find_program(CLANG_FORMAT_EXE NAMES "clang-format" DOC "Path to clang-format executable")

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

set(SDF_SHADOWS_SOURCES ${PROJECT_SOURCE_DIR}/src/main.cpp)
file(GLOB_RECURSE SHADER_SOURCES ${PROJECT_SOURCE_DIR}/src/*.glsl)

if (APPLE)
    add_executable(SDFBaking MACOSX_BUNDLE ${SDF_SHADOWS_SOURCES} ${SHADER_SOURCES} ${ASSET_SOURCES})
    set(MACOSX_BUNDLE_BUNDLE_NAME "SDFBaking") 
    set_source_files_properties(${SHADER_SOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/shader)
    set_source_files_properties(${ASSET_SOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
else()
    add_executable(SDFBaking ${SDF_SHADOWS_SOURCES}) 
endif()

target_link_libraries(SDFBaking dwSampleFramework)

if (NOT APPLE)
    add_custom_command(TARGET SDFBaking POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/src/shader $<TARGET_FILE_DIR:SDFBaking>/shader)
    add_custom_command(TARGET SDFBaking POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/data/mesh $<TARGET_FILE_DIR:SDFBaking>/mesh)
endif()

if(CLANG_FORMAT_EXE)
    add_custom_target(SDFBaking-clang-format COMMAND ${CLANG_FORMAT_EXE} -i -style=file ${SDF_SHADOWS_SOURCES} ${SHADER_SOURCES})
endif()

set_property(TARGET SDFBaking PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/bin/$(Configuration)")