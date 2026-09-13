# Differences to jb-xyz's openGBW:
 Extends https://github.com/jb-xyz/openGBW

 - Commented the code and added some more structure to it all
 - Cup weight now has an escape to it as well as a confirmation screen to subvert the escape
 - Info menu to see offset and cup weight
 - Wake on rotary turn
 - Sleep adjustable via menus instead of code compile
 - Two dosing cups: grinding starts when either of them is placed on the scale
 - Calibration by adjusting the scale factor directly while watching the live weight
 - Grinding aborts on timeout, missing progress, removed cup or scale errors, with the reason on the display
 - Debug menu with a weight chart and a history of the last grinds (duration and offset)
 - Games menu with two games controlled by the rotary encoder and the pressure on the scale

# OpenGBW

This Project extends and adapts the original by Guillaume Besson

More info: https://besson.co/projects/coffee-grinder-smart-scale


This mod will add GBW functionality to basically any coffe grinder that can be started and stopped manually.

The included 3D Models are adapted to the Eureka Mignon XL, but the electronics can be used for any Scale.

-----------

### Differences to the original

- added a rotary encoder to select weight and navigate menus
- made everything user configurable without having to compile your custom firmware
- dynamically adjust the weight offset after each grind
- added relay for greater compatibility
- added different ways to activate the grinder
- added scale only mode

-----------

### Getting started

1) 3D print the included models for a Eureka Mignon XL or design your own
2) flash the firmware onto an ESP32
3) connect the display, relay, load cell and rotary encoder to the ESP32 according to the wiring instructions
4) go into the menu by pressing the button of the rotary encoder and set your initial offset. -2g is a good enough starting value for a Mignon XL
5) if you're using the Mignon's push button to activate the grinder set grinding mode to impulse. If you're connected directly to the motor relay use continuous.
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

-----------

### Wiring

#### Load Cell

| Load Cell  | HX711 | ESP32  |
|---|---|---|
| black  | E-  | |
| red  | E+  | |
| green  | A+  | |
| white  | A-  | |
|   | VCC  | VCC/3.3 |
|   | GND  | GND |
|   | SCK  | GPIO 18 |
|   | DT  | GPIO 19|

#### Display

| Display | ESP32 |
|---|---|
| VCC | VCC/3.3 |
| GND | GND |
| SCL | GPIO 22 |
| SDA | GPIO 21 |

#### Relay

| Relay | ESP32 | Grinder |
|---|---|---|
| + | VCC/3.3 | |
| - | GND | |
| S | GPIO 25 | |
| Middle Screw Terminal | | push button |
| NO Screw Terminal | | push button |

#### Rotary Encoder

| Encoder | ESP32 |
|---|---|
| VCC/+ | VCC/3.3 |
| GND | GND |
| SW | GPIO 34 |
| DT | GPIO 23 |
| CLK | GPIO 32 |

-----------

### BOM

1x ESP32  
1x HX711 load cell amplifier  
1x 0.9" OLED Display  
1x KY-040 rotary encoder  
1x 500g load cell 60 x 15,8 x 6,7 https://www.amazon.de/gp/product/B019JO9BWK/ref=ppx_yo_dt_b_asin_title_o02_s00?ie=UTF8&psc=1  

various jumper cables  
a few WAGO or similar connectors

-----------

### 3D Files

You can find the 3D STL models on thangs.com

Eureka XL: https://thangs.com/designer/jbear-xyz/3d-model/Eureka%20Mignon%20XL%20OpenGBW%20scale%20addon-834667?manualModelView=true

These _should_ fit any grinder in the Mignon line up as far as I can tell.

There's also STLs for a universal scale in the repo, though it is mostly meant as a starting off point to create your own. You can use the provided files, but you'll need to print an external enclosure for the ESP32, relay and any other components your setup might need.

### Todo:

- ~add option to change grind start/stop behaviour. Right now it pulses for 50ms, this works if its hooked up to the push button of a Eureka grinder. Other models might need constant input while grinding~ done
- add mounting options and cable routing channels to base
- more detailed instructions (with pictures!)
- other grinders?
- ???