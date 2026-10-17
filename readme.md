
# Raspberry Pi Zero 2 W IMX462 Camera with Betaflight OSD

## Overview

This project implements an IMX462 camera system based on the Raspberry Pi Zero 2 W. It overlays full-color Betaflight OSD graphics onto live camera footage and outputs the combined video as an analog CVBS signal.

## Features

- **Raspberry Pi Zero 2 W** — Compact embedded computing platform.
- **IMX462 Camera** — Captures live video using the IMX462 image sensor.
- **Betaflight MSP DisplayPort Support** — Receives OSD data from Betaflight via the MSP DisplayPort protocol.
- **Full-Color OSD** — Displays colorful OSD characters and graphics over the camera image.
- **Customizable OSD Font** — Uses a full-color BMP image as the OSD character set, making it easy to replace or customize the font.
- **CVBS Video Output** — Outputs the combined camera image and full-color OSD as an analog CVBS signal.

## Requirements

- Raspberry Pi Zero 2 W.
- IMX462 camera module.
- A flight controller running Betaflight.
- Betaflight OSD configured in **HD mode**.
