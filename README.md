# homepi-plus
homepi+ is an Arduino based implementation of a home control system. It is an evolution of my previous project which accomplished a similar task and which was implemented on a Raspberry Pi. This time, everything is happening on a microcontroller, in order to reduce overhead and speed up execution. Also, advantages include easier communication with the hardware (for example, working with infrared LEDs does not involve messing with gigantic monoliths like the LIRC library on Linux). This is intended to be deployed to an Arduino Nano.

<img src="res\img0.png" alt="img0" style="zoom:50%;" />

## Functionality

Currently, this project interfaces with and allows control of the following:

* Connection to motherboard power headers in order to allow PC power cycle
* Connection to ThinkPad Ultra Dock in order to allow docked laptop power cycle
* Control of OSRAM LEDs which use the NEC infrared protocol
* Changing active source, volume, and brightness of my AOC U3277FWQ monitor by issuing DDC/CI commands over the D-Sub connector
* Control of a 2-5V-relay module
* (Deprecated) Control of Delock 18685 4K HDMI switch which uses the NEC infrared protocol

## Inner workings

In order to 'talk' with the Arduino, a web service is implemented. This leverages the ENC28J60 module; communication is done using the EtherCard library. This was chosen instead of UIPEthernet because it comes with a smaller footprint, especially on the flash memory side, which is almost maxed out by the latter library on the Nano.

In order to easily discover the service, the service exposes its presence via mDNS/Apple Bonjour. This has been achieved by slightly patching the EtherCard library in order to accommodate a dedicated mDNS library, EtherCard-MDNS. The EtherCard library is configured to automatically obtain an IPv4 address using DHCP. In order to provide a fallback mechanism, 2 strategies can be employed:

* Configure the DHCP server to reserve a particular IP address for the ENC28J60 on-board NIC (that's what I use at home, and how the project is configured currently)
* Statically assign an IP address to the ENC28J60 on-board NIC using the EtherCard library

There are other two libraries used with this: Wire, from the default Arduino libraries, in order to facilitate i2c communication with the monitor, via DDC/CI; and IRLib2, which is used to control the infrared lights that control the lightbulbs in my room.

## Compiling

This repository contains all the necessary info to clone and correctly compile this, by including specific versions of the libraries used in the project. To correctly cone this, do:

```bash
git clone https://github.com/valinet/homepi-plus
cd homepi-plus
git submodule init
git clone --depth=1 --no-checkout `git config submodule.src/ArduinoCore-avr.url` src/ArduinoCore-avr
git -C src/ArduinoCore-avr config core.sparsecheckout true
echo "libraries/Wire" >> src/ArduinoCore-avr/.git/info/sparse-checkout
git submodule absorbgitdirs src/ArduinoCore-avr
git submodule update --force --checkout src/ArduinoCore-avr
git clone `git config submodule.src/EtherCard-MDNS.url` src/EtherCard-MDNS
git submodule absorbgitdirs src/EtherCard-MDNS
git submodule update --force --checkout src/EtherCard-MDNS
git clone `git config submodule.src/EtherCard.url` src/EtherCard
git submodule absorbgitdirs src/EtherCard
git submodule update --force --checkout src/EtherCard
git clone `git config submodule.src/IRLib2.url` src/IRLib2
git submodule absorbgitdirs src/IRLib2
git submodule update --force --checkout src/IRLib2
```

This project compiles successfully using Arduino IDE 1.8.3. Future versions may work as well; to compile successfully, make sure you do not already have copies of the libraries used in this project already in your Documents/libraries folder (this is because of a quirk of the Arduino build system; otherwise, you will get linker errors because some translation units will be compiled twice). Please temporarily remove the libraries from your libraries folder when attempting to compile this from the Arduino IDE.