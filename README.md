<div align="center">

# 🚄 hsm-barrier
### High-Speed Rail Edge-AI Inspection Barrier System

**Автоматизированный программно-аппаратный рубеж предэксплуатационного постового контроля высокоскоростного подвижного состава (ВСМ)**

[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-blue.svg?style=flat&logo=c%2B%2B)](https://en.cppreference.com/w/cpp/20)
[![Target Platform](https://img.shields.io/badge/Target-Orange%20Pi%20AI%20Pro%20(Ascend%20310B)-orange.svg)](http://www.orangepi.org/)
[![CANN Version](https://img.shields.io/badge/Huawei%20CANN-8.0.0-red.svg)](https://www.hiascend.com/)
[![OpenCV](https://img.shields.io/badge/OpenCV-4.5%2B-green.svg?logo=opencv)](https://opencv.org/)
[![License](https://img.shields.io/badge/License-Apache%202.0-lightgrey.svg)](LICENSE)

[Обзор](#-обзор-проекта) • [Архитектура](#-архитектура-и-пайплайн) • [Спецификации](#-технические-характеристики) • [Сборка](#-сборка-и-запуск) • [Аппаратный-комплекс](#-аппаратный-комплекс)

</div>

---

## 📌 Обзор проекта

**hsm-barrier** — аппаратно-программный комплекс превентивного постового досмотра поездов высокоскоростных магистралей (ВСМ).

Комплекс предназначен для развертывания на рубежах входного контроля (горловины станций, депо и пункты технического обслуживания, скоростной режим 30–60 км/ч) за 250-500 метров до скоростных перегонов.

### Решаемые задачи ВСМ:
* **Исключение человеческого фактора:** автоматический мультимодальный досмотр состава вместо длительного ручного обхода.
* **Минимизация простоев и «окон»:** экспресс-анализ состава в движении с временем инференса единицы миллисекунд.
* **Превентивная безопасность на скоростях 300–400 км/ч:** предотвращение срыва токоприемников, обрыва контактной сети 25 кВ и схода вагонов из-за незакрепленного подвагонного оборудования или посторонних объектов.

### Функциональные возможности:
1. **Верхний контур (опора / мачта):** Детекция несанкционированного присутствия людей (зацеперов) и посторонних объектов на крыше и суфле состава (`yolov8n.om`).
2. **Нижний контур (межшпальный короб):** Одноклассовый структурный анализ геометрии днища и поиск дефектов/закладок (`patchcore.om` / `cv::absdiff`).
3. **Аппаратный ToF-гейтинг:** Оптический затвор и нейросетевой пайплайн активируются сервоприводом строго при фиксации габарита клиренса лазерным дальномером VL53L0X[cite: 2, 4].
4. **Zero-Cloud Architecture:** Локальные вычисления на встроенном NPU Ascend 310B без выгрузки сырого видео в сеть.

---

## 🏗 Архитектура и пайплайн
```mermaid
flowchart TD
%% Стилизация узлов
    classDef hardware fill:#1e293b,stroke:#3b82f6,stroke-width:2px,color:#fff;
    classDef decision fill:#334155,stroke:#f59e0b,stroke-width:2px,color:#fff;
    classDef npu fill:#0f172a,stroke:#ef4444,stroke-width:2px,color:#fff;
    classDef alert fill:#7f1d1d,stroke:#dc2626,stroke-width:2px,color:#fff;
    classDef ok fill:#064e3b,stroke:#10b981,stroke-width:2px,color:#fff;

    Start(["Режим ожидания: Опрос шины /dev/i2c-7"]):::hardware --> ToF["ToF-лазер VL53L0X: Чтение дистанции h"]:::hardware

%% Аппаратный гейтинг и фильтрация
    ToF --> CheckClearance{"Клиренс поезда?<br/>30 мм <= h <= 80 мм"}:::decision
    CheckClearance -- Нет: Человек / Мусор / Пусто --> ToF
    CheckClearance -- Да --> CheckDebounce{"Удержание габарита<br/>>= 250 мс?"}:::decision

    CheckDebounce -- Ложный всплеск --> ToF
    CheckDebounce -- Габарит подтвержден --> OpenShutter["PCA9685: Открытие сервошторки нижнего короба"]:::hardware

    OpenShutter --> ParallelFork((Параллельный захват V4L2))

%% Верхний контур
    ParallelFork --> TopCam["Камера 0: Верхний рубеж<br/>640x640 /dev/video0"]:::hardware
    TopCam --> PreTop["fastPreprocessToNCHW<br/>Нормализация RGB FP32"]
    PreTop --> YoloNPU["Ascend NPU: yolov8n.om<br/>Инференс ~24 мс"]:::npu
    YoloNPU --> YoloPost["Постпроцессинг / NMS<br/>Фильтрация Bounding Boxes"]
    YoloPost --> CheckTop{"Обнаружен classId == 0<br/>Человек на крыше?"}:::decision

%% Нижний контур
    ParallelFork --> BotCam["Камера 4: Нижний рубеж<br/>640x480 /dev/video4"]:::hardware
    BotCam --> PreBot["Resize 256x256<br/>Экстракция тензора признаков"]
    PreBot --> PatchNPU["Ascend NPU: patchcore.om<br/>Memory Bank Distance"]:::npu
    PatchNPU --> CheckBot{"Anomaly Score > 0.40?<br/>Дефект днища / СВУ"}:::decision

%% Логика тревоги и алертов
    CheckTop -- Да --> AlertTop["🚨 TOP ALERT: Человек!"]:::alert
    CheckBot -- Да --> AlertBot["🚨 BOTTOM ALERT: Аномалия днища!"]:::alert

    CheckTop -- Нет --> TopOK["Крыша: Норма"]:::ok
    CheckBot -- Нет --> BotOK["Днище: Норма"]:::ok

    AlertTop --> DebounceTop{"Таймаут канала 1<br/>> 400 мс?"}:::decision
    AlertBot --> DebounceBot{"Таймаут канала 2<br/>> 400 мс?"}:::decision

    DebounceTop -- Да --> SendTop["StreamServer: broadcastAlarmFrame 1<br/>Передача кадра крыши на TCP :8088"]:::alert
    DebounceBot -- Да --> SendBot["StreamServer: broadcastAlarmFrame 2<br/>Передача кадра днища на TCP :8088"]:::alert

    DebounceTop -- Дебаунс спама --> CheckEndTrain{"Состав покинул<br/>створ ToF?"}:::decision
    DebounceBot -- Дебаунс спама --> CheckEndTrain
    SendTop --> CheckEndTrain
    SendBot --> CheckEndTrain
    TopOK --> CheckEndTrain
    BotOK --> CheckEndTrain

%% Цикл и завершение прохода
    CheckEndTrain -- Состав в створе --> ParallelFork
    CheckEndTrain -- Состав прошел --> CloseShutter["PCA9685: Закрытие защитной шторки короба"]:::hardware
    CloseShutter --> Start
```
---

## ⚡ Технические характеристики

| Параметр | Значение | Описание |
| :--- | :--- | :--- |
| **Вычислительная платформа** | Orange Pi AI Pro | SoC Huawei Ascend 310B4 (8/20 TOPS) |
| **Время цикла (Latency)** | **24–32 мс** | Обработка видеопотока $\ge 35$ FPS (для версии в 8 TOPS) |
| **Нейросеть верхнего яруса** | YOLOv8n (FP16/INT8) | Выявление силуэтов людей и посторонних предметов |
| **Нейросеть нижнего яруса** | PatchCore / One-Class | Поиск аномалий днища относительно эталона |
| **Аппаратный триггер** | VL53L0X (ToF-лазер) | 940 нм, опрос 33 Гц по I2C, фильтрация по клиренсу |
| **Конструкционные материалы** | Термостойкий ASA | Диапазон от -40°C до +85°C, УФ- и химостойкость |
| **Каналы оповещения** | POSIX Serial, GPIO, JSON | Интеграция с рабочим местом ДСП станции |

---

## 📁 Структура репозитория

```text
hsm-barrier/
├── CMakeLists.txt              # Конфигурация сборки CMake (C++20, CANN, OpenCV)
├── LICENSE                     # Лицензия проекта
├── README.md                   # Документация проекта
├── include/                    # Заголовочные файлы C++
│   ├── hardware/               # Низкоуровневые интерфейсы (I2C, PCA9685, Servo, VL53L0X)
│   │   ├── camera_stream.h
│   │   ├── i2c_bus.h
│   │   ├── pca9685.h
│   │   ├── servo.h
│   │   └── vl53l0x.h
│   └── npu/                    # Движок инференса AscendCL и постпроцессинг
│       ├── npu_model.h
│       └── yolo_postprocess.h
├── src/                        # Исходный код C++
│   ├── hardware/
│   ├── npu/
│   └── main.cpp                # Точка входа в систему постового контроля
├── models/                     # Каталог моделей
│   ├── .gitkeep
│   ├── yolov8n.om              # Скомпилированная модель детекции под NPU
│   └── patchcore.om            # Скомпилированная модель аномалий под NPU
├── scripts/                    # Скрипты автоматизации и MLOps
│   ├── build_arm64.sh          # Скрипт нативной сборки Release на плате
│   └── ML/                     # Экспорт и компиляция нейросетей
│       ├── requirements.txt
│       ├── export_yolo.py
│       ├── train_patchcore.py
│       └── convert_to_om.sh    # Вызов компилятора ATC под Ascend310B4
├── docs/                    # 
│   ├── convert_instructions.md # Инструкция по вызову компилятора ATC
 
```
---
### 🛠 Сборка и запуск
Системные требования:
* ОС: Ubuntu 22.04 LTS (aarch64) / openEuler
* Компилятор: GCC/G++ 11+ (стандарт C++20)
* SDK: Huawei CANN Toolkit 8.0.0+ (/usr/local/Ascend/ascend-toolkit)
* Библиотеки: OpenCV 4.5+ (libopencv-dev)

1. Подготовка окружения

```bash
source /usr/local/Ascend/ascend-toolkit/set_env.sh
export TE_PARALLEL_COMPILER=1
```

2. Компиляция моделей под NPU Ascend (ATC)

Если бинарные файлы .om еще не собраны:
```bash
chmod +x scripts/ML/convert_to_om.sh
./scripts/ML/convert_to_om.sh
```

3. Сборка бинарника barrier

Для автоматической нативной сборки с флагами оптимизации -O3:
```bash
chmod +x scripts/build_arm64.sh
./scripts/build_arm64.sh
```

Либо классическая сборка через CMake:

```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

4. Запуск комплекса
```bash
# Запуск исполняемого файла с доступом к /dev/i2c-7 и видеоустройствам
./build/barrier
```

### ⚙️ Аппаратный комплекс

* Оптико-электронный модуль: Две камеры видимого диапазона (верхняя обзорная на столбце/опоре + нижняя инспекционная в межшпальном ящике)[cite: 2, 3].
* Механическая защита (Clean-Lense): Подвижная шторка на сервоприводе с ШИМ-контроллером PCA9685, предохраняющая нижний объектив от щебня, мазута и снега в межпоездных интервалах.
* Материалы исполнения: Все несущие кронштейны и защитные кожухи изготовлены методом 3D-печати из полимера ASA (сохранение прочности при температурах от -40°C до +85°C, невосприимчивость к железнодорожным ГСМ и ультрафиолету)[cite: 2, 3].
* Инфраструктурная интеграция: Верхний модуль фиксируется на стандартные опоры контактной сети с использованием штатного станционного освещения (Zero-Footprint монтаж)[cite: 3].
