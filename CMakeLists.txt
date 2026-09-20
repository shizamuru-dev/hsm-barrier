cmake_minimum_required(VERSION 3.22.1)
add_compile_options(-O3 -march=armv8-a)
project(hsm-barrier)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ASCEND_PATH /usr/local/Ascend/ascend-toolkit/latest)

# RPATH для запуска бинарника на плате
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "${ASCEND_PATH}/lib64:${ASCEND_PATH}/compiler/lib64")

find_package(OpenCV REQUIRED)

# Проверяем наличие локальных заголовков для Clangd/CLion
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/dev/deps/ascend_include")
    set(ASCEND_INC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dev/deps/ascend_include")
else()
    set(ASCEND_INC_DIR "${ASCEND_PATH}/include")
endif()

add_executable(barrier
        src/main.cpp

        # Hardware
        src/hardware/i2c_bus.cpp
        include/hardware/i2c_bus.h
        src/hardware/pca9685.cpp
        include/hardware/pca9685.h
        src/hardware/servo.cpp
        include/hardware/servo.h
        src/hardware/vl53l0x.cpp
        include/hardware/vl53l0x.h

        # NPU
        src/npu/npu_model.cpp
        include/npu/npu_model.h
        src/npu/yolo_postprocess.cpp
        include/npu/yolo_postprocess.h
        src/hardware/camera_stream.cpp
        include/hardware/camera_stream.h
        src/network/stream_server.cpp
        include/network/stream_server.h
        src/npu/patchcore_engine.cpp
        include/npu/patchcore_engine.h
        include/npu/patchcore_engine.h

        # Network

)

target_include_directories(barrier PRIVATE
        include
        ${OpenCV_INCLUDE_DIRS}
        ${ASCEND_INC_DIR}
        ${ASCEND_PATH}/include
)

target_link_directories(barrier PRIVATE
        ${ASCEND_PATH}/lib64
        ${ASCEND_PATH}/compiler/lib64
)

target_link_libraries(barrier PRIVATE
        ${OpenCV_LIBS}
        ascendcl
        pthread
)