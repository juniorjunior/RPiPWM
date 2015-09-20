# RPiPWM

## Building pwmdemo

* Make a build folder and change to that folder
* Run cmake against the RPiPWM folder: cmake <full/path/to/RPiPWM>
* Run make

## Usage

The pwmdemo app can be used in two primary modes:

* Interactive Mode - This mode displays a menu allowing input allowing the individual color/PWM levels to be adjusted as well as listening for UDP messages to change settings.
* Daemon Mode - This mode only listens for UDP messages to change settings.

## UDP Messages

Each message consists of a two byte (16 bits) command followed by the appropriate data.

| Name | Value | Description |
| :--- | ----: | :---------- |
| CMD_SETLEVELS | 0x01 | Message contains data to set one full color triplet (R,G,B) and rest value |
| CMD_AUTOPATTERN | 0x02 | Message contains data containing a ramp time along with NumColors number of color triplets to cycle between. |
| CMD_AUTODISABLE | 0x03 | Message contains only the command (no extra data) and stops any current auto-cycling pattern. |

### CMD_SETLEVELS
| Name | Description | Bits |
| :--- | :---------- | ---: |
| CMD  | This is the command action to take | 16 |
| RampTime | This is the time in milliseconds over which the color will be changed | 32 |
| Red  | This is the level for the "red" GPIO pin. Values from 0.0 to 1.0 | 64 |
| Green  | This is the level for the "green" GPIO pin. Values from 0.0 to 1.0 | 64 |
| Blue  | This is the level for the "blue" GPIO pin. Values from 0.0 to 1.0 | 64 |

### CMD_AUTOPATTERN

The CMD, RampTime and NumColors value/bits are at the head of the message. The number of color triplets (R, G, B, RestTime) passed must be equal to NumColors.

| Name | Description | Bits |
| :--- | :---------- | ---: |
| CMD  | This is the command action to take | 16 |
| RampTime | This is the time in milliseconds over which the color will be changed | 32 |
| NumColors | This is the number of color triplets in the message | 16 |
| Red  | This is the level for the "red" GPIO pin. Values from 0.0 to 1.0 | 64 |
| Green  | This is the level for the "green" GPIO pin. Values from 0.0 to 1.0 | 64 |
| Blue  | This is the level for the "blue" GPIO pin. Values from 0.0 to 1.0 | 64 |
| RestTime | This is the time in milliseconds to hold on this color after ramping | 32 |

### CMD_AUTODISABLE

| Name | Description | Bits |
| :--- | :---------- | ---: |
| CMD  | This is the command action to take | 16 |
