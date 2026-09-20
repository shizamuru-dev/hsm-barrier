#pragma once

#include "pca9685.h"
#include <memory>

class Servo {
public:
    Servo(std::shared_ptr<PCA9685> pca, uint8_t channel);

    bool setAngle(int angle) const;
    bool open() const;   // 90°
    bool close() const;  // 0°
    bool detach() const;

private:
    std::shared_ptr<PCA9685> m_pca;
    uint8_t m_channel;

    static constexpr uint16_t MIN_TICKS = 123; // 600 мкс
    static constexpr uint16_t MAX_TICKS = 491; // 2400 мкс
};