---
name: android-ui-creator
description: >-
  A skill to search UI design examples, implement and build Android layout components,
  deploy them onto devices, automatically navigate and inspect screenshots to verify correctness,
  and notify the user via high-volume audio feedback when done.
---

# Android UI Creator

## Overview
This skill provides a structured workflow for designing new components or modifying and styling existing ones in Android applications. It automates testing deployments on a device/emulator and verifies correctness via captured screen references and UI hierarchy analysis. Once completed, it alerts the user with an audio completion prompt using the `espeak-ng` text-to-speech engine.

## Dependencies
- **android-cli**: For building/installing standard APK files.
- **adb**: Platform tools for android device connection, layout capture, and command-line execution.
- **espeak-ng**: For speech notifications.

## Quick Start
To trigger the speaker notification manually:
```bash
python3 /home/shado/.system/ui_helper.py speak --message "UI creation completed. Do you need anything more?"
```

To take a verification screenshot of the device:
```bash
python3 /home/shado/.system/ui_helper.py screenshot --output screenshot.png
```

## Utility Scripts
The helper script `/home/shado/.system/ui_helper.py` provides the following commands:
- **`speak`**: Plays a text-to-speech confirmation at full volume.
  ```bash
  python3 /home/shado/.system/ui_helper.py speak [--message "Text to speak"]
  ```
- **`screenshot`**: Captures the current device screen and pulls the image file.
  ```bash
  python3 /home/shado/.system/ui_helper.py screenshot --output <path_to_image.png>
  ```
- **`dump-ui`**: Dumps the layout hierarchy layout of the active screen using uiautomator.
  ```bash
  python3 /home/shado/.system/ui_helper.py dump-ui --output <path_to_hierarchy.xml>
  ```

## Workflow

### 1. Research and Reference Search
- Search the web for target user UI designs or components.
- Inspect the gathered references to establish standard design/layout paradigms (colors, typography, margins).

### 2. Implement layout changes
- Create or update the target layout source code (e.g., Jetpack Compose components).
- Ensure all styles are integrated correctly and standard themes are referenced.

### 3. Build & Install UI
- Compile and install the debug APK using standard Gradle tools (`./gradlew installDebug` or the `build-install.sh` script).

### 4. Navigate & Verify UI on Device
- Use `adb` command shell scripts to navigate to the screen where the new layout is loaded.
- Capture a screenshot via:
  ```bash
  python3 /home/shado/.system/ui_helper.py screenshot --output verified_screen.png
  ```
- View the screenshot to verify that the visual elements render exactly as expected.
- If elements are hard to identify visually, dump the window hierarchy XML file to inspect layout coordinates:
  ```bash
  python3 /home/shado/.system/ui_helper.py dump-ui --output hierarchy.xml
  ```

### 5. Final Notification (Voice alert)
- Trigger the voice alert at maximum volume:
  ```bash
  python3 /home/shado/.system/ui_helper.py speak --message "UI creation completed. Do you need anything more?"
  ```

## Common Mistakes
- **No Active Device/Emulator**: Make sure an emulator is running or a device is connected via USB debugging before calling screenshot or dump-ui command.
- **Audio Output Muted**: Ensure the system host audio configuration or VM audio pass-through is active to hear the espeak voice.
