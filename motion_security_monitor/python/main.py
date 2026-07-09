# SPDX-License-Identifier: MIT
#
# Motion Security Monitor -- MPU (Linux) application.
#
# - Listens for "motion_detected" / "distance_update" notifications sent
#   from the MCU sketch over the Bridge.
# - Keeps the person-detection pipeline "asleep" (ignored) until a PIR
#   motion event wakes it up, so we're not running AI inference 24/7.
# - When a person is confirmed AND the last known ultrasonic distance is
#   under the threshold, calls back into the MCU to escalate the buzzer.
# - Publishes everything (video is handled by the Brick itself, we just
#   publish status/log/distance) to the web dashboard.

import threading
import time
from datetime import datetime, timezone

from arduino.app_utils import App, Bridge
from arduino.app_bricks.web_ui import WebUI
from arduino.app_bricks.video_objectdetection import VideoObjectDetection

ALERT_DISTANCE_CM = 50.0
VISION_ACTIVE_WINDOW_SEC = 20.0  # how long the vision pipeline stays "awake" after motion

ui = WebUI()
detection_stream = VideoObjectDetection()  # default model includes a "person" class

_lock = threading.Lock()
_state = {
    "distance_cm": None,
    "person_present": False,
    "vision_active_until": 0.0,
    "motion": False,
}


def log_event(message, level="info"):
    entry = {
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "level": level,        # info | warning | alert
        "message": message,
    }
    print(f"[{entry['timestamp']}] {level.upper()}: {message}")
    ui.send_message("log", entry)


def on_motion_detected():
    """Fired by the MCU (Bridge.notify) whenever the PIR sensor trips."""
    now = time.monotonic()
    with _lock:
        _state["vision_active_until"] = now + VISION_ACTIVE_WINDOW_SEC
        _state["motion"] = True
    log_event("Motion detected -- camera/vision pipeline active", "info")
    ui.send_message("status", {"motion": True})


def on_distance_update(distance_cm):
    """Fired by the MCU (Bridge.notify) on every ultrasonic ping."""
    with _lock:
        _state["distance_cm"] = distance_cm
    ui.send_message("distance", {"distance_cm": distance_cm})


def handle_detections(results):
    now = time.monotonic()
    with _lock:
        vision_active = now < _state["vision_active_until"]
        distance = _state["distance_cm"]
        if not vision_active and _state["motion"]:
            _state["motion"] = False
            ui.send_message("status", {"motion": False})

    if not vision_active:
        return  # no recent PIR trigger: skip acting on detections

    person_seen = any(r.get("label") == "person" for r in results)

    with _lock:
        _state["person_present"] = person_seen
    ui.send_message("status", {"person": person_seen})

    close_range = distance is not None and 0 < distance < ALERT_DISTANCE_CM

    if person_seen and close_range:
        Bridge.call("set_alert_level", 2)  # escalate to urgent beep
        log_event(f"ALERT: person within {distance:.0f} cm", "alert")
    elif close_range:
        log_event(f"Object within {distance:.0f} cm (no person confirmed)", "warning")


def on_get_status(sid):
    with _lock:
        snapshot = dict(_state)
    ui.send_message("status", snapshot, sid=sid)


# --- Wire everything up ---
Bridge.provide("motion_detected", on_motion_detected)
Bridge.provide("distance_update", on_distance_update)

detection_stream.on_detect_all(handle_detections)

ui.on_message("get_status", on_get_status)

App.run()
