#pragma once

#include <string>
#include <mutex>
#include <cstdint>

class I2CBus {
public:
    explicit I2CBus(std::string  dev_path);
    ~I2CBus();

    I2CBus(const I2CBus&) = delete;
    I2CBus& operator=(const I2CBus&) = delete;

    // Потокобезопасная запись байта в регистр
    bool writeByte(uint8_t dev_addr, uint8_t reg, uint8_t val);

    // Потокобезопасное чтение байта из регистра
    bool readByte(uint8_t dev_addr, uint8_t reg, uint8_t& val);

    // Потокобезопасная запись буфера
    bool writeBytes(uint8_t dev_addr, uint8_t reg, const uint8_t* data, size_t length);

    // Потокобезопасное чтение буфера
    bool readBytes(uint8_t dev_addr, uint8_t reg, uint8_t* data, size_t length);

private:
    std::string m_dev_path;
    int m_fd;
    std::mutex m_mutex;

    [[nodiscard]] bool selectSlave(uint8_t dev_addr) const;
};