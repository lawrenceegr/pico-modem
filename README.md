# pico-modem (development status: in progress)

## Getting Started

### Prerequisites

This project requires the Raspberry Pi Pico SDK to build. Before proceeding, please follow the official Pico SDK documentation to install and set up the SDK on your system:

**Official Pico SDK Setup Guide:** https://www.raspberrypi.com/documentation/microcontrollers/c_sdk.html#sdk-setup

### Setting Up the Project

Once you have the Pico SDK installed, you can build this project from anywhere on your computer by setting the `PICO_SDK_PATH` environment variable to point to your SDK installation.

#### Option 1: Set PICO_SDK_PATH environment variable

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
```

Add this line to your shell configuration file (e.g., `~/.bashrc`, `~/.zshrc`) to make it permanent.

#### Option 2: Pass PICO_SDK_PATH to CMake directly

```bash
cmake -DPICO_SDK_PATH=/path/to/pico-sdk -B build
```

### Building

Once the SDK path is configured:

```bash
mkdir build
cd build
cmake ..
make
```

The built `.uf2` file will be ready to flash to your Raspberry Pi Pico.
