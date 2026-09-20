//
// Created by cyn on 9/20/26.
//

#include "../include/npu/yolo_postprocess.h"

std::vector<float> preprocessToNCHW(const cv::Mat& src, const int targetW, const int targetH) {
    const size_t plane = static_cast<size_t>(targetW) * static_cast<size_t>(targetH);
    std::vector<float> nchwTensor(3 * plane);

    cv::Mat resized;
    // Быстрый ресайз без тяжелой билинейной интерполяции
    if (src.cols != targetW || src.rows != targetH) {
        cv::resize(src, resized, cv::Size(targetW, targetH), 0, 0, cv::INTER_NEAREST);
    } else {
        resized = src;
    }

    float* ptrR = nchwTensor.data();
    float* ptrG = ptrR + plane;
    float* ptrB = ptrG + plane;

    const uint8_t* data = resized.data;
    constexpr float inv255 = 1.0f / 255.0f;

    // В один проход: BGR -> RGB + / 255.0f + NCHW layout
    for (size_t i = 0; i < plane; ++i) {
        const size_t idx = i * 3;
        ptrB[i] = static_cast<float>(data[idx + 0]) * inv255;
        ptrG[i] = static_cast<float>(data[idx + 1]) * inv255;
        ptrR[i] = static_cast<float>(data[idx + 2]) * inv255;
    }

    return nchwTensor;
}

std::vector<float> fastPreprocessToNCHW(const cv::Mat& src, const int targetW, const int targetH) {
    const size_t plane = static_cast<size_t>(targetW) * static_cast<size_t>(targetH);
    std::vector<float> nchwTensor(3 * plane);

    cv::Mat resized;
    // Билинейная интерполяция возвращает четкую, не мыльную картинку для детектора
    if (src.cols != targetW || src.rows != targetH) {
        cv::resize(src, resized, cv::Size(targetW, targetH), 0, 0, cv::INTER_LINEAR);
    } else {
        resized = src;
    }

    float* __restrict ptrR = nchwTensor.data();
    float* __restrict ptrG = ptrR + plane;
    float* __restrict ptrB = ptrG + plane;

    const uint8_t* __restrict data = resized.data;
    constexpr float inv255 = 1.0f / 255.0f;

    // Прямой последовательный проход без аллокаций clone()
    #pragma GCC unroll 4
    for (size_t i = 0; i < plane; ++i) {
        const size_t idx = i * 3;
        ptrB[i] = static_cast<float>(data[idx + 0]) * inv255;
        ptrG[i] = static_cast<float>(data[idx + 1]) * inv255;
        ptrR[i] = static_cast<float>(data[idx + 2]) * inv255;
    }

    return nchwTensor;
}

std::vector<Detection> postprocessYOLOv8(
    const std::vector<float>& rawOutput,
    const int originalW,
    const int originalH,
    const float confThresh,
    const float nmsThresh,
    const int modelInputW,
    const int modelInputH
) {
    std::vector<Detection> results;
    constexpr size_t numAnchors = 8400;  // Сетка для разрешения 640x640

    if (constexpr size_t numChannels = 84; rawOutput.size() < (numChannels * numAnchors)) {
        return results;
    }

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> classIds;

    const float scaleX = static_cast<float>(originalW) / static_cast<float>(modelInputW);
    const float scaleY = static_cast<float>(originalH) / static_cast<float>(modelInputH);

    for (size_t i = 0; i < numAnchors; ++i) {
        float maxScore = 0.0f;
        int bestClass = -1;

        for (size_t c = 0; c < 80; ++c) {
            if (const float score = rawOutput[(4 + c) * numAnchors + i]; score > maxScore) {
                maxScore = score;
                bestClass = static_cast<int>(c);
            }
        }

        if (maxScore >= confThresh) {
            const float cx = rawOutput[0 * numAnchors + i] * scaleX;
            const float cy = rawOutput[1 * numAnchors + i] * scaleY;
            const float w  = rawOutput[2 * numAnchors + i] * scaleX;
            const float h  = rawOutput[3 * numAnchors + i] * scaleY;

            const int left = static_cast<int>(cx - (w * 0.5f));
            const int top  = static_cast<int>(cy - (h * 0.5f));

            boxes.emplace_back(left, top, static_cast<int>(w), static_cast<int>(h));
            confidences.push_back(maxScore);
            classIds.push_back(bestClass);
        }
    }

    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, confidences, confThresh, nmsThresh, indices);

    results.reserve(indices.size());
    for (const int rawIdx : indices) {
        if (rawIdx >= 0) {
            const auto idx = static_cast<size_t>(rawIdx);
            results.push_back({
                .classId = classIds[idx],
                .confidence = confidences[idx],
                .box = boxes[idx]
            });
        }
    }

    return results;
}