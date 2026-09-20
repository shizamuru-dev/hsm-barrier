//
// Created by cyn on 9/20/26.
//

#include "../../include/npu/npu_model.h"
#include <iostream>

NpuModel::NpuModel(const int32_t deviceId) : m_deviceId(deviceId) {
    aclError ret = aclInit(nullptr);
    if (ret != ACL_SUCCESS && ret != ACL_ERROR_REPEAT_INITIALIZE) {
        std::cerr << "[NPU] Ошибка aclInit: " << ret << "\n";
    }

    ret = aclrtSetDevice(m_deviceId);
    if (ret != ACL_SUCCESS) {
        std::cerr << "[NPU] Ошибка aclrtSetDevice(" << m_deviceId << "): " << ret << "\n";
    }
}

NpuModel::~NpuModel() {
    unload();
    aclrtResetDevice(m_deviceId);
    aclFinalize();
}

bool NpuModel::load(const std::string& modelPath) {
    if (m_loaded) {
        unload();
    }

    // 1. Загрузка .om модели в память NPU
    aclError ret = aclmdlLoadFromFile(modelPath.c_str(), &m_modelId);
    if (ret != ACL_SUCCESS) {
        std::cerr << "[NPU] Ошибка загрузки .om файла " << modelPath << ": " << ret << "\n";
        return false;
    }

    // 2. Получение описания тензоров модели
    m_modelDesc = aclmdlCreateDesc();
    if (aclmdlGetDesc(m_modelDesc, m_modelId) != ACL_SUCCESS) {
        std::cerr << "[NPU] Не удалось прочитать дескриптор модели\n";
        unload();
        return false;
    }

    // 3. Аллокация буфера входа (Device Memory)
    m_inputSize = aclmdlGetInputSizeByIndex(m_modelDesc, 0);
    ret = aclrtMalloc(&m_inputDevPtr, m_inputSize, ACL_MEM_MALLOC_HUGE_FIRST);
    if (ret != ACL_SUCCESS) {
        std::cerr << "[NPU] Ошибка aclrtMalloc для входа\n";
        unload();
        return false;
    }

    m_inputDataset = aclmdlCreateDataset();
    aclDataBuffer* inDataBuf = aclCreateDataBuffer(m_inputDevPtr, m_inputSize);
    aclmdlAddDatasetBuffer(m_inputDataset, inDataBuf);

    // 4. Аллокация буфера выхода (Device Memory)
    m_outputSize = aclmdlGetOutputSizeByIndex(m_modelDesc, 0);
    ret = aclrtMalloc(&m_outputDevPtr, m_outputSize, ACL_MEM_MALLOC_HUGE_FIRST);
    if (ret != ACL_SUCCESS) {
        std::cerr << "[NPU] Ошибка aclrtMalloc для выхода\n";
        unload();
        return false;
    }

    m_outputDataset = aclmdlCreateDataset();
    aclDataBuffer* outDataBuf = aclCreateDataBuffer(m_outputDevPtr, m_outputSize);
    aclmdlAddDatasetBuffer(m_outputDataset, outDataBuf);

    m_loaded = true;
    std::cout << "[NPU] Модель успешно загружена: " << modelPath
              << " (In: " << m_inputSize << "B, Out: " << m_outputSize << "B)\n";
    return true;
}

void NpuModel::unload() {
    if (!m_loaded) return;

    if (m_inputDevPtr) {
        aclrtFree(m_inputDevPtr);
        m_inputDevPtr = nullptr;
    }
    if (m_outputDevPtr) {
        aclrtFree(m_outputDevPtr);
        m_outputDevPtr = nullptr;
    }
    if (m_inputDataset) {
        aclDestroyDataBuffer(aclmdlGetDatasetBuffer(m_inputDataset, 0));
        aclmdlDestroyDataset(m_inputDataset);
        m_inputDataset = nullptr;
    }
    if (m_outputDataset) {
        aclDestroyDataBuffer(aclmdlGetDatasetBuffer(m_outputDataset, 0));
        aclmdlDestroyDataset(m_outputDataset);
        m_outputDataset = nullptr;
    }
    if (m_modelDesc) {
        aclmdlDestroyDesc(m_modelDesc);
        m_modelDesc = nullptr;
    }

    aclmdlUnload(m_modelId);
    m_loaded = false;
}

bool NpuModel::execute(const std::vector<float>& inputTensor, std::vector<float>& outputTensor) const {
    if (!m_loaded) return false;

    // Host -> Device
    aclError ret = aclrtMemcpy(m_inputDevPtr, m_inputSize, inputTensor.data(),
                               inputTensor.size() * sizeof(float), ACL_MEMCPY_HOST_TO_DEVICE);
    if (ret != ACL_SUCCESS) {
        std::cerr << "[NPU] Ошибка копирования Host->Device: " << ret << "\n";
        return false;
    }

    // Запуск инференса на чипе
    ret = aclmdlExecute(m_modelId, m_inputDataset, m_outputDataset);
    if (ret != ACL_SUCCESS) {
        std::cerr << "[NPU] Ошибка вызова aclmdlExecute: " << ret << "\n";
        return false;
    }

    // Device -> Host
    outputTensor.resize(m_outputSize / sizeof(float));
    ret = aclrtMemcpy(outputTensor.data(), m_outputSize, m_outputDevPtr,
                      m_outputSize, ACL_MEMCPY_DEVICE_TO_HOST);
    if (ret != ACL_SUCCESS) {
        std::cerr << "[NPU] Ошибка копирования Device->Host: " << ret << "\n";
        return false;
    }

    return true;
}
