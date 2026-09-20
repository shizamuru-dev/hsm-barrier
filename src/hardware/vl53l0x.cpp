#include "../../include/hardware/vl53l0x.h"
#include <unistd.h>
#include <cstdlib>

VL53L0X::VL53L0X(std::shared_ptr<I2CBus> bus, const uint8_t addr)
    : m_bus(std::move(bus)), m_addr(addr), m_stop_variable(0) {
    resetDetectionState();
}

void VL53L0X::resetDetectionState() {
    m_tracking = false;
    m_last_stable_dist = -1;
    m_consecutive_drops = 0;
}

bool VL53L0X::performSingleRefCalibration(const uint8_t vhv_init_byte) const {
    m_bus->writeByte(m_addr, 0x00, 0x01 | vhv_init_byte);
    
    uint8_t sysrange_status = 0;
    int timeout = 100;
    while ((m_bus->readByte(m_addr, 0x00, sysrange_status)) && (sysrange_status & 0x01)) {
        usleep(1000);
        if (--timeout == 0) return false;
    }
    m_bus->writeByte(m_addr, 0x0B, 0x01);
    m_bus->writeByte(m_addr, 0x00, 0x00);
    return true;
}

bool VL53L0X::init() {
    if (uint8_t id = 0; !m_bus->readByte(m_addr, 0xC0, id) || id != 0xEE) {
        return false;
    }

    uint8_t vhv = 0;
    m_bus->readByte(m_addr, 0x89, vhv);
    m_bus->writeByte(m_addr, 0x89, vhv | 0x01);

    m_bus->writeByte(m_addr, 0x88, 0x00);
    m_bus->writeByte(m_addr, 0x80, 0x01);
    m_bus->writeByte(m_addr, 0xFF, 0x01);
    m_bus->writeByte(m_addr, 0x00, 0x00);
    m_bus->readByte(m_addr, 0x91, m_stop_variable);
    m_bus->writeByte(m_addr, 0x00, 0x01);
    m_bus->writeByte(m_addr, 0xFF, 0x00);
    m_bus->writeByte(m_addr, 0x80, 0x00);

    m_bus->writeByte(m_addr, 0x60, 0x00);
    m_bus->writeByte(m_addr, 0x83, 0x00);

    m_bus->writeByte(m_addr, 0x0B, 0x01);
    if (!performSingleRefCalibration(0x40)) return false;
    m_bus->writeByte(m_addr, 0x0B, 0x02);
    if (!performSingleRefCalibration(0x00)) return false;

    m_bus->writeByte(m_addr, 0x0B, 0x00);

    resetDetectionState();
    return true;
}

int VL53L0X::readDistanceMm() const {
    m_bus->writeByte(m_addr, 0x80, 0x01);
    m_bus->writeByte(m_addr, 0xFF, 0x01);
    m_bus->writeByte(m_addr, 0x00, 0x00);
    m_bus->writeByte(m_addr, 0x91, m_stop_variable);
    m_bus->writeByte(m_addr, 0x00, 0x01);
    m_bus->writeByte(m_addr, 0xFF, 0x00);
    m_bus->writeByte(m_addr, 0x80, 0x00);

    m_bus->writeByte(m_addr, 0x00, 0x01);

    uint8_t val = 0;
    int timeout = 50;
    while (timeout--) {
        m_bus->readByte(m_addr, 0x14, val);
        if (val & 0x07) break;
        usleep(1000);
    }
    if (timeout <= 0) {
        return -1;
    }

    uint8_t buf[2];
    if (!m_bus->readBytes(m_addr, 0x1E, buf, 2)) {
        return -1;
    }

    m_bus->writeByte(m_addr, 0x0B, 0x01);

    const uint16_t dist = (static_cast<uint16_t>(buf[0]) << 8) | buf[1];
    if (dist >= 8000 || dist <= 20) { // Ниже 20 мм у VL53L0X слепая зона и мусор
        return -1;
    }

    return static_cast<int>(dist);
}

bool VL53L0X::isObjectDetected(const int threshold_mm, 
                              const int hold_duration_ms, 
                              const int max_jitter_mm) {
    const int current_dist = readDistanceMm();
    const auto now = std::chrono::steady_clock::now();

    if (const bool in_range = (current_dist > 0 && current_dist <= threshold_mm); !in_range) {
        // Допускаем до 2 единичных промахов лазера (dropped frames), чтобы не сбрасывать таймер из-за блика
        if (m_tracking && ++m_consecutive_drops <= 2) {
            return false;
        }
        resetDetectionState();
        return false;
    }

    m_consecutive_drops = 0;

    if (!m_tracking) {
        m_tracking = true;
        m_first_seen_time = now;
        m_last_stable_dist = current_dist;
        return false;
    }

    // 3. Проверка на "бешенство" дистанции (Jitter check)
    if (std::abs(current_dist - m_last_stable_dist) > max_jitter_mm) {
        // Перезапускаем отсчет от новой дистанции
        m_first_seen_time = now;
        m_last_stable_dist = current_dist;
        return false;
    }

    // Сглаживаем базовую дистанцию (экспоненциальное скользящее среднее для компенсации мелкого шума)
    m_last_stable_dist = (m_last_stable_dist * 3 + current_dist) / 4;

    // 4. Проверяем, прошло ли нужное время непрерывного удержания
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_first_seen_time).count();
    return (elapsed >= hold_duration_ms);
}