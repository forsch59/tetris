#pragma once

#include <cstdint>

enum class PowerDisableCriteria : uint8_t {
    NONE = 0,
    TIME_ELAPSED = 1,
    LINES_CLEARED = 2,
    PIECES_DROPPED = 3,
    TETRIS_SCORED = 4
};

struct PowerDefinition {
    uint16_t power_id;
    uint16_t duration_ms;
    bool is_frozen;
    bool is_self_inflicted;
    PowerDisableCriteria criteria;
    uint8_t criteria_threshold;
};

// Example function to get a power definition locally
inline const PowerDefinition& get_power_definition(uint16_t power_id) {
    // This is a placeholder registry that would normally be populated from a config file.
    static const PowerDefinition fallback = {
        .power_id = 0,
        .duration_ms = 0,
        .is_frozen = false,
        .is_self_inflicted = false,
        .criteria = PowerDisableCriteria::NONE,
        .criteria_threshold = 0
    };
    static const PowerDefinition powers[] = {
        {.power_id = 1, .duration_ms = 5000, .is_frozen = false, .is_self_inflicted = false, .criteria = PowerDisableCriteria::TIME_ELAPSED, .criteria_threshold = 0}, // Example Power 1
        {.power_id = 2, .duration_ms = 0, .is_frozen = true, .is_self_inflicted = false, .criteria = PowerDisableCriteria::LINES_CLEARED, .criteria_threshold = 2}     // Example Power 2
    };

    for (const auto& power : powers) {
        if (power.power_id == power_id) {
            return power;
        }
    }
    return fallback;
}
