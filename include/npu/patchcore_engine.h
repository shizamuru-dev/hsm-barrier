#pragma once

#include <vector>
#include <string>
#include <opencv2/opencv.hpp>
#include "acl/acl.h"

struct AnomalyResult {
    bool isAnomaly{false};
    float anomalyScore{0.0f};
};

class PatchcoreEngine {
public:
    explicit PatchcoreEngine(int32_t deviceId = 0);
    ~PatchcoreEngine();

    PatchcoreEngine(const PatchcoreEngine&) = delete;
    PatchcoreEngine& operator=(const PatchcoreEngine&) = delete;

    bool load(const std::string& modelPath);
    void unload();

    AnomalyResult detect(const cv::Mat& rawFrame, float threshold = 0.40f) const;

private:
    int32_t m_deviceId;
    uint32_t m_modelId{0};
    bool m_isLoaded{false};

    aclmdlDesc* m_modelDesc{nullptr};
    aclmdlDataset* m_inputDataset{nullptr};
    aclmdlDataset* m_outputDataset{nullptr};

    void* m_inputDevPtr{nullptr};
    size_t m_inputBufferSize{0};

    // Динамический список под все выходы (у нас их 4 штуки)
    std::vector<void*> m_outputDevPtrs;
    std::vector<size_t> m_outputBufferSizes;

    static constexpr int INPUT_W = 256;
    static constexpr int INPUT_H = 256;
};