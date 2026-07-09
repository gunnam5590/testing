# Motion Security Monitor (Arduino UNO Q)

A PIR + ultrasonic + USB camera security monitor for the Arduino UNO Q,
built as an Arduino App Lab "App" (Bridge + Bricks). Buzzer beeps when
something gets within 50 cm; it escalates to an urgent double-beep if
the camera also confirms a person in frame while close.

## How it works

```
PIR sensor ---\
               \
Ultrasonic ---- MCU (sketch.ino, STM32/Zephyr) ---Bridge (RPC)--- MPU (main.py, Linux)
               /                                                        |
Buzzer -------/                                                  Camera + person
                                                                   detection Brick
                                                                          |
                                                                     Web dashboard
                                                                (video, distance, log)
```

- **MCU (real-time side)**: reads the PIR sensor, pings the ultrasonic
  sensor on a fixed schedule, and drives the buzzer directly. This keeps
  the beep timing jitter-free, which you don't get for free if you try
  to bit-bang a buzzer from a Python loop on Linux.
- **MPU (Linux side)**: runs the `video_object_detection` Brick, waits
  for a "motion detected" notification from the MCU before it starts
  acting on frames (so it isn't burning CPU/GPU on inference all day),
  and calls back into the MCU to escalate the buzzer when a person is
  confirmed within range.
- **Web dashboard**: served by the `web_ui` Brick at `http://<board>.local:7000`
  — live annotated video, motion/person/distance badges, and a scrolling
  event log.

## Wiring

| Component | Pin |
|---|---|
| PIR sensor OUT | D2 |
| Ultrasonic (HC-SR04) TRIG | D4 |
| Ultrasonic (HC-SR04) ECHO | D5 |
| Buzzer (+) | D3 (PWM-capable, used with `tone()`) |
| USB camera | USB-C hub port (needs external power for the hub) |

Change the pin constants at the top of `sketch/sketch.ino` if your wiring differs.

## Project structure

```
motion_security_monitor/
├── app.yaml            # App manifest: Bricks used (video_object_detection, web_ui)
├── sketch/
│   ├── sketch.ino       # MCU firmware: PIR, ultrasonic, buzzer
│   └── sketch.yaml       # Arduino CLI sketch project file (board FQBN, libraries)
├── python/
│   └── main.py           # MPU app: Bridge glue, person-detection logic, logging
└── assets/
    ├── index.html
    ├── app.js            # dashboard client (websocket via WebUI brick)
    └── style.css
```

## Setup in Arduino App Lab

1. Create a new blank App in App Lab, then replace its files with the
   ones in this folder (`sketch.ino` into the Sketch tab, `main.py` into
   the Python tab, the `assets/` files into Assets).
2. Add the **Video Object Detection** Brick and the **WebUI - HTML**
   Brick if App Lab doesn't pick them up from `app.yaml` automatically
   (Bricks tab → +). Once added, App Lab owns the `bricks:` section of
   `app.yaml` — don't hand-edit it after that.
3. Wire up the PIR sensor, ultrasonic sensor, and buzzer per the table
   above. Connect the USB camera via a **powered USB-C hub** (the board
   can't reliably power a webcam on its own).
4. Run the App. Give the first run extra time — it needs to pull the
   Brick's Docker container.
5. Open `http://<your-board-hostname>.local:7000` (or the board's IP) to
   see the dashboard. Walk in front of the sensor to trigger the PIR,
   then move within 50 cm of the ultrasonic sensor to see the buzzer/log
   escalate.

## Tuning

- `ALERT_DISTANCE_CM` (both files) — proximity threshold, currently 50 cm.
- `VISION_ACTIVE_WINDOW_SEC` (`main.py`) — how long the vision pipeline
  keeps acting on detections after a PIR trigger before going idle again.
- `MOTION_RETRIGGER_MS` (`sketch.ino`) — PIR debounce; raise this if your
  sensor is chattery.
- The detection model defaults to a general object-detection model
  (filtered in Python for the `"person"` label). If you'd rather use a
  dedicated person/face model, check the Brick's "AI models" tab in App
  Lab and update `model:` in `app.yaml` to match.

## Things worth double-checking on your setup

App Lab and its Bridge/Bricks APIs are actively evolving, so a few
specifics here are my best synthesis of the current docs/examples
rather than 100%-guaranteed-stable API surface. Worth a quick sanity
check the first time you run this:

- **`Bridge.provide()` on the Python side** for receiving MCU-originated
  events (`motion_detected`, `distance_update`) — this mirrors the
  documented MCU-side `Bridge.provide()`/`Bridge.call()` pattern, but if
  your App Lab version expects a different call (e.g. an `on()`-style
  listener), the Bridge API reference panel inside App Lab is the
  authoritative source.
- **`VideoObjectDetection` result shape** — I'm assuming each result in
  `on_detect_all` is a dict with at least a `label` key. Print `results`
  once to the console to confirm the exact field name if detection
  seems to silently miss.
- **Model name `object-detection`** in `app.yaml` — pick whatever's
  actually listed in the Brick's AI models tab; the built-in default
  model name may differ slightly by App Lab version.

If any of these don't quite match, the fix is almost always a one-line
change (the field/method name), not a restructure of the logic.
