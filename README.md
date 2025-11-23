# DSpico USB Examples
This repository contains examples of using the DSpico USB port from a DS application.

The `examples` folder contains the following examples:
- usb-speaker - Makes the DS/DSi/3DS work as a USB speaker for your PC
- mass-storage - Allows access to the DSpico micro SD card over USB

The `platform` folder contains the tiny usb platform code for the DSpico.

> [!IMPORTANT]
> There is currently a bug in the DSpico Firmware that sometimes causes the DSpico the crash if you use USB, reset the console and use USB again. If this happens, unplug USB and reset the console such that the DSpico performs a power cycle. You can plug the USB back in afterwards.

## Environment
The examples require a pre-calico devkitpro/libnds environment to build. With such an environment setup, just run `make` in the folder of the example to compile it.

## License
The platform code and examples are licensed under the Zlib license. For details, see `LICENSE.txt`.
