#pragma once

#include "i2c_bus.h"
#include <memory>

class PCA9685 {
public:
    explicit PCA9685(std::shared_ptr<I2CBus> bus, uint8_t addr = 0x61);

    void init(float freq_hz = 50.0f) const;
    bool setPwm(uint8_t channel, uint16_t on, uint16_t off) const;
    bool setFullOff(uint8_t channel) const;

private:
    std::shared_ptr<I2CBus> m_bus;
    uint8_t m_addr;

    static constexpr uint8_t MODE1    = 0x00;
    static constexpr uint8_t MODE2    = 0x01;
    static constexpr uint8_t PRESCALE = 0xFE;
    static constexpr uint8_t LED0_ON_L= 0x06;
};