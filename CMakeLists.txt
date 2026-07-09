cmake_minimum_required(VERSION 3.17)

project(Torq)
list(APPEND
    CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake
)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

add_subdirectory(lib/glad)
add_subdirectory(lib/glm)
add_subdirectory(lib/stb_image)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/glfw/CMakeLists.txt")
    add_subdirectory(lib/glfw)
    set(TORQ_GLFW_TARGET glfw)
else()
    find_package(glfw3 QUIET)
    if (TARGET glfw)
        set(TORQ_GLFW_TARGET glfw)
    elseif (TARGET glfw3)
        set(TORQ_GLFW_TARGET glfw3)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GLFW3 REQUIRED IMPORTED_TARGET glfw3)
        set(TORQ_GLFW_TARGET PkgConfig::GLFW3)
    endif()
endif()
add_subdirectory(lib/noise)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Building in Debug Mode")
    add_definitions(-DDEBUG_MODE)
elseif(CMAKE_BUILD_TYPE STREQUAL "Test")
    message(STATUS "Building in Test mode")
    add_definitions(-DTEST_MODE)
else()
    message(STATUS "Building in Release mode")
    add_definitions(-DRELEASE_MODE)
endif()

file(MAKE_DIRECTORY cache/chunks)
file(MAKE_DIRECTORY cache/maps)
file(MAKE_DIRECTORY assets/atlas)
file(MAKE_DIRECTORY assets/chunks)

file(GLOB_RECURSE MY_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_executable(torq
        main.cpp
        ${MY_SOURCES}
        include/chunk_cache.h
        include/chunk_mesh.h
        include/chunk_renderer.h
        include/chunk_streaming.h
        include/region_store.h
        include/torq_utility.h
)

set_target_properties(torq
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
)

find_package(OpenGL REQUIRED)
find_package(ZLIB REQUIRED)

target_include_directories(torq PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(torq PRIVATE ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_compile_definitions(torq PRIVATE CACHE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/cache")
target_compile_definitions(torq PRIVATE CHUNKS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/chunks")
target_link_libraries(torq PRIVATE
    OpenGL::GL
    ${TORQ_GLFW_TARGET}
    glad
    glm
    stb_image
    ZLIB::ZLIB
    noise
)
