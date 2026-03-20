#!/usr/bin/env python3
"""
Capture camera images and run basic UI quality checks for the e-paper weather screen.

Features:
- Live webcam preview
- Press SPACE to capture and analyze one frame
- Press Q to quit
- Optional auto-capture mode every N seconds
- Manual ROI workflow (press R) for reliable operation without auto detection
- Detect and label key UI elements on the rectified display
- Optional live ESP32 tuning via serial (arrow key nudges)
- Saves:
  - raw frame
  - detected and rectified display crop
  - annotated display image
  - JSON report with quality metrics and suggestions

Dependencies:
- pip install opencv-python numpy
- Optional: pip install pyserial
- Optional: pip install pytesseract (and install tesseract OCR binary)
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import time
from dataclasses import dataclass, asdict
from typing import List, Optional, Tuple

# Reduce noisy OpenCV backend logs on Linux (must be set before importing cv2).
os.environ.setdefault("OPENCV_LOG_LEVEL", "SILENT")
os.environ.setdefault("OPENCV_VIDEOIO_PRIORITY_OBSENSOR", "0")

import cv2
import numpy as np

try:
    import serial  # type: ignore
except Exception:
    serial = None

try:
    import pytesseract  # type: ignore
except Exception:
    pytesseract = None


DISPLAY_ASPECT = 250.0 / 122.0


def configure_opencv_logging() -> None:
    """Best-effort: reduce OpenCV camera backend warnings during probing."""
    # OpenCV API varies by version; try known options safely.
    try:
        if hasattr(cv2, "setLogLevel") and hasattr(cv2, "LOG_LEVEL_ERROR"):
            cv2.setLogLevel(cv2.LOG_LEVEL_ERROR)
            return
    except Exception:
        pass

    try:
        if hasattr(cv2, "utils") and hasattr(cv2.utils, "logging"):
            cv2.utils.logging.setLogLevel(cv2.utils.logging.LOG_LEVEL_ERROR)
    except Exception:
        pass


@dataclass
class UiMetrics:
    top_edge_ink_ratio: float
    bottom_edge_ink_ratio: float
    left_edge_ink_ratio: float
    right_edge_ink_ratio: float
    upper_half_ink_ratio: float
    lower_half_ink_ratio: float
    divider_y: int
    forecast_ink_ratio: float


@dataclass
class UiReport:
    timestamp: str
    warnings: List[str]
    suggestions: List[str]
    metrics: UiMetrics


@dataclass
class UiElement:
    name: str
    x: int
    y: int
    w: int
    h: int

    def contains(self, px: int, py: int) -> bool:
        return self.x <= px <= (self.x + self.w) and self.y <= py <= (self.y + self.h)


class UpdateIntervalVerifier:
    """Validate update cadence using the on-screen last-update timestamp value."""

    def __init__(
        self,
        expected_minutes: float,
        tolerance_minutes: float,
    ) -> None:
        self.expected_seconds = max(1.0, expected_minutes * 60.0)
        self.tolerance_seconds = max(0.0, tolerance_minutes * 60.0)

        self._last_screen_timestamp: Optional[str] = None
        self._last_change_epoch: Optional[float] = None
        self._last_interval_seconds: Optional[float] = None
        self._last_result = "warming-up"

    def _evaluate_interval(self, elapsed_seconds: float) -> str:
        target = self.expected_seconds
        delta = abs(elapsed_seconds - target)
        return "OK" if delta <= self.tolerance_seconds else "WARN"

    def status_line(self) -> str:
        if self._last_interval_seconds is None:
            if self._last_screen_timestamp:
                return f"update-check:{self._last_result} ts={self._last_screen_timestamp}"
            return f"update-check:{self._last_result}"

        mins = self._last_interval_seconds / 60.0
        target = self.expected_seconds / 60.0
        return f"update-check:{self._last_result} last={mins:.1f}m target={target:.1f}m"

    def ingest_screen_timestamp(self, screen_timestamp: str, now_epoch: float) -> Optional[str]:
        if self._last_screen_timestamp is None:
            self._last_screen_timestamp = screen_timestamp
            self._last_result = "warming-up"
            return f"[update-check] Baseline on-screen time: {screen_timestamp}"

        if screen_timestamp == self._last_screen_timestamp:
            return None

        prev_stamp = self._last_screen_timestamp
        self._last_screen_timestamp = screen_timestamp

        if self._last_change_epoch is None:
            self._last_change_epoch = now_epoch
            self._last_result = "warming-up"
            return (
                f"[update-check] First on-screen time change detected: "
                f"{prev_stamp} -> {screen_timestamp}. Timing verification starts now."
            )

        elapsed = now_epoch - self._last_change_epoch
        self._last_change_epoch = now_epoch
        self._last_interval_seconds = elapsed

        result = self._evaluate_interval(elapsed)
        self._last_result = result
        elapsed_min = elapsed / 60.0
        expected_min = self.expected_seconds / 60.0
        tol_min = self.tolerance_seconds / 60.0

        if result == "OK":
            return (
                f"[update-check][OK] On-screen time changed {prev_stamp} -> {screen_timestamp} "
                f"after {elapsed_min:.2f} min "
                f"(target {expected_min:.2f} +/- {tol_min:.2f} min)."
            )

        return (
            f"[update-check][WARN] On-screen time changed {prev_stamp} -> {screen_timestamp} "
            f"after {elapsed_min:.2f} min "
            f"(target {expected_min:.2f} +/- {tol_min:.2f} min)."
        )


class DisplayTimeReader:
    """Read last-update date/time from display and expose stable OCR results."""

    def __init__(self, sample_seconds: float) -> None:
        self.sample_seconds = max(0.5, sample_seconds)
        self._stable_value: Optional[str] = None
        self._candidate: Optional[str] = None
        self._candidate_hits = 0
        self._stable_samples = 2

    def _extract_roi(self, display_img: np.ndarray) -> np.ndarray:
        gray = cv2.cvtColor(display_img, cv2.COLOR_BGR2GRAY)
        h, w = gray.shape
        roi = gray[0:max(20, int(h * 0.34)), 0:max(50, int(w * 0.42))]
        up = cv2.resize(roi, None, fx=3.0, fy=3.0, interpolation=cv2.INTER_CUBIC)
        blur = cv2.GaussianBlur(up, (3, 3), 0)
        _, bw = cv2.threshold(blur, 0, 255, cv2.THRESH_BINARY + cv2.THRESH_OTSU)
        return bw

    def _normalize_time(self, text: str) -> Optional[str]:
        cleaned = text.replace("\n", " ").strip()
        if not cleaned:
            return None

        filtered = re.sub(r"[^0-9:./ ]", "", cleaned)
        time_match = re.search(r"\b([01]?\d|2[0-3])[:.]([0-5]\d)\b", filtered)
        if not time_match:
            return None

        hour = int(time_match.group(1))
        minute = int(time_match.group(2))

        date_match = re.search(r"\b([0-3]?\d)[/.-]([01]?\d)\b", filtered)
        if date_match:
            day = int(date_match.group(1))
            month = int(date_match.group(2))
            return f"{day:02d}/{month:02d} {hour:02d}:{minute:02d}"

        return f"{hour:02d}:{minute:02d}"

    def sample(self, display_img: np.ndarray) -> Tuple[Optional[str], Optional[str]]:
        if pytesseract is None:
            return self._stable_value, None

        roi = self._extract_roi(display_img)
        raw = pytesseract.image_to_string(
            roi,
            config="--psm 6 -c tessedit_char_whitelist=0123456789:./",
        )

        parsed = self._normalize_time(raw)
        prev_stable = self._stable_value
        if parsed is None:
            self._candidate = None
            self._candidate_hits = 0
            return self._stable_value, None

        if parsed == self._candidate:
            self._candidate_hits += 1
        else:
            self._candidate = parsed
            self._candidate_hits = 1

        if self._candidate_hits >= self._stable_samples:
            self._stable_value = parsed

        if self._stable_value is None or self._stable_value == prev_stable:
            return self._stable_value, None

        return self._stable_value, f"[display-time] {self._stable_value}"


class Esp32TuningClient:
    """Small serial client for the ESP32 UI tuning console."""

    def __init__(self, port: str, baud: int) -> None:
        if serial is None:
            raise RuntimeError("pyserial is not installed. Install with: pip install pyserial")
        self._port = port
        self._baud = baud
        self._ser = serial.Serial(port=port, baudrate=baud, timeout=0.2)

    def send(self, command: str) -> None:
        line = command.strip() + "\n"
        self._ser.write(line.encode("utf-8"))

    def nudge(self, field: str, delta: int) -> None:
        if delta == 0:
            return
        self.send(f"nudge {field} {delta}")

    def redraw(self) -> None:
        self.send("redraw")

    def close(self) -> None:
        try:
            self._ser.close()
        except Exception:
            pass


def list_serial_ports() -> List[str]:
    patterns = [
        "/dev/ttyUSB*",
        "/dev/ttyACM*",
        "/dev/cu.usb*",
        "/dev/cu.SLAB*",
        "COM*",
    ]
    ports: List[str] = []
    for p in patterns:
        ports.extend(glob.glob(p))
    # Stable ordering and dedup.
    return sorted(set(ports))


def _roi_bbox(binary: np.ndarray, x0: int, y0: int, x1: int, y1: int, min_pixels: int = 12) -> Tuple[int, int, int, int]:
    h, w = binary.shape
    x0 = max(0, min(x0, w - 1))
    x1 = max(1, min(x1, w))
    y0 = max(0, min(y0, h - 1))
    y1 = max(1, min(y1, h))
    if x1 <= x0 or y1 <= y0:
        return x0, y0, 1, 1

    roi = binary[y0:y1, x0:x1]
    ys, xs = np.where(roi > 0)
    if xs.size < min_pixels:
        return x0, y0, max(1, x1 - x0), max(1, y1 - y0)

    bx0 = x0 + int(xs.min())
    by0 = y0 + int(ys.min())
    bx1 = x0 + int(xs.max()) + 1
    by1 = y0 + int(ys.max()) + 1
    return bx0, by0, max(1, bx1 - bx0), max(1, by1 - by0)


def detect_ui_elements(display_img: np.ndarray) -> List[UiElement]:
    """Heuristic element detection over rectified panel image."""
    binary = to_binary(display_img)
    h, w = binary.shape
    divider_y = detect_divider_y(binary)

    elems: List[UiElement] = []

    # Top area elements.
    x, y, bw, bh = _roi_bbox(binary, 0, 0, int(w * 0.42), int(divider_y * 0.45))
    elems.append(UiElement("timestamp", x, y, bw, bh))

    x, y, bw, bh = _roi_bbox(binary, 0, int(divider_y * 0.15), int(w * 0.30), int(divider_y * 0.95))
    elems.append(UiElement("current_icon", x, y, bw, bh))

    x, y, bw, bh = _roi_bbox(binary, int(w * 0.20), int(divider_y * 0.20), int(w * 0.60), int(divider_y * 0.95))
    elems.append(UiElement("current_temp", x, y, bw, bh))

    x, y, bw, bh = _roi_bbox(binary, int(w * 0.52), int(divider_y * 0.20), int(w * 0.75), int(divider_y * 0.85))
    elems.append(UiElement("feel_temp", x, y, bw, bh))

    x, y, bw, bh = _roi_bbox(binary, int(w * 0.68), int(divider_y * 0.10), w, int(divider_y * 0.52))
    elems.append(UiElement("high_temp", x, y, bw, bh))

    x, y, bw, bh = _roi_bbox(binary, int(w * 0.68), int(divider_y * 0.45), w, int(divider_y * 0.98))
    elems.append(UiElement("low_temp", x, y, bw, bh))

    elems.append(UiElement("divider", 0, max(0, divider_y - 1), w, 3))

    # Forecast bands.
    lower_y0 = min(h - 1, divider_y + 2)
    lower_h = h - lower_y0
    hour_y0 = lower_y0
    hour_y1 = min(h, lower_y0 + int(lower_h * 0.35))
    temp_y0 = hour_y1
    temp_y1 = min(h, lower_y0 + int(lower_h * 0.72))
    icon_y0 = temp_y1
    icon_y1 = h

    x, y, bw, bh = _roi_bbox(binary, 0, hour_y0, w, hour_y1)
    elems.append(UiElement("forecast_hour", x, y, bw, bh))
    x, y, bw, bh = _roi_bbox(binary, 0, temp_y0, w, temp_y1)
    elems.append(UiElement("forecast_temp", x, y, bw, bh))
    x, y, bw, bh = _roi_bbox(binary, 0, icon_y0, w, icon_y1)
    elems.append(UiElement("forecast_icon", x, y, bw, bh))

    return elems


def draw_ui_elements_overlay(display_img: np.ndarray, elements: List[UiElement], selected_index: int) -> np.ndarray:
    out = display_img.copy()
    for i, el in enumerate(elements):
        color = (0, 220, 255) if i == selected_index else (0, 200, 0)
        cv2.rectangle(out, (el.x, el.y), (el.x + el.w, el.y + el.h), color, 1)
        cv2.putText(out, el.name, (el.x + 2, max(12, el.y - 3)), cv2.FONT_HERSHEY_SIMPLEX, 0.45, color, 1, cv2.LINE_AA)

    cv2.putText(
        out,
        "Click element | TAB next | Arrows move | [ ] size",
        (8, 18),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.48,
        (255, 0, 0),
        1,
        cv2.LINE_AA,
    )
    return out


def apply_element_nudge(client: Optional[Esp32TuningClient], element_name: str, dx: int, dy: int, dsize: int) -> bool:
    if client is None:
        return False

    mapping = {
        "timestamp": [("lastUpdateX", "x"), ("lastUpdateY", "y")],
        "current_icon": [("currentIconX", "x"), ("currentIconY", "y"), ("currentIconSize", "size")],
        "current_temp": [("currentTempX", "x"), ("currentTempY", "y")],
        "feel_temp": [
            ("feelTempX", "x"),
            ("feelTempY", "y"),
            ("feelDegreeX", "x"),
            ("feelDegreeY", "y"),
            ("feelCX", "x"),
            ("feelCY", "y"),
        ],
        "high_temp": [("highRightX", "x"), ("highY", "y")],
        "low_temp": [("lowRightX", "x"), ("lowY", "y")],
        "divider": [("dividerY", "y")],
        "forecast_hour": [("forecastHourY", "y")],
        "forecast_temp": [("forecastTempY", "y")],
        # OffsetX is inverted in firmware (xCenter - offset)
        "forecast_icon": [("forecastIconOffsetX", "inv_x"), ("forecastIconY", "y"), ("forecastIconSize", "size")],
    }

    fields = mapping.get(element_name, [])
    sent = False
    for field, axis in fields:
        delta = 0
        if axis == "x":
            delta = dx
        elif axis == "inv_x":
            delta = -dx
        elif axis == "y":
            delta = dy
        elif axis == "size":
            delta = dsize

        if delta != 0:
            client.nudge(field, int(delta))
            sent = True

    return sent


def order_quad_points(pts: np.ndarray) -> np.ndarray:
    """Return points ordered as TL, TR, BR, BL."""
    pts = pts.astype(np.float32)
    s = pts.sum(axis=1)
    diff = np.diff(pts, axis=1).flatten()

    top_left = pts[np.argmin(s)]
    bottom_right = pts[np.argmax(s)]
    top_right = pts[np.argmin(diff)]
    bottom_left = pts[np.argmax(diff)]
    return np.array([top_left, top_right, bottom_right, bottom_left], dtype=np.float32)


def _quad_geometry(rect: np.ndarray) -> Tuple[float, float, float]:
    """Return (width, height, area) for an ordered quad."""
    width_a = np.linalg.norm(rect[2] - rect[3])
    width_b = np.linalg.norm(rect[1] - rect[0])
    height_a = np.linalg.norm(rect[1] - rect[2])
    height_b = np.linalg.norm(rect[0] - rect[3])
    width = float(max(width_a, width_b))
    height = float(max(height_a, height_b))
    return width, height, width * height


def _contour_to_quad(cnt: np.ndarray) -> Optional[np.ndarray]:
    """Convert contour to 4-point quad via polygon or min-area rectangle fallback."""
    peri = cv2.arcLength(cnt, True)
    approx = cv2.approxPolyDP(cnt, 0.02 * peri, True)
    if len(approx) == 4:
        return order_quad_points(approx.reshape(4, 2).astype(np.float32))

    rect = cv2.minAreaRect(cnt)
    box = cv2.boxPoints(rect).astype(np.float32)
    ordered = order_quad_points(box)

    width, height, _ = _quad_geometry(ordered)
    if width < 20 or height < 20:
        return None
    return ordered


def _score_quad(rect: np.ndarray, frame_shape: Tuple[int, int]) -> float:
    """Score candidate display quad using aspect, area and center proximity."""
    h, w = frame_shape
    frame_area = float(h * w)

    width, height, area = _quad_geometry(rect)
    if height <= 1:
        return -1.0

    aspect = width / height
    # Keep a broad window for perspective distortion.
    if aspect < 1.35 or aspect > 3.4:
        return -1.0

    area_ratio = area / frame_area
    if area_ratio < 0.03 or area_ratio > 0.95:
        return -1.0

    aspect_error = abs(aspect - DISPLAY_ASPECT)
    aspect_score = max(0.0, 1.0 - (aspect_error / 1.4))
    area_score = min(1.0, area_ratio / 0.28)

    center = rect.mean(axis=0)
    cx, cy = float(center[0]), float(center[1])
    norm_dx = abs(cx - (w * 0.5)) / (w * 0.5)
    norm_dy = abs(cy - (h * 0.5)) / (h * 0.5)
    center_score = max(0.0, 1.0 - (0.7 * norm_dx + 0.3 * norm_dy))

    return (0.50 * aspect_score) + (0.30 * area_score) + (0.20 * center_score)


def _extract_contours_multi(gray: np.ndarray, frame: np.ndarray) -> List[np.ndarray]:
    """Get contours from complementary pipelines: edges and bright panel mask."""
    contours_all: List[np.ndarray] = []

    # Pipeline A: edge-heavy with local contrast boost.
    clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8))
    boosted = clahe.apply(gray)
    blur = cv2.GaussianBlur(boosted, (5, 5), 0)
    edges = cv2.Canny(blur, 40, 130)
    edges = cv2.morphologyEx(edges, cv2.MORPH_CLOSE, np.ones((5, 5), np.uint8), iterations=1)
    c_edges, _ = cv2.findContours(edges, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    contours_all.extend(c_edges)

    # Pipeline B: bright, low-saturation panel mask (works well for white e-paper).
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
    s = hsv[:, :, 1]
    v = hsv[:, :, 2]
    bright_mask = cv2.inRange(v, 130, 255)
    low_sat_mask = cv2.inRange(s, 0, 95)
    mask = cv2.bitwise_and(bright_mask, low_sat_mask)
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, np.ones((7, 7), np.uint8), iterations=2)
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, np.ones((3, 3), np.uint8), iterations=1)
    c_mask, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    contours_all.extend(c_mask)

    return contours_all


def detect_display_quad(frame: np.ndarray) -> Optional[np.ndarray]:
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    contours = _extract_contours_multi(gray, frame)

    best_score = -1.0
    best_quad: Optional[np.ndarray] = None

    for cnt in contours:
        if cv2.contourArea(cnt) < 400:
            continue

        rect = _contour_to_quad(cnt)
        if rect is None:
            continue

        score = _score_quad(rect, gray.shape)
        if score > best_score:
            best_score = score
            best_quad = rect

    return best_quad


def warp_display(frame: np.ndarray, quad: np.ndarray, out_w: int = 500, out_h: int = 244) -> np.ndarray:
    dst = np.array(
        [[0, 0], [out_w - 1, 0], [out_w - 1, out_h - 1], [0, out_h - 1]],
        dtype=np.float32,
    )
    matrix = cv2.getPerspectiveTransform(quad, dst)
    return cv2.warpPerspective(frame, matrix, (out_w, out_h))


def to_binary(display_img: np.ndarray) -> np.ndarray:
    gray = cv2.cvtColor(display_img, cv2.COLOR_BGR2GRAY)
    _, binary = cv2.threshold(gray, 180, 255, cv2.THRESH_BINARY_INV)
    return binary


def detect_divider_y(binary: np.ndarray) -> int:
    row_sums = binary.mean(axis=1)
    h = binary.shape[0]
    mid_start = int(h * 0.35)
    mid_end = int(h * 0.70)
    segment = row_sums[mid_start:mid_end]
    if segment.size == 0:
        return h // 2
    local_idx = int(np.argmax(segment))
    return mid_start + local_idx


def edge_ink_ratio(binary: np.ndarray, side: str, thickness: int) -> float:
    h, w = binary.shape
    if side == "top":
        region = binary[0:thickness, :]
    elif side == "bottom":
        region = binary[h - thickness:h, :]
    elif side == "left":
        region = binary[:, 0:thickness]
    else:
        region = binary[:, w - thickness:w]
    return float(np.count_nonzero(region)) / float(region.size)


def analyze_ui(display_img: np.ndarray) -> Tuple[UiReport, np.ndarray]:
    binary = to_binary(display_img)
    h, w = binary.shape
    divider_y = detect_divider_y(binary)

    top_t = max(2, int(h * 0.05))
    side_t = max(2, int(w * 0.02))

    top_edge = edge_ink_ratio(binary, "top", top_t)
    bottom_edge = edge_ink_ratio(binary, "bottom", top_t)
    left_edge = edge_ink_ratio(binary, "left", side_t)
    right_edge = edge_ink_ratio(binary, "right", side_t)

    upper_half = binary[:divider_y, :]
    lower_half = binary[divider_y:, :]
    upper_ratio = float(np.count_nonzero(upper_half)) / float(upper_half.size)
    lower_ratio = float(np.count_nonzero(lower_half)) / float(lower_half.size)

    forecast_start = min(h - 1, divider_y + max(4, int(h * 0.02)))
    forecast_zone = binary[forecast_start:, :]
    forecast_ratio = float(np.count_nonzero(forecast_zone)) / float(forecast_zone.size)

    warnings: List[str] = []
    suggestions: List[str] = []

    if top_edge > 0.09:
        warnings.append("Top area may be clipped.")
        suggestions.append("Move top timestamp and icon down by 1-3 px.")
    if bottom_edge > 0.12:
        warnings.append("Bottom content may be touching the border.")
        suggestions.append("Move forecast icons up or reduce icon size slightly.")
    if right_edge > 0.12:
        warnings.append("Right-side text may be too close to border.")
        suggestions.append("Shift max/min block 1-3 px left.")
    if forecast_ratio > 0.33:
        warnings.append("Forecast strip appears dense.")
        suggestions.append("Use smaller forecast font or increase vertical spacing.")
    if abs(upper_ratio - lower_ratio) > 0.25:
        warnings.append("Visual balance between top and bottom sections is uneven.")
        suggestions.append("Rebalance font sizes or spacing between sections.")

    metrics = UiMetrics(
        top_edge_ink_ratio=top_edge,
        bottom_edge_ink_ratio=bottom_edge,
        left_edge_ink_ratio=left_edge,
        right_edge_ink_ratio=right_edge,
        upper_half_ink_ratio=upper_ratio,
        lower_half_ink_ratio=lower_ratio,
        divider_y=divider_y,
        forecast_ink_ratio=forecast_ratio,
    )

    report = UiReport(
        timestamp=time.strftime("%Y-%m-%d %H:%M:%S"),
        warnings=warnings,
        suggestions=suggestions,
        metrics=metrics,
    )

    annotated = display_img.copy()
    cv2.line(annotated, (0, divider_y), (w - 1, divider_y), (0, 0, 255), 1)
    cv2.rectangle(annotated, (0, 0), (w - 1, h - 1), (0, 255, 0), 1)

    y = 18
    if warnings:
        for msg in warnings[:3]:
            cv2.putText(annotated, f"WARN: {msg}", (8, y), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0, 0, 255), 1, cv2.LINE_AA)
            y += 18
    else:
        cv2.putText(annotated, "UI checks passed", (8, y), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 140, 0), 1, cv2.LINE_AA)

    return report, annotated


def save_outputs(out_dir: str, frame: np.ndarray, display_img: np.ndarray, annotated: np.ndarray, report: UiReport) -> None:
    os.makedirs(out_dir, exist_ok=True)
    stamp = time.strftime("%Y%m%d_%H%M%S")

    raw_path = os.path.join(out_dir, f"raw_{stamp}.jpg")
    crop_path = os.path.join(out_dir, f"display_{stamp}.jpg")
    ann_path = os.path.join(out_dir, f"annotated_{stamp}.jpg")
    rep_path = os.path.join(out_dir, f"report_{stamp}.json")

    cv2.imwrite(raw_path, frame)
    cv2.imwrite(crop_path, display_img)
    cv2.imwrite(ann_path, annotated)

    with open(rep_path, "w", encoding="utf-8") as f:
        json.dump(
            {
                "timestamp": report.timestamp,
                "warnings": report.warnings,
                "suggestions": report.suggestions,
                "metrics": asdict(report.metrics),
            },
            f,
            indent=2,
        )

    print(f"Saved raw frame:      {raw_path}")
    print(f"Saved display crop:   {crop_path}")
    print(f"Saved annotated view: {ann_path}")
    print(f"Saved report:         {rep_path}")

    if report.warnings:
        print("\nWarnings:")
        for w in report.warnings:
            print(f"- {w}")
    if report.suggestions:
        print("\nSuggestions:")
        for s in report.suggestions:
            print(f"- {s}")


def process_capture(
    frame: np.ndarray,
    out_dir: str,
    forced_quad: Optional[np.ndarray] = None,
    auto_detect: bool = False,
) -> None:
    quad = forced_quad if forced_quad is not None else (detect_display_quad(frame) if auto_detect else None)
    if quad is None:
        print("Display region not set. Press 'R' and select the screen region.")
        stamp = time.strftime("%Y%m%d_%H%M%S")
        os.makedirs(out_dir, exist_ok=True)
        cv2.imwrite(os.path.join(out_dir, f"raw_no_display_{stamp}.jpg"), frame)
        return

    display_img = warp_display(frame, quad)
    report, annotated = analyze_ui(display_img)
    save_outputs(out_dir, frame, display_img, annotated, report)


def select_display_quad(frame: np.ndarray) -> Optional[np.ndarray]:
    """Let user click 4 display corners and return an ordered perspective quad."""
    window = "Select Display Corners"
    cv2.namedWindow(window, cv2.WINDOW_NORMAL | cv2.WINDOW_GUI_NORMAL)

    state = {"points": []}

    def on_mouse(event: int, x: int, y: int, flags: int, param: object) -> None:
        if event == cv2.EVENT_LBUTTONDOWN:
            points: List[Tuple[int, int]] = state["points"]  # type: ignore[assignment]
            if len(points) < 4:
                points.append((x, y))
        elif event == cv2.EVENT_RBUTTONDOWN:
            points = state["points"]  # type: ignore[assignment]
            if points:
                points.pop()

    cv2.setMouseCallback(window, on_mouse)

    while True:
        preview = frame.copy()
        points: List[Tuple[int, int]] = state["points"]  # type: ignore[assignment]

        for i, (px, py) in enumerate(points):
            cv2.circle(preview, (px, py), 6, (0, 255, 255), -1)
            cv2.putText(
                preview,
                str(i + 1),
                (px + 8, py - 8),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.55,
                (0, 255, 255),
                2,
                cv2.LINE_AA,
            )

        if len(points) >= 2:
            cv2.polylines(preview, [np.array(points, dtype=np.int32)], False, (0, 200, 0), 2)
        if len(points) == 4:
            cv2.polylines(preview, [np.array(points, dtype=np.int32)], True, (0, 255, 0), 2)

        cv2.putText(
            preview,
            "Click 4 corners TL->TR->BR->BL | Right click undo | C clear | Enter confirm | Esc cancel",
            (10, 24),
            cv2.FONT_HERSHEY_SIMPLEX,
            0.52,
            (20, 220, 20),
            2,
            cv2.LINE_AA,
        )

        cv2.imshow(window, preview)
        key = cv2.waitKeyEx(1)

        if key in (27, ord("q")):  # Esc or q
            cv2.destroyWindow(window)
            return None
        if key in (ord("c"), ord("C")):
            state["points"] = []
            continue
        if key in (13, 10, 32):  # Enter, Return, Space
            if len(points) == 4:
                cv2.destroyWindow(window)
                return order_quad_points(np.array(points, dtype=np.float32))



def _try_open_camera(index: int) -> Optional[cv2.VideoCapture]:
    """Try to open camera index with common backends, return opened capture or None."""
    candidates = [
        cv2.CAP_V4L2,
        cv2.CAP_ANY,
    ]
    for backend in candidates:
        cap = cv2.VideoCapture(index, backend)
        if cap.isOpened():
            ok, _ = cap.read()
            if ok:
                return cap
        cap.release()
    return None


def list_available_cameras(max_index: int = 10) -> List[int]:
    available: List[int] = []
    for idx in range(max_index + 1):
        cap = _try_open_camera(idx)
        if cap is not None:
            available.append(idx)
            cap.release()
    return available


def run_camera(
    camera_index: int,
    out_dir: str,
    auto_seconds: float,
    probe_max_index: int,
    serial_port: Optional[str],
    serial_baud: int,
    auto_detect: bool,
    verify_updates: bool,
    expected_update_minutes: float,
    update_tolerance_minutes: float,
    monitor_sample_seconds: float,
    read_display_time: bool,
    time_read_seconds: float,
) -> None:
    if camera_index >= 0:
        cap = _try_open_camera(camera_index)
        if cap is None:
            available = list_available_cameras(max_index=probe_max_index)
            raise RuntimeError(
                f"Unable to open camera index {camera_index}. "
                f"Detected working indices: {available if available else 'none'}"
            )
        selected_index = camera_index
    else:
        available = list_available_cameras(max_index=probe_max_index)
        if not available:
            raise RuntimeError(
                "No working camera indices found. "
                "Check permissions, camera connection, and if another app is using the camera."
            )
        selected_index = available[0]
        cap = _try_open_camera(selected_index)
        if cap is None:
            raise RuntimeError(f"Camera index {selected_index} was detected but could not be reopened")

    client: Optional[Esp32TuningClient] = None
    serial_state = "off"
    serial_error = ""
    if serial_port:
        try:
            client = Esp32TuningClient(serial_port, serial_baud)
            serial_state = f"connected:{serial_port}"
            print(f"Connected to ESP32 console on {serial_port} @ {serial_baud}")
        except Exception as ex:
            serial_state = "error"
            serial_error = str(ex)
            print(f"Serial connection failed: {serial_error}")
            print("Hint: close any Serial Monitor using this port and check user permission for /dev/ttyACM*.")

    print(f"Camera opened on index {selected_index}.")
    print("Keys: R=set 4-corner display quad, SPACE=capture, TAB/Arrows/[ ] tune, Q=quit")
    if not auto_detect:
        print("Auto detection is OFF. Use R to select the display region.")
    if auto_seconds > 0:
        print(f"Auto-capture every {auto_seconds:.1f} seconds")
    if verify_updates:
        if pytesseract is None:
            print("Update verification requires OCR timestamp reading, but pytesseract is not installed.")
            print("Install with: pip install pytesseract (and system package: tesseract-ocr)")
        else:
            print(
                "Update verification ON (from on-screen date/time): "
                f"target={expected_update_minutes:.1f} min, "
                f"tolerance={update_tolerance_minutes:.1f} min, "
                f"sample={monitor_sample_seconds:.1f}s"
            )
    if read_display_time:
        if pytesseract is None:
            print("Display time OCR requested, but pytesseract is not installed.")
            print("Install with: pip install pytesseract (and system package: tesseract-ocr)")
        else:
            print(f"Display time OCR ON: sample={time_read_seconds:.1f}s")

    last_auto = time.time()
    last_monitor_sample = 0.0
    last_time_sample = 0.0
    active_quad: Optional[np.ndarray] = None
    verifier: Optional[UpdateIntervalVerifier] = None
    time_reader: Optional[DisplayTimeReader] = None
    use_ocr_for_verify = verify_updates and (pytesseract is not None)
    if use_ocr_for_verify:
        verifier = UpdateIntervalVerifier(
            expected_minutes=expected_update_minutes,
            tolerance_minutes=update_tolerance_minutes,
        )
    if (read_display_time or use_ocr_for_verify) and pytesseract is not None:
        time_reader = DisplayTimeReader(sample_seconds=time_read_seconds)

    state = {"elements": [], "selected": 0}

    window_flags = cv2.WINDOW_NORMAL | cv2.WINDOW_GUI_NORMAL
    cv2.namedWindow("E-paper UI Camera Helper", window_flags)

    try:
        while True:
            ok, frame = cap.read()
            if not ok:
                print("Failed to read frame")
                break

            preview = frame.copy()
            quad = active_quad
            if quad is None and auto_detect:
                quad = detect_display_quad(preview)
            if quad is not None:
                q = quad.astype(int)
                cv2.polylines(preview, [q], True, (0, 255, 0), 2)

                display_img = warp_display(frame, quad)
                elements = detect_ui_elements(display_img)
                state["elements"] = elements
                if state["selected"] >= len(elements):
                    state["selected"] = 0

            status = f"serial:{serial_state}"
            if verifier is not None:
                status += f" | {verifier.status_line()}"
            cv2.putText(preview, f"R set corners | SPACE capture | TAB/Arrows tune | Q quit | {status}", (10, 24), cv2.FONT_HERSHEY_SIMPLEX, 0.52, (30, 200, 30), 2, cv2.LINE_AA)
            if serial_state == "error" and serial_error:
                msg = f"serial error: {serial_error}"
                cv2.putText(preview, msg[:100], (10, 46), cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0, 0, 255), 1, cv2.LINE_AA)
            cv2.imshow("E-paper UI Camera Helper", preview)

            now = time.time()
            if auto_seconds > 0 and (now - last_auto) >= auto_seconds:
                print("Auto capture...")
                process_capture(frame, out_dir, forced_quad=active_quad, auto_detect=auto_detect)
                last_auto = now

            need_verify_sample = verifier is not None and (now - last_monitor_sample) >= max(0.5, monitor_sample_seconds)
            need_time_sample = time_reader is not None and (now - last_time_sample) >= time_reader.sample_seconds
            if quad is not None and (need_verify_sample or need_time_sample):
                display_for_check = warp_display(frame, quad)

                stable_stamp: Optional[str] = None
                time_msg: Optional[str] = None
                if time_reader is not None:
                    stable_stamp, time_msg = time_reader.sample(display_for_check)

                if need_verify_sample and verifier is not None:
                    message = verifier.ingest_screen_timestamp(stable_stamp, now) if stable_stamp is not None else None
                    if message:
                        print(message)
                    last_monitor_sample = now

                if need_time_sample and time_reader is not None:
                    if time_msg:
                        print(time_msg)
                    last_time_sample = now

            key = cv2.waitKeyEx(1)
            if key == ord("q"):
                break
            if key == ord(" "):
                process_capture(frame, out_dir, forced_quad=active_quad, auto_detect=auto_detect)
            if key == ord("r"):
                selected_quad = select_display_quad(frame)
                if selected_quad is not None:
                    active_quad = selected_quad
                    print("Display 4-corner quad updated.")
                    process_capture(frame, out_dir, forced_quad=active_quad, auto_detect=False)
                else:
                    print("Manual corner selection canceled")

            # TAB cycles selected UI element.
            if key == 9 and state["elements"]:
                state["selected"] = (int(state["selected"]) + 1) % len(state["elements"])
                chosen = state["elements"][int(state["selected"])]
                print(f"Selected element: {chosen.name}")

            # Arrow keys for nudging the selected element.
            dx = 0
            dy = 0
            if key in (2424832, 81):   # left
                dx = -1
            elif key in (2555904, 83):  # right
                dx = 1
            elif key in (2490368, 82):  # up
                dy = -1
            elif key in (2621440, 84):  # down
                dy = 1

            dsize = 0
            if key == ord("["):
                dsize = -1
            elif key == ord("]"):
                dsize = 1

            if (dx != 0 or dy != 0 or dsize != 0) and state["elements"]:
                selected = state["elements"][int(state["selected"])]
                if client is None:
                    print("No serial client configured. Use --serial-port to apply tuning on ESP32.")
                else:
                    changed = apply_element_nudge(client, selected.name, dx, dy, dsize)
                    if changed:
                        client.redraw()
                        print(f"Nudged {selected.name}: dx={dx} dy={dy} dsize={dsize}")

    finally:
        cap.release()
        cv2.destroyAllWindows()
        if client is not None:
            client.close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Capture camera photos and score e-paper UI quality")
    parser.add_argument(
        "--camera",
        type=int,
        default=-1,
        help="Camera index (default: -1, auto-pick first working camera)",
    )
    parser.add_argument(
        "--probe-max-index",
        type=int,
        default=10,
        help="Highest camera index to probe when auto-detecting/listing (default: 10)",
    )
    parser.add_argument(
        "--list-cameras",
        action="store_true",
        help="List working camera indices and exit",
    )
    parser.add_argument(
        "--list-serial",
        action="store_true",
        help="List likely serial ports and exit",
    )
    parser.add_argument(
        "--serial-port",
        default="",
        help="ESP32 serial port for live tuning (e.g. /dev/ttyACM0)",
    )
    parser.add_argument(
        "--serial-baud",
        type=int,
        default=115200,
        help="ESP32 serial baud rate (default: 115200)",
    )
    parser.add_argument("--out", default="ui_captures", help="Output directory (default: ui_captures)")
    parser.add_argument(
        "--auto-seconds",
        type=float,
        default=0.0,
        help="Auto-capture interval in seconds; 0 disables auto mode",
    )
    parser.add_argument(
        "--auto-detect",
        action="store_true",
        help="Enable automatic display detection (default: off; manual ROI with R)",
    )
    parser.add_argument(
        "--verify-updates",
        action="store_true",
        help="Monitor live display changes and verify refresh cadence",
    )
    parser.add_argument(
        "--expected-update-minutes",
        type=float,
        default=15.0,
        help="Expected display refresh interval in minutes (default: 15)",
    )
    parser.add_argument(
        "--update-tolerance-minutes",
        type=float,
        default=3.0,
        help="Allowed deviation from expected refresh interval in minutes (default: 3)",
    )
    parser.add_argument(
        "--monitor-sample-seconds",
        type=float,
        default=5.0,
        help="How often to sample the display for change detection (default: 5)",
    )
    parser.add_argument(
        "--read-display-time",
        action="store_true",
        help="Read time from display timestamp area using OCR and print to console",
    )
    parser.add_argument(
        "--time-read-seconds",
        type=float,
        default=5.0,
        help="Sampling period for OCR time reading in seconds (default: 5)",
    )
    return parser.parse_args()


def main() -> None:
    configure_opencv_logging()
    args = parse_args()
    if args.list_cameras:
        available = list_available_cameras(max_index=args.probe_max_index)
        if available:
            print("Working camera indices:", ", ".join(str(i) for i in available))
        else:
            print("No working camera indices found.")
        return

    if args.list_serial:
        ports = list_serial_ports()
        if ports:
            print("Serial ports:", ", ".join(ports))
        else:
            print("No serial ports found.")
        return

    run_camera(
        args.camera,
        args.out,
        args.auto_seconds,
        args.probe_max_index,
        args.serial_port.strip() or None,
        args.serial_baud,
        args.auto_detect,
        args.verify_updates,
        args.expected_update_minutes,
        args.update_tolerance_minutes,
        args.monitor_sample_seconds,
        args.read_display_time,
        args.time_read_seconds,
    )


if __name__ == "__main__":
    main()
