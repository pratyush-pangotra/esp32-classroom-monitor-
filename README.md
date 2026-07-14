# Smart Classroom Occupancy System

ESP32-based system that counts people entering/leaving a room using two IR
sensors, monitors temperature and humidity with a DHT22, automatically
switches a fan on/off based on temperature, and streams all data to a
Thinger.io dashboard.

## Objective

Count room occupancy using IR sensors and monitor environmental conditions,
with remote monitoring and control via the cloud.

## Components

- ESP32 dev board
- 2x IR obstacle/beam-break sensor modules
- DHT22 temperature & humidity sensor
- 5V single-channel relay module
- Small 2-pin brushless DC fan
- 10kΩ resistor (DHT22 pull-up, if not already on the breakout)
- Breadboard + jumper wires

## How occupancy counting works

Two IR sensors are mounted a short distance apart across the doorway:

- **IR1** — outer sensor, corridor side
- **IR2** — inner sensor, room side

A person walking through breaks both beams in sequence:

- **IR1 → IR2** (in that order) = person **entered** → count +1
- **IR2 → IR1** (in that order) = person **left** → count −1 (floored at 0)

If only one sensor triggers without the other following, it's treated as
noise and ignored.

Most IR obstacle modules are **active-LOW** (output goes LOW when the beam
is broken). If your module behaves the opposite way, flip the
`BEAM_BROKEN` constant in the code from `LOW` to `HIGH`.

## Wiring

| Component            | Pin/Terminal         | ESP32 Connection                 |
|-----------------------|----------------------|-----------------------------------|
| IR sensor 1 (outer)   | OUT                   | GPIO14                            |
| IR sensor 1           | VCC / GND             | 3.3V or 5V (per module) / GND     |
| IR sensor 2 (inner)   | OUT                   | GPIO25                            |
| IR sensor 2           | VCC / GND             | 3.3V or 5V (per module) / GND     |
| DHT22                 | DATA                  | GPIO4 (10kΩ pull-up to VCC)       |
| DHT22                 | VCC / GND             | 3.3V / GND                        |
| Relay module          | VCC                   | VIN (5V)                          |
| Relay module          | GND                   | GND                                |
| Relay module          | IN                    | GPIO33                            |

### Fan wiring (via relay, not direct GPIO)

An ESP32 GPIO pin **cannot** supply enough current to power a fan directly
(GPIO pins are rated for ~12mA safely, 40mA absolute max — most small fans
draw 60–150mA+). The relay isolates the fan's real power draw from the
ESP32 entirely:

- **VIN** → relay **NO** (normally open)
- Relay **COM** → fan **red wire**
- Fan **black wire** → **GND** (direct, not through the relay)

NO (normally open) is used so the fan defaults to **off** if the ESP32
resets or loses power, rather than defaulting to on.

> **Note:** Many cheap relay modules are active-LOW (the relay energizes
> when `IN` goes LOW, not HIGH). Test yours before wiring the fan in —
> toggle GPIO33 HIGH/LOW manually and listen for the relay's click. If
> yours is active-LOW, invert the `digitalWrite(FAN_PIN, ...)` calls in
> the fan-control section of the code.

## Setup

### 1. Arduino IDE

Install these libraries via Library Manager:
- `ThingerESP32`
- `DHT sensor library` (by Adafruit)

### 2. Thinger.io

1. Create an account at [thinger.io](https://thinger.io).
2. Add a new device, note its **Device ID** and the generated **Device
   Credential**.
3. Fill in `USERNAME`, `DEVICE_ID`, `DEVICE_CREDENTIAL`, `SSID`, and
   `SSID_PASSWORD` at the top of the sketch — **do not commit real
   credentials to a public repo**; the version in this repo uses
   placeholders.
4. Build a dashboard with widgets bound to these resources:
   - `occupancy` — counter/value widget
   - `temperature` — gauge widget
   - `humidity` — gauge widget
   - `fan_status` — indicator/switch widget
   - `fan_manual` — switch widget (manual fan override)
   - `reset_count` — button (zero the occupancy count)

### 3. Flash and test

Upload the sketch, open Serial Monitor at 115200 baud. You should see a
live status line every ~200ms showing raw sensor states, occupancy,
temperature, humidity, and fan status — useful for confirming wiring
before relying on the cloud dashboard.

## Configuration

| Constant          | Default | Description                          |
|--------------------|---------|----------------------------------------|
| `FAN_ON_TEMP`       | 29.0°C  | Temperature threshold to turn fan on  |
| `DHT_INTERVAL`      | 2500ms  | Minimum time between DHT22 reads      |
| `BEAM_BROKEN`       | `LOW`   | Active level when an IR beam is blocked |

## Troubleshooting

- **A sensor's `digitalRead` never changes** — check its onboard LED
  responds when blocked (rules out a wiring/power issue vs. a code
  issue). Try swapping the sensor to a different GPIO to isolate a bad
  pin from a bad sensor.
- **Fan doesn't turn on** — confirm the relay clicks when GPIO33 toggles;
  check `BEAM_BROKEN`-style active-high/active-low assumption for your
  relay module.
- **DHT22 reads `nan`** — check the 10kΩ pull-up resistor and wiring; the
  sensor also can't be read faster than every ~2 seconds.

## License

MIT — use freely for personal or educational projects.
