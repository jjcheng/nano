from ultralytics import YOLO
import coremltools as ct

# model = YOLO("tmp/UnderlineSegment.pt")
model = YOLO("tmp/PenDetector.pt")
model.export(format="coreml", imgsz=640,
             nms=False, half=False, device="mps")
