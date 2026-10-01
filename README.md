# Alt-Az Autonomous Field De-Rotator Pipeline (v1.0.0)

A lightweight, high-precision, **dependency-free** C++14 mathematical de-rotation engine designed to drive the **ZWO Camera Angle Adjuster (CAA)** over USB on Alt-Az telescope configurations (such as GoTo Dobs, Meade LX200 setups, or iOptron Alt-Az trackers).

## 🌌 Hardware Independence Note
Because this software computes all astronomical field trajectories **natively on your computer's CPU** using high-precision equatorial calculus (Hour Angle and Declination matrices), **it functions completely standalone without needing network telemetry from an ASIAIR, INDI, or ASCOM server.** 

As long as your telescope mount (whether an old Meade LX200, a Sky-Watcher GoTo Dobsonian, or a modern multi-axis tracker) is successfully tracking a celestial object, this software will perfectly de-rotate your camera sensor over USB.

---

## 📦 Host System Prerequisites & Dependencies

To execute headless background master-frame stacking and real-time flat calibrations, your host Linux machine must have the following system libraries and binaries installed.

### 1. Core Operating System Packages
Install the required standard compilation utilities and network storage tools via your terminal:
```bash
sudo apt-get update
sudo apt-get install -y build-essential g++ libusb-1.0-0-dev cifs-utils smbclient
```

### 2. Astrophotography Processing Engine (Siril v1.4.0+)
This pipeline utilizes Siril's native command-line interface framework to execute noise reduction stacking and pixel interpolation rotations. Ensure you are running Siril version **1.4.0-rc2 or newer**:
```bash
sudo apt-get install -y siril
```
*Note: The application binary includes an built-in dependency check at startup that will automatically warn you and halt deployment if `siril` is missing from your system environment paths.*

### 3. Native Hardware Library Drivers
Ensure that ZWO's factory header file (`CAA_API.h`) and its static library binary signature archive (`libCAA.a`) are placed directly inside your project's root build folder before running compilation strings.

### 4. ZWO Camera Angle Adjuster Hardware Requirements
This application interacts directly with the rotator's firmware over USB. The following hardware components and driver links are mandatory:

* **Physical Device:** ZWO Camera Angle Adjuster (CAA) connected via a high-quality USB 2.0/3.0 cable directly to your Linux laptop.
* **Power Delivery:** Ensure the CAA has a stable 12V DC power supply attached. Powering the device solely off a laptop USB port can cause voltage drops during continuous micro-stepping loops, leading to dropped connection handles.
* **Dynamic/Static SDK Drivers:** You must download the official **ZWO CAA SDK** from the [ZWO Developer Software Suite](https://zwoastro.com). 
  * Extract the files and place **`CAA_API.h`** and **`libCAA.a`** (from the `lib/x64` Linux folder) directly into your local repository build folder.

** NOTE WELL:  This project uses the CAA_API v. 1.5.9.  There is an interface churn safety check at compile time to warn if you are using a (potentially) incompatible version.


## 🛠️ Project Workspace Architecture
Ensure your local project execution folder matches this layout:

```text
📁 project_root/
│
├── 📄 main.cpp                 <-- Central session scheduler watchdog loop
├── 📄 tracking_engine.cpp       <-- Precision 4-Quadrant Equatorial math engine
├── 📄 setup_manager.cpp         <-- Configuration script auto-generator module
├── 📄 pipeline_core.h          <-- Data structures & coordinate transformation headers
├── 📄 script_template.txt       <-- Raw Bash architecture script configuration matrix
│
├── 📄 CAA_API.h                <-- ZWO Factory SDK Header
├── 💾 libCAA.a                 <-- ZWO Factory Static Archive Library
│
├── 📁 temp_bias/                <-- [User Input] Drop your raw .fit bias exposures here
├── 📁 temp_flats/               <-- [User Input] Drop your raw .fit flat exposures here
├── 📁 lights/                   <-- [User Input] Drop your final raw light exposures here
└── 📁 custom_flats/             <-- [Auto-Generated] App creates unique rotated flats here
```

---

## 🚀 Compilation Command (Native GCC One-Liner)
Strip away bulky compilation frameworks like Makefile or CMake. Compile your standalone tracking engine instantly with this single command-line execution:

```bash
g++ -std=c++14 main.cpp tracking_engine.cpp setup_manager.cpp -o rotator_pipeline -L. -lCAA -lpthread -ludev
```

---

## 📋 Standard Nightly Imaging Workflow

### Step 1: Capture Calibration Data
1. Cover your telescope optics and take **15–20 Bias frames** via your imaging software. Move the files into your laptop's local `./temp_bias/` folder.
2. Turn on your flat field panel and capture **15–20 Flat frames**. Move the files into your laptop's local `./temp_flats/` folder.

### Step 2: Initialize the Deployment Wizard
Run the compiled tracker application without arguments to generate your dynamic environment control script:
```bash
./rotator_pipeline
```

### Step 3: Run the Launch Script
Launch the generated script file via your terminal window:
```bash
./launch_pipeline.sh
```
1. Input your active **Site Location Latitude and Longitude** coordinates (West values must be written as negative parameters).
2. Input your camera resolution parameters and exposure duration settings (e.g., 90).
3. **Automated Master Processing:** The script will headlessly command Siril to stack your raw files into a pristine `master_bias.fits` and `master_flat_0deg.fits` right inside your root directory.
4. Input your celestial object's target **Right Ascension (Decimal Hours)** and **Declination (Decimal Degrees)** parameters when prompted.

---

## 🎯 Best Practice: How to Launch Your Imaging Session

To guarantee pixel-perfect star roundness on your very first exposure sub-frame, always implement this chronological staging timeline:

1. **Target and Center:** Slew your telescope mount to your deep-sky object using your hand controller or software panel. Execute a plate-solve to ensure your target is dead-center.
2. **Engage Mount Tracking:** Ensure your telescope mount is actively tracking the sky. If you are autoguiding, let your tracking graph stabilize completely.
3. **Launch the Rotator Pipeline FIRST:** Trigger your target coordinates inside the `./launch_pipeline.sh` terminal loop.
   * *Why:* The C++ program will instantly calculate the sky geometry and execute its **Initial Pre-Positioning Pass**, moving the ZWO CAA rotator to the exact mathematical midpoint angle of the oncoming exposure before the camera begins imaging.
4. **Launch Your Camera Exposure Sequence Second:** Wait roughly 5 to 10 seconds for the rotator to finish its initial pre-positioning step, then tap **Start** on your automated camera capture sequence (AutoRun, Plan, or sequence looping).

---

## 🗃️ Morning-After Calibration and Processing
When your imaging session completes, your raw **Light frames** are sitting on your camera controller storage media. 

1. Move your raw Light files into your laptop's local `./lights/` directory folder.
2. Open your automation pre-processing profile script: `siril -s execute_final_stack.ssf`.
3. Siril will automatically read your individual frame data, align the metadata profiles to your locally stored `./custom_flats/` files, pre-calibrate your arrays, register the star rotation trajectories, and export your finalized deep-sky stack as `processed/final_stacked_result.fits`.

