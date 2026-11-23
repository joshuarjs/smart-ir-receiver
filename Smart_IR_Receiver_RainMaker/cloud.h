#pragma once
#include <Arduino.h>

void cloudBegin();                  // init node + 8 contact sensors, start RainMaker, SoftAP provisioning if needed
void cloudTick();                   // report sensor changes (every loop pass, outside the web lock)
const char* cloudStatus();          // "Connected", "Connecting", "Provisioning", "No Wi-Fi"
