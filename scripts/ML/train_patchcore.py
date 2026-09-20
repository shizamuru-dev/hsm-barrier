import os
from pathlib import Path
from anomalib.data import Folder
from anomalib.models import Patchcore
from anomalib.engine import Engine
import albumentations as A

DATASET_ROOT = Path("dataset")

# 1. Датасет с легкой аугментацией на свет (чтобы не орал на мелкие тени)
datamodule = Folder(
    name="undercarriage_hq",
    root=DATASET_ROOT,
    normal_dir="train/good",
    abnormal_dir="test/bad",
    normal_test_dir="test/good",
    train_batch_size=4,
    eval_batch_size=4,
    num_workers=4,
)

# 2. Wide-ResNet50 для честной пространственной семантики
print("[INFO] Конфигурация модели PatchCore (Wide-ResNet50-2)...")
model = Patchcore(
    backbone="wide_resnet50_2",
    layers=["layer2", "layer3"],
    pre_trained=True,
    coreset_sampling_ratio=0.2, # Плотное ядро нормы
    num_neighbors=9             # Чуть сглаживаем локальные выбросы (вместо 5)
)

engine = Engine(
    max_epochs=1,
    accelerator="auto",
    default_root_dir="results_hq",
)

print("🚀 Обучение Coreset-памяти...")
engine.fit(model=model, datamodule=datamodule)

# 3. Экспорт под фиксированный вход 256x256 (или 384x384, если дефекты мелкие)
output_dir = Path("models_exported")
print(f"📦 Экспорт в ONNX...")
engine.export(
    model=model,
    export_type="onnx",
    export_root=output_dir,
    input_size=(256, 256),
)

print("✅ Качественный ONNX готов!")