cmake_minimum_required(VERSION 3.20)
project(Tetris C CXX)

# Require modern C++
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Export compile commands for VS Code IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add SDL3
add_subdirectory(external/SDL)
add_subdirectory(external/SDL_net)

# Collect all source files from the src/ directory (excluding tests)
file(GLOB SOURCES "src/*.cpp")
list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/test_main.cpp")

# Create the executable
if(ANDROID)
    add_library(main SHARED ${SOURCES})
    
    # Default CLIENT_ID if not passed via Gradle
    if(NOT DEFINED CLIENT_ID)
        set(CLIENT_ID 1)
    endif()
    
    # Use 'main' for Android-specific commands
    target_include_directories(main PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src external/SDL_net/include/SDL3_net)
    target_compile_definitions(main PRIVATE CLIENT_ID=${CLIENT_ID})
    target_link_libraries(main PRIVATE SDL3::SDL3 SDL3_net::SDL3_net)
else()
    add_executable(Tetris1 ${SOURCES})
    target_include_directories(Tetris1 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src external/SDL_net/include/SDL3_net)
    target_compile_definitions(Tetris1 PRIVATE CLIENT_ID=1)
    target_link_libraries(Tetris1 PRIVATE SDL3::SDL3 SDL3_net::SDL3_net)

    add_executable(Tetris2 ${SOURCES})
    target_include_directories(Tetris2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src external/SDL_net/include/SDL3_net)
    target_compile_definitions(Tetris2 PRIVATE CLIENT_ID=2)
    target_link_libraries(Tetris2 PRIVATE SDL3::SDL3 SDL3_net::SDL3_net)

    # Add TetrisTests target
    add_executable(TetrisTests src/test_main.cpp src/network.cpp)
    target_include_directories(TetrisTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src external/SDL_net/include/SDL3_net)
    target_compile_definitions(TetrisTests PRIVATE CLIENT_ID=1)
    target_link_libraries(TetrisTests PRIVATE SDL3::SDL3 SDL3_net::SDL3_net)
endif()
