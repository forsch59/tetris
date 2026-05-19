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
    static const PowerDefinition fallback = {0, 0, false, false, PowerDisableCriteria::NONE, 0};
    static const PowerDefinition powers[] = {
        {1, 5000, false, false, PowerDisableCriteria::TIME_ELAPSED, 0}, // Example Power 1
        {2, 0, true, false, PowerDisableCriteria::LINES_CLEARED, 2}     // Example Power 2
    };

    for (const auto& power : powers) {
        if (power.power_id == power_id) {
            return power;
        }
    }
    return fallback;
}
