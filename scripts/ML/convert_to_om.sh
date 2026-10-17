#!/usr/bin/env bash
set -e

source /usr/local/Ascend/ascend-toolkit/set_env.sh

export TE_PARALLEL_COMPILER=1
export TBE_PARALLEL_COMPILER=1
MODELS_DIR="../../models"

echo "[1/2] Converting YOLOv8n to .om..."
#atc --model="${MODELS_DIR}/yolov8n.onnx" \
#    --framework=5 \
#    --output="${MODELS_DIR}/yolov8n" \
#    --soc_version=Ascend310B4 \
#    --input_format=NCHW \
#    --input_shape="images:1,3,640,640" \
#    --log=error

echo "[2/2] Converting PatchCore to .om..."
atc --model="${MODELS_DIR}/patchcore.onnx" \
    --framework=5 \
    --output="${MODELS_DIR}/patchcore" \
    --soc_version=Ascend310B4 \
    --input_format=NCHW \
    --input_shape="input:1,3,256,256" \
    --log=error

echo "Artifacts compiled successfully into ${MODELS_DIR}/"