#!/usr/bin/env bash
set -e

# Цвета для красивого вывода
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${BLUE}=========================================${NC}"
echo -e "${BLUE}   HSM Barrier: Native ARM64 Build       ${NC}"
echo -e "${BLUE}=========================================${NC}"

# 1. Корневой каталог проекта (на уровень выше от папки scripts)
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"

# 2. Инициализация окружения Huawei Ascend CANN
ASCEND_TOOLKIT_DIR="/usr/local/Ascend/ascend-toolkit/latest"
if [ -d "$ASCEND_TOOLKIT_DIR" ]; then
    echo -e "${GREEN}[ENV] Подключение окружения Huawei CANN...${NC}"
    if [ -f "${ASCEND_TOOLKIT_DIR}/bin/set_env.sh" ]; then
        source "${ASCEND_TOOLKIT_DIR}/bin/set_env.sh"
    fi
    export ASCEND_HOME_PATH="${ASCEND_TOOLKIT_DIR}"
    export LD_LIBRARY_PATH="${ASCEND_TOOLKIT_DIR}/lib64:${ASCEND_TOOLKIT_DIR}/compiler/lib64:${LD_LIBRARY_PATH}"
else
    echo -e "${RED}[WARN] Ascend Toolkit не найден в ${ASCEND_TOOLKIT_DIR}! Проверь пути установки.${NC}"
fi

# 3. Подготовка сборочной папки
echo -e "${GREEN}[BUILD] Подготовка каталога сборки: ${BUILD_DIR}${NC}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# 4. Конфигурация CMake под Release c оптимизацией
echo -e "${GREEN}[CMAKE] Конфигурация проекта в режиме Release...${NC}"
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_FLAGS="-O3 -march=armv8-a -ffast-math" \
      "${PROJECT_ROOT}"

# 5. Параллельная компиляция
NUM_JOBS=$(nproc 2>/dev/null || echo 4)
echo -e "${GREEN}[MAKE] Сборка на ${NUM_JOBS} потоках...${NC}"
make -j"${NUM_JOBS}"

echo -e "${BLUE}=========================================${NC}"
echo -e "${GREEN}✅ Сборка успешно завершена!${NC}"
echo -e "Исполняемый файл: ${BUILD_DIR}/barrier"
echo -e "Запуск: cd ${BUILD_DIR} && ./barrier"
echo -e "${BLUE}=========================================${NC}"