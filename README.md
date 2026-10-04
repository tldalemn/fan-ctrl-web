

# FAN-CTRL: Web Edition

Turn your pain cave fan into a smart fan you can control from your phone.

An ESP32 reads your Bluetooth heart rate strap and switches your fan between Low, Medium, and High. A built-in web app lets you pick the mode, set speeds by hand, and tune your heart rate zones without re-uploading code. An optional 16x2 LCD shows heart rate, fan speed, and the web address.

This is a fork of [fan-ctrl by Andrew Grabbs](https://github.com/agrabbs/fan-ctrl). His original build and video are here: [DIY KICKR Headwind (Smart Fan)](https://www.andrewgrabbs.com/interests/cycling/diy-kickr-headwind-smart-fan/). All credit for the original idea and hardware design goes to him.

<!-- Add photos here: the web app on your phone, the LCD, and the finished box -->
<img width="3072" height="4080" alt="PXL_20261004_034333836" src="https://github.com/user-attachments/assets/40f33698-d041-4620-8a54-516aca40e92a" />
<img width="1007" height="1712" alt="Screenshot_20261003-224518" src="https://github.com/user-attachments/assets/6970c87a-cde8-4078-9456-07de11223d1c" />

## What's new in this fork

- **Web control app.** Open it on any phone or computer on your WiFi.
  - **Off:** fan off
  - **Heart rate:** fan follows your heart rate zones
  - **Manual:** pick Low, Medium, or High
- **Zone editor in the app.** Set the bpm where each speed kicks in.
- **Settings survive a power cycle.** Mode and zones are saved on the ESP32.
- **15 second delay before slowing down.** Speeds up right away, but won't flick back and forth when you hover near a zone line. The app and LCD show a countdown.
- **Break before make switching.** All relays drop out for a moment before the new speed turns on, so two speed windings are never powered at once.
- **Optional daisy chain (interlock) wiring mode.** Hardware protection for the fan motor. See below.
- **I2C LCD support.** Address is detected automatically. Runs fine without a screen.
- **Auto reconnect** if the strap drops out. Fan turns off if heart rate stops for 10 seconds.
- **Optional strap lock.** Lock the fan to one strap so a second strap in the room can't grab it.

## Parts

- ESP32 dev board (ESP-WROOM-32 DevKit)
- 3 or 4 channel 5V relay board
- Bluetooth heart rate strap (tested with Garmin HRM-Pro Plus)
- Multi-speed fan (3 speeds)
- Jumper wires and a micro USB cable that carries data
- Optional: 16x2 LCD with I2C backpack

## Wiring

### Relays

| ESP32 | Relay board |
|---|---|
| D25 | IN1 (Low) |
| D26 | IN2 (Medium) |
| D27 | IN3 (High) |
| VIN (5V) | VCC |
| GND | GND |

Pins can be changed in `RELAY_PINS` at the top of the sketch.

### LCD (optional)

| ESP32 | LCD I2C backpack |
|---|---|
| D21 | SDA |
| D22 | SCL |
| VIN (5V) | VCC |
| GND | GND |

### Fan wiring: safer daisy chain option

> **Warning:** This involves mains voltage. Unplug the fan before working on it. If you're not comfortable with mains wiring, get help.

The original design uses one relay per speed. If two relays are ever on at once, two motor windings get power together, which is hard on the motor. This sketch switches in a safe order to avoid that. You can also make it impossible in hardware with a daisy chain, suggested by a commenter on Andrew's original post:

- Line in to **Relay 1 COM**. Leave Relay 1 NC open.
- **Relay 1 NO** to **Relay 2 COM**.
- **Relay 2 NC** to fan **Low**. **Relay 2 NO** to **Relay 3 COM**.
- **Relay 3 NC** to fan **Medium**. **Relay 3 NO** to fan **High**.

Relay 1 is on/off. Relay 2 picks Low or "faster." Relay 3 picks Medium or High. Only one speed wire can ever be live.

If you wire it this way, set `INTERLOCK_WIRING = true` in the sketch.

## Setup

1. **Install Arduino IDE** and add the **esp32 by Espressif** board package.
2. **Install the LCD library:** Sketch > Include Library > Manage Libraries, search **LiquidCrystal I2C**, and install the one by **Frank de Brabander**. A warning about ESP32 compatibility is normal. It works.
3. **Add your WiFi:** copy `secrets.example.h` to `secrets.h` in the same folder and fill in your network name and password. The ESP32 only connects to **2.4 GHz** WiFi.
4. **Board settings:**
   - Tools > Board: **ESP32 Dev Module**
   - Tools > Partition Scheme: **Huge APP (3MB No OTA/1MB SPIFFS)**. Bluetooth plus WiFi won't fit in the default.
5. **Upload.** If it hangs at "Connecting...", hold the **BOOT** button until the upload starts.
6. **Find the address:** open Serial Monitor at **115200** baud and press **EN**. It prints the control panel address. The LCD also shows it.

### Settings at the top of the sketch

| Setting | What it does |
|---|---|
| `RELAY_PINS` | GPIO pins for Low, Medium, High |
| `RELAY_ACTIVE_LOW` | `true` for most relay boards. Set `false` if the fan runs when the app says Off. |
| `INTERLOCK_WIRING` | `true` if you used the daisy chain wiring |
| `HRM_ADDRESS` | Optional. Lock to one strap, e.g. `"aa:bb:cc:dd:ee:ff"`. Leave `""` to use any strap. |
| `LCD_SDA`, `LCD_SCL` | I2C pins for the screen |
| `STEP_DOWN_DELAY_MS` | How long to wait before slowing the fan |
| `HR_TIMEOUT_MS` | How long without heart rate before the fan turns off |

## Using it

- Open **http://fan.local** on a computer, or the IP address from Serial Monitor.
- **Android phones** often can't open `.local` addresses. Use the IP instead, like `http://192.168.0.138`, then add it to your home screen.
- **Keep the IP from changing:** set a DHCP reservation for the ESP32 in your router. It shows up as **fan-control**.

## Troubleshooting

| Problem | Fix |
|---|---|
| No COM port in Arduino IDE | Try a different USB cable (many are charge only). On Windows, install the Silicon Labs CP210x driver. |
| Garbled text in Serial Monitor | Set the baud rate to 115200. |
| "Sketch too big" | Set Partition Scheme to Huge APP. |
| Strap never connects | Close Zwift and other apps that may hold the strap's Bluetooth connection. Many straps only allow one or two. Most also broadcast ANT+, so Zwift can use an ANT+ dongle instead. |
| Speeds in the wrong order | Swap the numbers in `RELAY_PINS`. |
| Fan does the opposite of the app | Change `RELAY_ACTIVE_LOW`. |
| LCD lit but blank | Turn the contrast knob on the back of the LCD. |
| "No LCD found" | Check SDA and SCL wiring, or change `LCD_SDA` and `LCD_SCL`. |

## Credits

- **Andrew Grabbs:** original fan-ctrl concept, code, and hardware design. [GitHub](https://github.com/agrabbs) and [website](https://www.andrewgrabbs.com).
- **Tyler Dale:** web control app, LCD support, zone editor, saved settings, and switching improvements.

## License

MIT. See [LICENSE](LICENSE).
