#pragma once

#include <vector>
#include <opencv2/opencv.hpp>

struct Detection {
    int classId;
    float confidence;
    cv::Rect box;
};

// Преобразование cv::Mat в NCHW тензор 1x3xHxW
std::vector<float> preprocessToNCHW(const cv::Mat& src, int targetW, int targetH);
std::vector<float> fastPreprocessToNCHW(const cv::Mat& src, int targetW, int targetH);

// Постпроцессинг сырого вывода YOLOv8
std::vector<Detection> postprocessYOLOv8(
    const std::vector<float>& rawOutput,
    int originalW,
    int originalH,
    float confThresh = 0.45f,
    float nmsThresh = 0.5f,
    int modelInputW = 640,
    int modelInputH = 640
);