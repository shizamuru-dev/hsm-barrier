#include "../../include/hardware/l298n.h"
#include <iostream>

L298N::L298N(std::shared_ptr<PCA9685> pca, uint8_t in1_channel,
             uint8_t in2_channel, uint8_t in3_channel, uint8_t in4_channel,
             uint8_t ena_channel, uint8_t enb_channel)
    : m_pca(std::move(pca)), m_in1_channel(in1_channel), m_in2_channel(in2_channel),
      m_in3_channel(in3_channel), m_in4_channel(in4_channel),
      m_ena_channel(ena_channel), m_enb_channel(enb_channel) {}

bool L298N::init() {
    std::cout << "[L298N] Инициализация через PCA9685...\n";
    if (!m_pca) return false;
    allOff();
    std::cout << "[L298N] Драйвер инициализирован успешно\n";
    return true;
}

void L298N::setChannel(uint8_t channel, bool state) {
    if (state) m_pca->setPwm(channel, 0, 4095);
    else m_pca->setFullOff(channel);
}

void L298N::setIN1(bool state) { setChannel(m_in1_channel, state); }
void L298N::setIN2(bool state) { setChannel(m_in2_channel, state); }
void L298N::setIN3(bool state) { setChannel(m_in3_channel, state); }
void L298N::setIN4(bool state) { setChannel(m_in4_channel, state); }

void L298N::allOff() {
    setIN1(false); setIN2(false); setIN3(false); setIN4(false);
    enableA(false); enableB(false);
}

void L298N::enableA(bool state) { setChannel(m_ena_channel, state); }
void L298N::enableB(bool state) { setChannel(m_enb_channel, state); }
