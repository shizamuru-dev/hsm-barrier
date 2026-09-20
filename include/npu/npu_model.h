#pragma once

#include <string>
#include <vector>
#include "acl/acl.h"

class NpuModel {
public:
    explicit NpuModel(int32_t deviceId = 0);
    virtual ~NpuModel();

    NpuModel(const NpuModel&) = delete;
    NpuModel& operator=(const NpuModel&) = delete;

    bool load(const std::string& modelPath);
    void unload();

    [[nodiscard]] bool isLoaded() const { return m_loaded; }
    
    // Запуск выполнения графа на ядрах DaVinci
    bool execute(const std::vector<float>& inputTensor, std::vector<float>& outputTensor) const;

    [[nodiscard]] size_t getInputSize() const { return m_inputSize; }
    [[nodiscard]] size_t getOutputSize() const { return m_outputSize; }

private:
    int32_t m_deviceId;
    uint32_t m_modelId{0};
    bool m_loaded{false};

    aclmdlDesc* m_modelDesc{nullptr};
    aclmdlDataset* m_inputDataset{nullptr};
    aclmdlDataset* m_outputDataset{nullptr};

    void* m_inputDevPtr{nullptr};
    void* m_outputDevPtr{nullptr};
    size_t m_inputSize{0};
    size_t m_outputSize{0};
};