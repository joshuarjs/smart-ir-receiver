# Working notes for agents

Smart IR Receiver: ESP32-C6 SuperMini (C3 still builds via config.h), arduino-esp32 3.3.x, RainMaker 4MB partition, IRremoteESP8266 2.9.0.
Built on the Sub Masker modules (httpsrv, netwatch, wifimgr, make_webui) per Josh's ESP32-C3 SuperMini project notes (they apply to the C6 too, except the C3-only low TX power).
- C6 pins: IR 20, hotspot button 5, status LED 15 (polarity per LED_ACTIVE_LOW), RGB LED 8 kept off. No hardware changes wanted.

- No Bluetooth. Never add WiFiProv BLE. SoftAP provisioning only as a fallback; boards are claimed with RMakerSetup.
- Keep RainMaker names: node "ESP RainMaker Node", devices "IR Remote Sensor N", param "IR Remote Button State" (true = open). Changing them breaks Alexa routines.
- Console = esp_http_server (httpsrv.*). State shared with handlers is touched only under webLock(); RainMaker reports go out in cloudTick() outside the lock.
- JSON for polled endpoints goes through JsonW into g_jsonBuf, never String concatenation.
- Page: edit webui.html / hotspot.html, run tools/make_webui.py (gzips into webui.h). Fixed-height rows and message slots: nothing may shift when text changes. Check 360 px width.
- Preview: tools/sim/build.py -> /mnt/user-data/outputs/preview/index.html; sim.js mirrors ir.cpp's logic, keep them in step.
- Show Josh the preview before compiling UI changes. Build: tools/dev-env.sh once, then tools/build.sh.
- Releases: releases/v<ver>/ with -ota.bin and -full-flash-0x0.bin; zip named SmartIRReceiver-v<ver>.zip.
