cmake_minimum_required(VERSION 3.17)

# Project Information
project(WotlkBot)

# Set C++ Standard and Build Settings
set(CMAKE_CXX_STANDARD 26)
set(BUILD_SHARED_LIBS FALSE)  # Disable building shared libraries
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # Export compile commands for better IDE support

# Define the directory for ImGui
set(IMGUI_DIR imgui)

# Add external libraries as subdirectories
add_subdirectory(fmt)  # fmt for string formatting
add_subdirectory(glfw)  # GLFW for window management

# Set GLFW build options
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

# Find required libraries
find_library(XDO_LIBRARY NAMES xdo libxdo PATHS /usr/lib REQUIRED)
find_package(X11 REQUIRED COMPONENTS Xfixes Xrender)
find_package(PkgConfig REQUIRED)
find_library(TCMALLOC_LIB NAMES tcmalloc REQUIRED)
find_package(OpenGL REQUIRED)
find_package(SFML COMPONENTS Audio)
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()
find_package(Boost 1.81.0 COMPONENTS serialization filesystem thread chrono process REQUIRED)

# Check for TCMalloc
if(TCMALLOC_LIB)
    message(STATUS "Found Google TCMalloc: ${TCMALLOC_LIB}")
    add_compile_definitions(HAS_TCMALLOC)
else()
    message(STATUS "Google TCMalloc not found")
endif()

include_directories(
        ${X11_INCLUDE_DIR}
        ${XFIXES_INCLUDE_DIRS}
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
        ${IMGUI_DIR}/misc/cpp
        ${OPENGL_INCLUDE_DIRS}
        ${LIBAO_INCLUDE_DIR}
        arraydatabase
)

set(SOURCES
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
        spell_defines.hpp
        consolidation_header.hpp
        main3.cpp
        sounds.hpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

set(COMMON_FLAGS "-Werror -Wextra -pedantic-errors -Wabi -pipe")
add_compile_options(${COMMON_FLAGS})

# Add compile options based on build type
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Debug build settings
    add_compile_options("-g" "-O0")  # Include debug symbols and disable optimizations
    add_compile_definitions(BUILD_TYPE_DEFINE=DEBUG DEBUG=1)
    set(COMMON_FLAGS "${COMMON_FLAGS} -Wall -Wextra -Wconversion -pedantic-errors -Wabi -pipe")
    add_compile_options(${COMMON_FLAGS})
    message(STATUS "Building in Debug mode")
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_STRIP "/usr/bin/strip")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_STRIP} $<TARGET_FILE:${PROJECT_NAME}>
            COMMENT "Stripping executable"
    )
    target_compile_options(${PROJECT_NAME} PRIVATE -O3)
    set_property(TARGET ${PROJECT_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    add_link_options("-Wl,-gc-sections,--strip-all")
    add_compile_definitions(BUILD_TYPE_DEFINE=RELEASE)
    message(STATUS "Building in Release mode")
endif()

# Link libraries to the executable
target_link_libraries(${PROJECT_NAME} PRIVATE
        ${XDO_LIBRARY}
        glfw
        ${X11_LIBRARIES}
        ${X11_Xfixes_LIB}
        ${CMAKE_DL_LIBS}
        sfml-audio
        pthread
        ${OPENGL_LIBRARIES}
        fmt::fmt
        ${Boost_LIBRARIES}
        ${TCMALLOC_LIB}
)

# Configuration messages
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ compiler ID: ${CMAKE_CXX_COMPILER_ID}")

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options("-Weverything")  # Enables all warnings with Clang
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GCC")
    add_compile_options("-Wall")  # Enables more warnings for GCC
endif()

# Generate a random string for compile-time definition
string(RANDOM LENGTH 6 ALPHABET "0123456789" RANDOM_STRING)
add_compile_definitions(RANDOM_MACRO="${RANDOM_STRING}")
message(STATUS "Random Seed: ${RANDOM_STRING}")

# Additional compile-time definitions
add_compile_definitions(CXX_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}")
add_compile_definitions(BUILD_TYPE_RAW="${CMAKE_BUILD_TYPE}")
