# Radiomobile Signal Detection Using ESP32

##  Project Overview

This project was developed as part of a seminar on **Radiomobile Signal Detection**.

The main objective is to investigate the detection of signals emitted by smartphones inside a classroom using an **ESP32**. The system focuses mainly on **WiFi and Bluetooth Low Energy (BLE)** signals.

The project aims to detect the presence of nearby devices, measure their signal strength, estimate their approximate distance, and investigate possible localization techniques.

##  Objectives

The project aims to:

* Detect nearby smartphones using WiFi signals.
* Detect nearby devices using Bluetooth Low Energy (BLE).
* Measure the **RSSI (Received Signal Strength Indicator)**.
* Estimate the approximate distance between the ESP32 and the detected device.
* Investigate device localization using multiple ESP32 nodes.
* Study the limitations of smartphone signal detection.
* Explore the theoretical detection of cellular signals such as **3G and 4G**.

##  Technologies

### Hardware

* ESP32
* Smartphones
* WiFi/Bluetooth antennas integrated into the ESP32

### Wireless Technologies

* WiFi — 2.4 GHz
* Bluetooth Low Energy (BLE)
* 3G / 4G — theoretical study

### Software

* Arduino IDE
* C/C++
* ESP32 WiFi and BLE libraries

##  Repository Structure

```text
Seminaire-Detection-Radiomobile/
│
├── README.md
│
├── WiFi/
│   ├── calibration_wifi.ino
│   └── detection_wifi.ino
│
├── BLE/
│   ├── calibration_ble.ino
│   └── detection_ble.ino
│
└── Combined/
    └── wifi_ble.ino
```

##  WiFi Detection

The ESP32 is configured in **promiscuous mode** to capture WiFi management frames, particularly **Probe Request** frames.

The system extracts information such as:

* MAC address
* RSSI
* WiFi channel
* Requested SSID, when available

The RSSI value is then used to estimate the approximate distance of the detected device.

A calibration procedure is performed by placing a smartphone at a known distance of **1 meter** from the ESP32.

##  Bluetooth Detection

The ESP32 is also used as a BLE scanner to detect nearby Bluetooth devices.

An **active BLE scan** is used to improve detection of smartphones during normal operation.

The system can obtain information such as:

* Device address
* RSSI
* Device name, when available
* Manufacturer information
* Available BLE services, when provided

A separate calibration procedure is used for BLE because its reference RSSI differs from WiFi.

##  RSSI-Based Distance Estimation

The distance estimation is based on a logarithmic path-loss model:

```text
d = 10^((RSSI_d0 - RSSI) / (10 × n))
```

where:

* `d` is the estimated distance.
* `RSSI_d0` is the RSSI measured at a reference distance of 1 meter.
* `RSSI` is the measured signal strength.
* `n` is the path-loss exponent.

The reference RSSI is obtained experimentally through calibration.

##  WiFi + BLE Integration

A combined version of the system was developed to perform WiFi and BLE detection using the same ESP32.

Since WiFi and BLE share the same physical 2.4 GHz radio on the ESP32, particular attention was given to **radio coexistence and scan timing**.

The combined implementation uses appropriate initialization and scanning parameters to allow both technologies to operate on the ESP32.

##  Limitations

Several limitations were identified during testing:

* RSSI-based distance estimation is only approximate.
* Signal strength is affected by walls, obstacles, people, antenna orientation, and multipath propagation.
* Smartphones may reduce or disable wireless transmissions to save energy.
* Modern smartphones use MAC address randomization, which prevents reliable long-term tracking of a specific device.
* WiFi Probe Requests are not continuously transmitted by all smartphones.
* BLE advertisements may be sporadic during normal smartphone operation.
* The ESP32 cannot directly decode 3G/4G cellular signals; their detection requires appropriate RF/SDR equipment.

##  Future Improvements

Possible future developments include:

* Deploying multiple ESP32 nodes for device localization.
* Improving RSSI calibration for the specific classroom environment.
* Implementing trilateration or fingerprinting techniques.
* Adding a graphical interface for real-time monitoring.
* Storing and visualizing detected devices and RSSI values.
* Using an SDR platform to investigate 3G/4G signal detection.

##  Project Context

**Seminar:** Radiomobile Signal Detection
**Platform:** ESP32
**Main Focus:** WiFi and BLE smartphone detection
**Development Environment:** Arduino IDE

---

> **Note:** This repository contains the source code developed for experimental and academic purposes as part of the seminar project.
