//
// Created by cyn on 9/20/26.
//

#include "../include/npu/patchcore_engine.h"
#include <iostream>

PatchcoreEngine::PatchcoreEngine(const int32_t deviceId) : m_deviceId(deviceId) {
    aclrtSetDevice(m_deviceId);
}

PatchcoreEngine::~PatchcoreEngine() {
    unload();
}

bool PatchcoreEngine::load(const std::string& modelPath) {
    if (m_isLoaded) unload();

    aclrtSetDevice(m_deviceId);

    if (const aclError ret = aclmdlLoadFromFile(modelPath.c_str(), &m_modelId); ret != ACL_SUCCESS) {
        std::cerr << "[PatchCore NPU] Ошибка загрузки .om файла: " << ret << "\n";
        return false;
    }

    m_modelDesc = aclmdlCreateDesc();
    aclmdlGetDesc(m_modelDesc, m_modelId);

    // 1. Память под ВХОД
    m_inputBufferSize = aclmdlGetInputSizeByIndex(m_modelDesc, 0);
    aclrtMalloc(&m_inputDevPtr, m_inputBufferSize, ACL_MEM_MALLOC_HUGE_FIRST);
    aclDataBuffer* inBuffer = aclCreateDataBuffer(m_inputDevPtr, m_inputBufferSize);
    m_inputDataset = aclmdlCreateDataset();
    aclmdlAddDatasetBuffer(m_inputDataset, inBuffer);

    // 2. Память под ВСЕ ВЫХОДЫ (цикл по всем аутпутам графа)
    const size_t numOutputs = aclmdlGetNumOutputs(m_modelDesc);
    m_outputDataset = aclmdlCreateDataset();
    m_outputDevPtrs.resize(numOutputs);
    m_outputBufferSizes.resize(numOutputs);

    for (size_t i = 0; i < numOutputs; ++i) {
        m_outputBufferSizes[i] = aclmdlGetOutputSizeByIndex(m_modelDesc, i);
        aclrtMalloc(&m_outputDevPtrs[i], m_outputBufferSizes[i], ACL_MEM_MALLOC_HUGE_FIRST);
        aclDataBuffer* outBuf = aclCreateDataBuffer(m_outputDevPtrs[i], m_outputBufferSizes[i]);
        aclmdlAddDatasetBuffer(m_outputDataset, outBuf);
    }

    m_isLoaded = true;
    std::cout << "[PatchCore NPU] Модель успешно сконфигурирована (" << numOutputs << " выходов привязано)\n";
    return true;
}

void PatchcoreEngine::unload() {
    if (!m_isLoaded) return;

    if (m_inputDevPtr) {
        aclrtFree(m_inputDevPtr);
        m_inputDevPtr = nullptr;
    }

    // Очистка всех буферов выхода
    for (void* ptr : m_outputDevPtrs) {
        if (ptr) aclrtFree(ptr);
    }
    m_outputDevPtrs.clear();
    m_outputBufferSizes.clear();

    if (m_inputDataset) {
        aclDestroyDataBuffer(aclmdlGetDatasetBuffer(m_inputDataset, 0));
        aclmdlDestroyDataset(m_inputDataset);
        m_inputDataset = nullptr;
    }

    if (m_outputDataset) {
        const size_t numOutputs = aclmdlGetDatasetNumBuffers(m_outputDataset);
        for (size_t i = 0; i < numOutputs; ++i) {
            aclDestroyDataBuffer(aclmdlGetDatasetBuffer(m_outputDataset, i));
        }
        aclmdlDestroyDataset(m_outputDataset);
        m_outputDataset = nullptr;
    }

    if (m_modelDesc) {
        aclmdlDestroyDesc(m_modelDesc);
        m_modelDesc = nullptr;
    }

    aclmdlUnload(m_modelId);
    m_isLoaded = false;
}

AnomalyResult PatchcoreEngine::detect(const cv::Mat& rawFrame, const float threshold) const {
    if (!m_isLoaded || rawFrame.empty()) return {};

    // 1. Препроцессинг под 256x256
    cv::Mat resized;
    if (rawFrame.cols != INPUT_W || rawFrame.rows != INPUT_H) {
        cv::resize(rawFrame, resized, cv::Size(INPUT_W, INPUT_H), 0, 0, cv::INTER_LINEAR);
    } else {
        resized = rawFrame;
    }

    constexpr size_t plane = INPUT_W * INPUT_H;
    std::vector<float> inputTensor(3 * plane);

    float* __restrict ptrR = inputTensor.data();
    float* __restrict ptrG = ptrR + plane;
    float* __restrict ptrB = ptrG + plane;

    const uint8_t* __restrict data = resized.data;
    constexpr float inv255 = 1.0f / 255.0f;

    #pragma GCC unroll 4
    for (size_t i = 0; i < plane; ++i) {
        const size_t idx = i * 3;
        ptrB[i] = static_cast<float>(data[idx + 0]) * inv255;
        ptrG[i] = static_cast<float>(data[idx + 1]) * inv255;
        ptrR[i] = static_cast<float>(data[idx + 2]) * inv255;
    }

    aclrtSetDevice(m_deviceId);

    // 2. Копируем вход на NPU
    aclrtMemcpy(m_inputDevPtr, m_inputBufferSize, inputTensor.data(), m_inputBufferSize, ACL_MEMCPY_HOST_TO_DEVICE);

    // 3. Выполнение графа
    if (const aclError ret = aclmdlExecute(m_modelId, m_inputDataset, m_outputDataset); ret != ACL_SUCCESS) {
        std::cerr << "[PatchCore NPU] Ошибка инференса: " << ret << "\n";
        return {};
    }

    // 4. Читаем Output 0 (4 байта = float скор аномалии)
    float anomalyScore = 0.0f;
    aclrtMemcpy(&anomalyScore, sizeof(float), m_outputDevPtrs[0], sizeof(float), ACL_MEMCPY_DEVICE_TO_HOST);

    return {
        .isAnomaly = (anomalyScore >= threshold),
        .anomalyScore = anomalyScore
    };
}
