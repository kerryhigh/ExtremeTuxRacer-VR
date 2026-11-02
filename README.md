# Extreme Tux Racer VR

**Download, install guide, controls and pictures: [portroyale.online/extremetuxracer.html](https://portroyale.online/extremetuxracer.html)**

[![Sponsor](https://img.shields.io/badge/Sponsor-SgtBilko76-ea4aaa?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/SgtBilko76)

[Extreme Tux Racer](https://sourceforge.net/projects/extremetuxracer/) running standalone on Meta Quest and Android phones with Google Cardboard. Race Tux the penguin down icy slopes and grab herrings along the way.

- The Quest flavor is a native OpenXR app; the Cardboard flavor is a separate native Android app with the Cardboard SDK.
- Each race is simulated once per frame and rendered once per eye. Menus stay on the floating screen and use the connected controller.
- A GLES 3 compatibility layer runs the game's original OpenGL 1.2 renderer unchanged.

The Android project has separate `quest` (OpenXR) and `cardboard` product flavors. The Cardboard flavor uses the official Google Cardboard SDK for viewer parameters, 3DOF head tracking, eye projections, and lens distortion; it does not load an OpenXR runtime. Both flavors package the game data and unpack it on first launch.

## Install

### Meta Quest

1. Enable developer mode on your Quest.
2. Download the APK from the [Releases](../../releases) page.
3. Sideload it with [SideQuest](https://sidequestvr.com/) or `adb install -r <file>.apk`.
4. Launch it from *Library → Unknown Sources*.

### Android phone with Google Cardboard

1. Download `etr-cardboard-debug.apk` from the `extreme-tux-racer-cardboard` GitHub Actions artifact, or build it with the instructions below.
2. Install it with `adb install -r etr-cardboard-debug.apk` or your preferred APK installer.
3. Launch **Extreme Tux Racer Cardboard** from the Android app drawer. It starts in the original player/menu flow; use the Xbox controller to choose **Practice** or an event and navigate the floating menu.
4. Insert the phone into the viewer after choosing a course. The Cardboard SDK scans the viewer QR code on first launch and stores its profile; if you skip scanning, startup uses the SDK's standard Cardboard V1 fallback.

The game data is unpacked to the app's internal storage on first launch.

## Controls

![Extreme Tux Racer VR controller mapping](docs/controls.webp)

### Google Cardboard and Xbox controller

| Xbox control | Action |
| --- | --- |
| Left stick horizontal | Steer Tux |
| RT | Paddle / accelerate |
| LT | Brake |
| Hold A, then release | Charge and perform a jump |
| LB | Trick modifier |
| LB + left stick left/right | Roll left/right while airborne |
| LB + RT | Front flip while airborne |
| LB + LT | Back flip while airborne |
| RB | Recenter head orientation |
| Y | Cycle the three existing camera modes |
| X | Toggle HUD |
| Menu / Start | Pause during a race |
| View / Select | Restart through the game's existing confirmation screen |
| B | Back / cancel in menus |
| D-pad or left stick | Navigate menus |
| A | Confirm menu selection |

Rolls and flips can be combined, preserving the original trick controls. The right stick is unused.

### Cardboard viewer calibration

On a fresh install, the Cardboard SDK opens its viewer QR scanner. Grant camera access and scan the code printed on the viewer; the SDK saves its profile for later launches and rebuilds the eye projections and distortion mesh from those parameters. If you skip scanning, the game uses the SDK's Cardboard V1 fallback profile. To scan a different viewer later, hold **Menu + View** for about one second while on a game menu. The SDK scanner has its own camera permission prompt and scan/skip screen.

## Build and install the Cardboard APK

Requirements: Android SDK platform/build tools 35, Android NDK `27.2.12479018` and `29.0.14206865`, CMake `3.22.1`, and JDK 17. The Gradle wrapper stages all files from `data/` automatically.

On Windows PowerShell, from `android/`:

```powershell
.\gradlew.bat assembleCardboardDebug
```

On Linux or macOS, from `android/`:

```sh
./gradlew assembleCardboardDebug
```

The installable debug APK is `android/build/outputs/apk/cardboard/debug/etr-cardboard-debug.apk` (ARM64, min Android 10 / API 29, landscape, target API 35). Install it with:

```sh
adb install -r android/build/outputs/apk/cardboard/debug/etr-cardboard-debug.apk
```

The [Android Cardboard workflow](.github/workflows/android-cardboard.yml) also builds this APK and uploads it as a GitHub Actions artifact for 30 days on pushes, pull requests, and manual runs. Download the `extreme-tux-racer-cardboard` artifact from the workflow run and install the APK inside it.

To build the Quest flavor instead, run `assembleQuestDebug`; both variants share the gameplay and GLES code, but only the Quest flavor carries the OpenXR loader and Quest manifest categories.

## Compatibility notes

- Designed for 64-bit Android phones with GLES 3; the requested Galaxy S21 Plus / Android 15 target is covered by the API levels and ARM64 ABI.
- The Cardboard build starts in the original player/menu flow, while Quest retains its direct practice-race startup. Cardboard menus and HUD use the existing bold sans face and 1.5x font sizing for the floating screen.
- Gameplay and assets are local after install. QR scanning uses the SDK's camera scanner; Google Play services may need to provide its barcode detector the first time it runs.
- If no viewer profile is saved, the built-in Cardboard V1 fallback allows startup, but a scanned profile is needed for best lens alignment and eye separation.
- A Galaxy S21 Plus / Android 15 smoke check verified app startup, stereo menus, gamepad-source menu navigation, course selection, and entry to a race. Actual Xbox-button/trigger feel, QR scan alignment, headset comfort, and sustained frame pacing still need hands-on testing with the target Xbox controller and Cardboard viewer.
- The APK builds and its manifest/native payload are checked in CI.
- There is no flat-screen diagnostic flavor yet.

## Building

The Quest and Cardboard builds live in `android/` (Gradle + CMake, arm64-v8a, minSdk 29). The desktop build is unaffected. `vr-probe/` is a minimal standalone OpenXR app for checking the toolchain.

---

Original project: http://sourceforge.net/projects/extremetuxracer/
