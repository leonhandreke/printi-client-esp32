## Run

To flash using USB:

	pio run --target upload -e lolin_s2_mini-usb

Because we need the USB port to talk to the printer, the ESP32-S2 needs to be put into bootloader mode for flashing by holding the BOOT button while pressing RESET. For more information, see https://docs.espressif.com/projects/esp-idf/en/latest/esp32s2/api-guides/dfu.html.



## HTTPS OTA

The printis of this world try to get new firmware from the highly optimized global printi CDN at startup. To release:

	pio run -e lolin_s2_mini-usb && scp .pio/build/lolin_s2_mini-usb/firmware.bin ndreke.de:~/www/dump/printi-firmware.bin

Note that the printi will flash itself only if the version tag on the server is different from the one currently running. The version tag is added to the firmware and derived from the latest git commit hash. For more details, see the PlatformIO config file.

## How to build a printi

If you want to build one more or less exactly like Leon did, you will need:

- a HOIN HOP-H58 receipt printer (less than 30€)
- a LD1117V33 voltage regulator
- a [Lolin S2 Mini board](https://www.wemos.cc/en/latest/s2/s2_mini.html)
* a USB-C to USB-A OTG adapter
- wires, soldering iron, screwdriver, multimeter, hotglue --- the usual stuff.

All in all, if you build 10 of these printers, each can be had for <30€.

First, open the printer and solder wires to the 12V power lines. I forget which one is which, just measure:

![Power wire 1](/doc/images/power_1.jpeg)
![Power wire 2](/doc/images/power_2.jpeg)

Solder the two wires to the correct pins on the voltage regulator, and drill a hole for the the wires with the resulting 3.3V from the regulator. Note how I tied a little knot so that the wires won't be torn off from the regulator in time. I secured the regulator with hot glue.

![Regulator installation](/doc/images/regulator.jpeg)

Solder the 3.3V power lines to the ESP32 printi brain on the outside. Measure before you solder, reversing the voltage will fry the ESP32 (yes, I did).

![Printi brain](/doc/images/brain.jpeg)

Finally, flash the firmware (unplug the printi before!), connect up the printer with the OTG adapter and wait for instructions to be printed. Happy printi printi!

![Assembly done](/doc/images/assembled.jpeg)
