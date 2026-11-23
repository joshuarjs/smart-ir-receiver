#!/bin/bash
# Compile with arduino-cli for the C6 (CHIP=esp32c3 tools/build.sh for a C3) (see dev-env.sh). The .ino body is moved into a .cpp so the
# IDE's automatic prototypes aren't needed. Output: /home/claude/build/out/Smart_IR_Receiver_RainMaker.ino.bin
set -e
SRC=$(cd "$(dirname "$0")/.." && pwd); B=/home/claude/build
rm -rf $B && mkdir -p $B && cp -r $SRC $B/Smart_IR_Receiver_RainMaker && rm -rf $B/Smart_IR_Receiver_RainMaker/tools
cd $B/Smart_IR_Receiver_RainMaker && { echo '#include <Arduino.h>'; cat Smart_IR_Receiver_RainMaker.ino; } > main_build.cpp && echo '// build shim' > Smart_IR_Receiver_RainMaker.ino
export ARDUINO_CONFIG_FILE=/home/claude/ard/cli.yaml
/home/claude/tools/arduino-cli compile --fqbn esp32:esp32:${CHIP:-esp32c6}:CDCOnBoot=cdc,PartitionScheme=rainmaker --output-dir $B/out $B/Smart_IR_Receiver_RainMaker 2>&1 | grep -iE "error|warning: |Sketch uses|Global var" | grep -v "Error initializing\|Downloading index"
