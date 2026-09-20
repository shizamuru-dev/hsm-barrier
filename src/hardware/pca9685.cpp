//
// Created by cyn on 9/19/26.
//

#include "../../include/hardware/pca9685.h"

#include <cmath>
#include <unistd.h>
#include <stdexcept>

PCA9685::PCA9685(std::shared_ptr<I2CBus> bus, uint8_t addr)
    : m_bus(std::move(bus)), m_addr(addr) {}

void PCA9685::init(const float freq_hz) const {
    if (!m_bus->writeByte(m_addr, MODE1, 0x00)) {
        throw std::runtime_error("PCA9685: ошибка пробуждения");
    }
    usleep(5000);

    // OUTDRV (push-pull)
    m_bus->writeByte(m_addr, MODE2, 0x04);

    // Настройка частоты
    const float prescaleval = 25000000.0f / (4096.0f * freq_hz) - 1.0f;
    const auto prescale = static_cast<uint8_t>(std::round(prescaleval));
    uint8_t oldmode = 0;

    m_bus->readByte(m_addr, MODE1, oldmode);
    const uint8_t sleep_mode = (oldmode & 0x7F) | 0x10;
    m_bus->writeByte(m_addr, MODE1, sleep_mode);
    m_bus->writeByte(m_addr, PRESCALE, prescale);
    m_bus->writeByte(m_addr, MODE1, oldmode);
    usleep(5000);

    m_bus->writeByte(m_addr, MODE1, oldmode | 0xA1);
}

bool PCA9685::setPwm(const uint8_t channel, const uint16_t on, const uint16_t off) const {
    if (channel > 15) return false;
    const uint8_t reg = LED0_ON_L + 4 * channel;
    const uint8_t data[4] = {
        static_cast<uint8_t>(on & 0xFF),
        static_cast<uint8_t>((on >> 8) & 0x0F),
        static_cast<uint8_t>(off & 0xFF),
        static_cast<uint8_t>((off >> 8) & 0x0F)
    };
    return m_bus->writeBytes(m_addr, reg, data, 4);
}

bool PCA9685::setFullOff(const uint8_t channel) const {
    if (channel > 15) return false;
    const uint8_t reg = LED0_ON_L + 4 * channel;
    constexpr uint8_t data[4] = {0x00, 0x00, 0x00, 0x10};
    return m_bus->writeBytes(m_addr, reg, data, 4);
}
