# RPiPWM

This application was initially started to get a feel for using Pi-Blaster based PWM on the RaspberryPi. It worked so well that I ended up expanding things to work not only as an example of possible uses, but as a listening daemon for remote controlling the PWM pins. The code was written with Adafruit's analog LED strips in mind, but should be easily adaptable to any PWM functionality. [Pi-Blaster](https://github.com/sarfata/pi-blaster) is required and must be running in order to function. **Caveat:** This code is not hardened to attack and I do not recommend exposing the UDP interface to outside traffic without significant additional data integrity checks and rate limiters.

## Building pwmdemo

* Make a build folder and change to that folder
* Run cmake against the RPiPWM folder: cmake /full/path/to/RPiPWM
* Run make

## Usage

The pwmdemo app can be used in different modes:

* Interactive Mode - This mode displays a menu allowing input allowing the individual color/PWM levels to be adjusted as well as listening for UDP messages to change settings.
* Daemon Mode - This mode only listens for UDP messages to change settings. (command line option --daemon)
* Testing Mode - This mode causes the app to bind to /dev/null instead of /dev/pi-blaster which can be useful while testing. (command line option --test)

## Setup

* GPIO Pins - The code has three pins (defaults to 23, 24, 25) set up as Red, Green, and Blue respectively. These are the GPIO numbers not the connector pin numbers. They are set at the top of the source and do require a recompile if changed.

## UDP Messages

Each message consists of a one byte (8 bits) command followed by the appropriate data.

| Name | Value | Description |
| :--- | ----: | :---------- |
| CMD_SETLEVELS | 0x01 | Message contains data to set one full color triplet (R,G,B) and rest value |
| CMD_AUTOPATTERN | 0x02 | Message contains data containing a ramp time along with NumColors number of color triplets to cycle between. |
| CMD_AUTODISABLE | 0x03 | Message contains only the command (no extra data) and stops any current auto-cycling pattern. |

### CMD_SETLEVELS
| Name | Description | Type | Bits |
| :--- | :---------- | :--- | ---: |
| CMD  | This is the command action to take | Unsigned Char | 8 |
| RampTime | This is the time in milliseconds over which the color will be changed | Unsigned Int | 32 |
| Red  | This is the level for the "red" GPIO pin. Values from 0.0 to 1.0 | Double | 64 |
| Green  | This is the level for the "green" GPIO pin. Values from 0.0 to 1.0 | Double | 64 |
| Blue  | This is the level for the "blue" GPIO pin. Values from 0.0 to 1.0 | Double | 64 |

### CMD_AUTOPATTERN

The CMD, RampTime and NumColors value/bits are at the head of the message. The number of color triplets (R, G, B, RestTime) passed must be equal to NumColors.

| Name | Description | Type | Bits |
| :--- | :---------- | :--- | ---: |
| CMD  | This is the command action to take | Unsigned Char | 8 |
| RampTime | This is the time in milliseconds over which the color will be changed | Unsigned Int | 32 |
| NumColors | This is the number of color triplets in the message | Unsigned Char | 8 |
| Red  | This is the level for the "red" GPIO pin. Values from 0.0 to 1.0 | Double | 64 |
| Green  | This is the level for the "green" GPIO pin. Values from 0.0 to 1.0 | Double | 64 |
| Blue  | This is the level for the "blue" GPIO pin. Values from 0.0 to 1.0 | Double | 64 |
| RestTime | This is the time in milliseconds to hold on this color after ramping | Unsigned Int | 32 |

### CMD_AUTODISABLE

| Name | Description | Type | Bits |
| :--- | :---------- | :--- | ---: |
| CMD  | This is the command action to take | Unsigned Char | 8 |
