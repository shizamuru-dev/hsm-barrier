//
// Created by cyn on 9/19/26.
//

#include "../../include/hardware/i2c_bus.h"

#include <stdexcept>
#include <utility>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

I2CBus::I2CBus(std::string  dev_path) : m_dev_path(std::move(dev_path)), m_fd(-1) {
    m_fd = ::open(m_dev_path.c_str(), O_RDWR);
    if (m_fd < 0) {
        throw std::runtime_error("Не удалось открыть I2C устройство: " + m_dev_path);
    }
}

I2CBus::~I2CBus() {
    if (m_fd >= 0) {
        ::close(m_fd);
    }
}

bool I2CBus::selectSlave(const uint8_t dev_addr) const {
    if (ioctl(m_fd, I2C_SLAVE, dev_addr) < 0) {
        return false;
    }
    return true;
}

bool I2CBus::writeByte(const uint8_t dev_addr, const uint8_t reg, const uint8_t val) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!selectSlave(dev_addr)) return false;

    const uint8_t buf[2] = {reg, val};
    return (::write(m_fd, buf, 2) == 2);
}

bool I2CBus::readByte(const uint8_t dev_addr, const uint8_t reg, uint8_t& val) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!selectSlave(dev_addr)) return false;

    if (::write(m_fd, &reg, 1) != 1) return false;
    return (::read(m_fd, &val, 1) == 1);
}

bool I2CBus::writeBytes(const uint8_t dev_addr, const uint8_t reg, const uint8_t* data, const size_t length) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!selectSlave(dev_addr)) return false;

    std::vector<uint8_t> buf(length + 1);
    buf[0] = reg;
    for (size_t i = 0; i < length; ++i) {
        buf[i + 1] = data[i];
    }
    return (::write(m_fd, buf.data(), buf.size()) == static_cast<ssize_t>(buf.size()));
}

bool I2CBus::readBytes(const uint8_t dev_addr, const uint8_t reg, uint8_t* data, const size_t length) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!selectSlave(dev_addr)) return false;

    if (::write(m_fd, &reg, 1) != 1) return false;
    return (::read(m_fd, data, length) == static_cast<ssize_t>(length));
}