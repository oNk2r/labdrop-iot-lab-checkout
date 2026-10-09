# LabDrop

**Smart laboratory equipment checkout and tracking system using ESP32, RFID, MQTT, and Node-RED.**

LabDrop tracks laboratory equipment borrowing and returns using RFID cards. The ESP32 processes scans, provides feedback through an OLED, LEDs, and buzzer, and publishes updates to a Node-RED dashboard through MQTT.

## Features

- RFID-based student and equipment identification
- Equipment checkout and return tracking
- OLED, LED, and buzzer feedback
- MQTT-based status and event updates
- Node-RED dashboard for equipment and activity monitoring
- CSV transaction logging

## Tech Stack

`ESP32` · `C++` · `MFRC522` · `SSD1306 OLED` · `MQTT` · `HiveMQ` · `Node-RED` · `Wokwi`

## How It Works

1. Scan a registered student ID.
2. Scan an equipment tag within 10 seconds to check it out.
3. Scan the borrowed equipment tag again to return it.
4. View equipment status and transaction activity on the Node-RED dashboard.

## Run the Project

- **Simulation:** [Wokwi Project](https://wokwi.com/projects/476883545027002369)
- **Source Code:** [GitHub Repository](https://github.com/oNk2r/labdrop-iot-lab-checkout)

Run the ESP32 simulation in Wokwi, import the Node-RED flow, and configure both to use the same MQTT broker and topic root.


## Hardware

ESP32 DevKit, MFRC522 RFID reader, SSD1306 OLED, two LEDs, 220 Ω resistors, and a buzzer.

*Currently simulated in Wokwi.*

