#pragma once

#include "i2c_bus.h"
#include <memory>
#include <cstdint>
#include <chrono>

class VL53L0X {
public:
    explicit VL53L0X(std::shared_ptr<I2CBus> bus, uint8_t addr = 0x29);

    bool init();
    [[nodiscard]] int readDistanceMm() const;


    bool isObjectDetected(int threshold_mm = 250,
                          int hold_duration_ms = 1200,
                          int max_jitter_mm = 60);

    void resetDetectionState();

private:
    std::shared_ptr<I2CBus> m_bus;
    uint8_t m_addr;
    uint8_t m_stop_variable;

    bool performSingleRefCalibration(uint8_t vhv_init_byte) const;

    // Поля состояния для детекции
    bool m_tracking = false;
    int m_last_stable_dist = -1;
    int m_consecutive_drops = 0;
    std::chrono::steady_clock::time_point m_first_seen_time;
};