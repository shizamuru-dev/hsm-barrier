//
// Created by cyn on 9/19/26.
//

#include "../../include/hardware/servo.h"

Servo::Servo(std::shared_ptr<PCA9685> pca, uint8_t channel)
    : m_pca(std::move(pca)), m_channel(channel) {}

bool Servo::setAngle(int angle) const {
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;

    uint16_t off = MIN_TICKS + static_cast<uint16_t>(((MAX_TICKS - MIN_TICKS) * angle) / 180);
    return m_pca->setPwm(m_channel, 0, off);
}

bool Servo::open() const {
    return setAngle(90);
}

bool Servo::close() const {
    return setAngle(0);
}

bool Servo::detach() const {
    return m_pca->setFullOff(m_channel);
}
