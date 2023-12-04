## Run

To flash using USB:

	pio run --target upload -e lolin_s2_mini-usb

## HTTPS OTA

The printis of this world try to get new firmware from the highly optimized global printi CDN at startup. To release:

	pio run -e lolin_s2_mini-usb && scp .pio/build/lolin_s2_mini-usb/firmware.bin ndreke.de:~/www/dump/printi-firmware.bin

Note that the printi will flash itself only if the version tag on the server is different from the one currently running. The version tag is added to the firmware and derived from the latest git commit hash. For more details, see the PlatformIO config file.
