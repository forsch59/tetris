#include <iostream>
#include <cassert>
#include <vector>
#include <cstring>
#include <memory>
#include <SDL3/SDL.h>
#include "tetris_core.hpp"

// Simple test reporter
#define TEST_CASE(name) void name() { std::cout << "[TEST] Running " << #name << "..." << std::endl;
#define TEST_END std::cout << "[PASS] Test passed!" << std::endl; }

TEST_CASE(test_grid_serialization)
    TetrisBoard board1;
    TetrisBoard board2;

    // Fill board1 with some patterns
    for (int y = 0; y < TetrisBoard::HEIGHT; ++y) {
        for (int x = 0; x < TetrisBoard::WIDTH; ++x) {
            board1.grid[y][x].color = (x + y) % 7 + 1;
            board1.grid[y][x].has_crystal = (x % 3 == 0);
        }
    }

    uint8_t packed[100];
    board1.pack_grid(packed);
    board2.unpack_grid(packed);

    for (int y = 0; y < TetrisBoard::HEIGHT; ++y) {
        for (int x = 0; x < TetrisBoard::WIDTH; ++x) {
            assert(board2.grid[y][x].color == board1.grid[y][x].color);
            assert(board2.grid[y][x].has_crystal == board1.grid[y][x].has_crystal);
        }
    }
TEST_END

TEST_CASE(test_line_clearing)
    TetrisBoard board;
    
    // Fill bottom row completely
    for (int x = 0; x < TetrisBoard::WIDTH; ++x) {
        board.grid[TetrisBoard::HEIGHT - 1][x].color = 1;
        if (x == 5) board.grid[TetrisBoard::HEIGHT - 1][x].has_crystal = true;
    }
    
    // Fill second to bottom row partially
    board.grid[TetrisBoard::HEIGHT - 2][0].color = 2;
    
    board.crystals = 0;
    board.clear_lines();
    
    // Bottom row should be gone, second row should have moved down
    assert(board.grid[TetrisBoard::HEIGHT - 1][0].color == 2);
    assert(board.grid[TetrisBoard::HEIGHT - 1][1].color == 0);
    assert(board.crystals == 1);
    
    // Top row should be empty
    for (int x = 0; x < TetrisBoard::WIDTH; ++x) {
        assert(board.grid[0][x].color == 0);
    }
TEST_END

TEST_CASE(test_collision_detection)
    TetrisBoard board;
    // I piece (horizontal)
    board.current_piece.type = 1;
    board.current_piece.rotation = 0;
    board.current_piece.x = 0;
    board.current_piece.y = TetrisBoard::HEIGHT - 1;
    
    // Should collide with bottom because it's on the last row and has shape in row 1
    board.apply_rotation();
    assert(board.check_collision() == true);
    
    // Move up, should not collide
    board.current_piece.y = 5;
    assert(board.check_collision() == false);
    
    // Collide with left wall
    board.current_piece.x = -1;
    assert(board.check_collision() == true);
    
    // Collide with right wall
    board.current_piece.x = TetrisBoard::WIDTH - 2; // I piece is 4 wide in a 4x4 grid, but only uses 1 row
    // Wait, let's check I piece shape: {{0,0,0,0},{1,1,1,1},{0,0,0,0},{0,0,0,0}}
    // It's at row 1.
    board.current_piece.x = TetrisBoard::WIDTH - 3; // x=7, c=0..3 => nx=7..10. nx=10 is out.
    assert(board.check_collision() == true);
TEST_END

TEST_CASE(test_deterministic_rng)
    SharedPieceQueue q1(nullptr);
    SharedPieceQueue q2(nullptr);
    
    uint32_t seed = 12345;
    q1.rng.seed(seed);
    q1.seed_initialized = true;
    q2.rng.seed(seed);
    q2.seed_initialized = true;
    
    for (int i = 0; i < 100; ++i) {
        auto p1 = q1.get_piece_at(i);
        auto p2 = q2.get_piece_at(i);
        assert(p1.type == p2.type);
        assert(p1.crystal_r == p2.crystal_r);
        assert(p1.crystal_c == p2.crystal_c);
    }
TEST_END

int main(int argc, char* argv[]) {
    test_grid_serialization();
    test_line_clearing();
    test_collision_detection();
    test_deterministic_rng();
    
    std::cout << "\nAll tests passed successfully!" << std::endl;
    return 0;
}
