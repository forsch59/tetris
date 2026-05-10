cmake_minimum_required(VERSION 3.20)
project(Tetris C CXX)

# Require modern C++
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Export compile commands for VS Code IntelliSense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable clang-tidy with warnings as errors
# set(CMAKE_CXX_CLANG_TIDY "clang-tidy;--warnings-as-errors=*")
set(CMAKE_CXX_CLANG_TIDY "clang-tidy;--use-color;--extra-arg=-fcolor-diagnostics")

# Add SDL3
add_subdirectory(external/SDL)
add_subdirectory(external/SDL_net)

# Collect all source files from the src/ directory (excluding tests)
file(GLOB SOURCES "src/*.cpp")
list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/test_main.cpp")

# Helper function to define targets
function(add_tetris_target TARGET_NAME CLIENT_ID_VAL IS_TEST)
    if(IS_TEST)
        add_executable(${TARGET_NAME} src/test_main.cpp src/network.cpp)
    elseif(ANDROID)
        add_library(${TARGET_NAME} SHARED ${SOURCES})
    else()
        add_executable(${TARGET_NAME} ${SOURCES})
    endif()

    target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src external/SDL_net/include/SDL3_net)
    target_compile_definitions(${TARGET_NAME} PRIVATE CLIENT_ID=${CLIENT_ID_VAL})
    target_link_libraries(${TARGET_NAME} PRIVATE SDL3::SDL3 SDL3_net::SDL3_net)
endfunction()

# Define targets
if(ANDROID)
    if(NOT DEFINED CLIENT_ID)
        set(CLIENT_ID 1)
    endif()
    add_tetris_target(main ${CLIENT_ID} FALSE)
else()
    add_tetris_target(Tetris1 1 FALSE)
    add_tetris_target(Tetris2 2 FALSE)
    add_tetris_target(TetrisTests 1 TRUE)
endif()
