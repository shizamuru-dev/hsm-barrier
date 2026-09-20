# ⚙️ Инструкция по компиляции нейросетевых моделей в формат .om (Ascend ATC)

Данный документ описывает процесс компиляции оптимизированных оффлайн-моделей `.om` (Ascend Offline Model) из формата ONNX для их последующего аппаратного инференса на NPU **Huawei Ascend 310B4** платы **Orange Pi AI Pro** под управлением **CANN 8.0.0+**.

---

## 📋 Предварительные требования

1. **Целевая платформа:** Orange Pi AI Pro (SoC Ascend 310B4, архитектура ARM64/aarch64).
2. **Установленный тулкит:** Huawei CANN Toolkit (`/usr/local/Ascend/ascend-toolkit/latest`).
3. **Файлы моделей в формате ONNX:**
   * `models/yolov8n.onnx` — верхний ярус контроля (детекция силуэтов/людей/объектов, вход: `1x3x640x640`).
   * `models/patchcore.onnx` — нижний ярус контроля (One-Class сегментация аномалий подвагонного пространства, вход: `1x3x256x256`).

---

## ⚠️ Критически важная настройка памяти (Защита от OOM Killer)

Компилятор ATC при вызове движка генерации операторов TBE (*Tensor Boost Engine*) активно задействует многопоточную компиляцию на Python. На объединенной памяти платы (8/16 ГБ RAM+NPU) пиковое потребление памяти процессом `atc.bin` может превысить лимит, вызвав принудительное завершение процесса ядром Linux (`Killed` / SIGKILL).

Перед началом компиляции **обязательно** выполните следующие действия:

### 1. Подключение файла подкачки (Swap) на 8 ГБ
```bash
# Создание swap-файла при его отсутствии
sudo fallocate -l 8G /swapfile || sudo dd if=/dev/zero of=/swapfile bs=1M count=8192
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile

# Проверка активации swap
free -h

```

### 2. Инициализация окружения CANN и ограничение параллелизма

```bash
# Подгрузка библиотек и бинарников CANN
source /usr/local/Ascend/ascend-toolkit/set_env.sh

# Принудительное ограничение TBE до 1-2 параллельных воркеров (предотвращает утечку памяти)
export TE_PARALLEL_COMPILER=1
export TBE_PARALLEL_COMPILER=1

```

---

## 🚀 Команды компиляции моделей (ATC)

Выполняйте команды, находясь в корневой директории проекта `hsm-barrier`:

### 1. Компиляция YOLOv8n (`yolov8n.om`)

Модель преобразуется под статический размер батча `1x3x640x640` в формате тензора `NCHW`.

```bash
atc --model=models/yolov8n.onnx \
    --framework=5 \
    --output=models/yolov8n \
    --soc_version=Ascend310B4 \
    --input_format=NCHW \
    --input_shape="images:1,3,640,640" \
    --log=error

```

* **`--framework=5`**: Указывает исходный формат ONNX.
* **`--output=models/yolov8n`**: ATC автоматически сформирует файл `models/yolov8n.om`.
* **`--soc_version=Ascend310B4`**: Целевой чип DaVinci на Orange Pi AI Pro.

### 2. Компиляция PatchCore (`patchcore.om`)

Экстрактор признаков и банк памяти аномалий со статическим входом `1x3x256x256`.

```bash
atc --model=models/patchcore.onnx \
    --framework=5 \
    --output=models/patchcore \
    --soc_version=Ascend310B4 \
    --input_format=NCHW \
    --input_shape="input:1,3,256,256" \
    --log=error

```

---

## ⚡ Автоматическая компиляция через скрипт

Все описанные выше шаги и переменные окружения уже зашиты в готовый скрипт автоматизации:

```bash
chmod +x scripts/ML/convert_to_om.sh
./scripts/ML/convert_to_om.sh

```

---

## ✅ Проверка результата

После успешного выполнения утилиты ATC в директории `models/` должны появиться скомпилированные артефакты:

```text
models/
├── yolov8n.om     # ~12-15 МБ
└── patchcore.om   # ~45-60 МБ

```

При запуске бинарника `./build/barrier` движок AscendCL подгрузит данные бинарники напрямую в специализированную память NPU (`ACL_MEM_MALLOC_HUGE_FIRST`).
