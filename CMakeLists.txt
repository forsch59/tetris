cmake_minimum_required(VERSION 3.20)
project(Tetris C CXX)

# Require modern C++
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Export compile commands for VS Code IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add SDL3
add_subdirectory(external/SDL)

# Collect all source files from the src/ directory
file(GLOB SOURCES "src/*.cpp")

# Create the executable
if(ANDROID)
    add_library(main SHARED ${SOURCES})
    
    # Use 'main' for Android-specific commands
    target_include_directories(main PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(main PRIVATE SDL3::SDL3)
else()
    add_executable(Tetris ${SOURCES})
    
    # Use 'Tetris' for Linux commands
    target_include_directories(Tetris PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(Tetris PRIVATE SDL3::SDL3)
endif()
