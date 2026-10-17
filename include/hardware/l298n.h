#pragma once

#include "pca9685.h"
#include <cstdint>
#include <memory>

// L298N используется как четырехканальный драйвер светодиодов.
// Все управляющие сигналы IN1..IN4, ENA и ENB формируются PCA9685.
class L298N {
public:
    L298N(std::shared_ptr<PCA9685> pca, uint8_t in1_channel,
          uint8_t in2_channel, uint8_t in3_channel, uint8_t in4_channel,
          uint8_t ena_channel, uint8_t enb_channel);
    ~L298N() = default;

    L298N(const L298N&) = delete;
    L298N& operator=(const L298N&) = delete;

    bool init();
    void setIN1(bool state);
    void setIN2(bool state);
    void setIN3(bool state);
    void setIN4(bool state);
    void allOff();
    void enableA(bool state);
    void enableB(bool state);

private:
    void setChannel(uint8_t channel, bool state);

    std::shared_ptr<PCA9685> m_pca;
    uint8_t m_in1_channel, m_in2_channel, m_in3_channel, m_in4_channel;
    uint8_t m_ena_channel, m_enb_channel;
};
