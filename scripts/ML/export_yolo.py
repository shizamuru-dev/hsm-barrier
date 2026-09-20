from ultralytics import YOLO

def export_yolo():
    print("🚀 Загрузка весов YOLOv8n...")
    model = YOLO("yolov8n.pt")

    print("📦 Экспорт в ONNX со статическим входом под Ascend NPU...")
    # opset=11 гарантирует совместимость с парсером Huawei ATC
    model.export(
        format="onnx",
        imgsz=640,
        opset=11,
        simplify=True,
        dynamic=False
    )
    print("✅ Файл yolov8n.onnx готов!")

if __name__ == "__main__":
    export_yolo()