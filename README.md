# OpenGBW for the Eureka Mignon MCI

A grind by weight scale for the Eureka Mignon MCI. It works without a relay and should also work with other Mignon grinders. My build uses a 1kg load cell.

-----------

### Differences

Changes on top of the version this is based on (see [Origin](#origin)):

- **Games:** Comet Blaster and Curve Tracer, controlled with the knob and the pressure on the scale
- **Grinding:** two dosing cups; aborts on timeout (60 seconds), missing progress, a removed cup or a scale error, with the reason shown on the display; no early stop caused by vibration spikes and no interruption by the display timer
- **Calibration:** "Scale Factor" menu to calibrate while watching the live weight (replaces the calibration with a 100g weight), scale factor shown in the Info Menu
- **Operation:** weights aligned on the decimal point, reworked menu (Exit first, cup weight menus can be left by turning), reversed encoder direction, continuous as default grinding mode, fixed sleep timer that is kept after a restart and wakes the display when the scale is tapped
- **Debug Menu:** Weight Chart of the last 100 readings, Weight History of the last 10 grinds
- **Defaults for my setup:** cup weights 396.1g and 76.3g, scale factor 1760 (1kg load cell), dose 17.5g, offset -1.67g, grinder on GPIO 25

-----------

### Hardware on the Mignon MCI

**Power supply:** my version of the MCI has a 3.0V output, which I use to power the ESP32. It works, even though the ESP32 actually needs 3.3V. WiFi does not work at this voltage, though, which is why this version is not based on SyButter's latest state with WiFi and web server (see [Origin](#origin)).

**Starting the grinder:** the grinder is started by the switch at the top, below the coffee outlet, which carries a 12V signal. The ESP32 is wired in parallel to this switch: it drives a transistor from GPIO 25 through a small resistor, and the transistor switches the 12V signal just like the switch does. No relay is needed.

**Manual mode:** openGBW only works when the MCI is set to manual mode. The grinder can still be used as before after installing openGBW: manually with the switch or with the timer.

Opening the grinder and working on its electronics is at your own risk.

-----------

### Origin

This project has been passed on through several forks:

1. **Guillaume Besson ([geekuillaume](https://github.com/geekuillaume/coffee-grinder-smart-scale))** built the original coffee grinder smart scale. More info: https://besson.co/projects/coffee-grinder-smart-scale
2. **[jb-xyz/openGBW](https://github.com/jb-xyz/openGBW)** adapted it as openGBW: rotary encoder to select the weight and navigate menus, all settings configurable without compiling your own firmware, dynamic adjustment of the offset after each grind, relay support, different ways to activate the grinder, scale only mode and 3D models for the Eureka Mignon XL.
3. **[SyButter/openGBW](https://github.com/SyButter/openGBW)** commented and restructured the code and added a confirmation screen and escape for the cup weight, the Info Menu, wake on rotary turn, the sleep timer in the menu, the shot counter and the Debug Menu.
4. **This version** is based on SyButter's state of December 26, 2024 (commit `becc51d`), not on his latest state. His later changes (WiFi and web server, Seeed Studio XIAO ESP32-C3, PCB, switch start) are not included, mainly because WiFi does not work with the 3.0V supply of the MCI (see [Hardware](#hardware-on-the-mignon-mci)). On top of that come the [Differences](#differences) listed above.

-----------

### Getting started

1) 3D print the included models for a Eureka Mignon XL or design your own
2) flash the firmware onto an ESP32
3) connect the display, load cell and rotary encoder to the ESP32 and connect the grinder (on the Mignon MCI see [Hardware](#hardware-on-the-mignon-mci), other grinders may need a relay). The pins are defined in `include/config.hpp`
4) go into the menu by pressing the button of the rotary encoder and set your initial offset. -2g is a good enough starting value for a Mignon XL
5) set the grinding mode: on the Mignon MCI use continuous (the default) and set the grinder to manual mode (see [Hardware](#hardware-on-the-mignon-mci)). Use impulse if your grinder needs a short pulse to start and another one to stop.
6) if you only want to use the scale to check your weight when single dosing, set scale mode to scale only. This will not trigger any relay switching and start a timer when the weight begins to increase. If you'd like to build your own brew scale with timer, this is also the mode to use.
7) calibrate your load cell: open "Scale Factor", place a known weight (e.g. 100g) on the scale and turn the knob until the live weight matches, then press to save
8) set your dosing cup weights: open "Cup Weight 1", place the empty cup on the scale and press. Repeat with "Cup Weight 2" if you use a second cup
9) exit the menu, set your desired weight and place one of your empty dosing cups on the scale. The first grind might be off by a bit - the accuracy will increase with each grind as the scale auto adjusts the grinding offset

-----------

### Usage

#### Main screen

Turn the knob to set the target weight. Place an empty dosing cup on the scale: as soon as it has rested on the scale for one second within 10g of one of the two saved cup weights, the grinder starts. After grinding the display shows the final weight and the grinding time. Remove the cup to get back to the main screen.

#### Grinding aborts

The grinder is stopped and the reason is shown when

- grinding takes longer than 60 seconds ("Timeout")
- the weight does not increase by at least 1g within 2 seconds, checked from 10 seconds after the start ("No progress")
- the cup is lifted off the scale ("Cup removed")
- the load cell stops responding ("Scale error")

Press the knob to leave the error screen.

#### Menu

Press the knob on the main screen to open the menu, turn to select and press to open an item.

| Item | Function |
|---|---|
| Exit | back to the main screen |
| Cup Weight 1 / Cup Weight 2 | place the empty cup and press to save its weight, turn to leave without saving |
| Scale Factor | turn to change the calibration factor while watching the live weight, press to save |
| Offset | how early the grinder stops before the target weight; adjusted automatically after each grind |
| Scale Mode | GBW (default) or scale only (no grinder control, timer starts when the weight increases) |
| Grinding Mode | continuous (relay stays closed while grinding) or impulse (short pulse to start and stop) |
| Info Menu | cup weights, offset, scale factor and shot count |
| Sleep Timer | time without activity until the display turns off (default 60 seconds). Tapping the scale, turning or pressing the knob wakes it without changing anything |
| Reset | restore all settings to their defaults |
| Games | see below |
| Debug Menu | only visible in debug mode, toggled by pressing the knob four times quickly |

#### Debug Menu

| Item | Function |
|---|---|
| Sim Grind | simulates a grind without the grinder |
| Weight Chart | graph of the last 100 scale readings |
| Weight History | shot number, grinding time and used offset of the last 10 grinds (kept after power off), turn to scroll |
| Zero Shot Count | resets the shot counter to 0 |

#### Games

Both games use the scale as a pressure sensor: remove the cup and press on the scale with your finger. Press the knob during a game to pause or exit. The best score of each game is saved.

- **Comet Blaster**: comets fly in from the right. Turn the knob to move your ship and press the scale to fire the laser: the harder you press, the wider and stronger the beam, but the more energy it uses. Comets have to be hit near their center and give points and energy, bigger comets give more. A collision costs a life; hearts flying by give extra lives, but only if you fly into them - the laser destroys them.
- **Curve Tracer**: a line scrolls in from the right. The pen follows the pressure on the scale - the more you press, the higher it goes. Stay on the line to score points and build up a multiplier, leaving it drains the grip bar. The line gets faster and the curves get steeper over time.
