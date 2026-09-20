# Display simulator

Runs the display, menu and game code of the firmware on the computer and saves the content of the 128x64 display as PNG. The firmware sources are compiled unchanged against small stubs for Arduino, the load cell, the preferences and the rotary encoder. The knob, the button and the scale are driven by a script.

## Build

Needs a C++ compiler, make and zlib (included in macOS). U8g2 is taken from the PlatformIO libraries, so build the firmware once (`pio run`) or run `pio pkg install` first.

```sh
cd sim
make               # builds build/opengbw-sim
make screenshots   # runs screenshots.txt, the PNGs land in sim/screenshots
make readme-screenshots   # also copies the screenshots of the main README to screenshots/
```

## Usage

```sh
sim/build/opengbw-sim [-o dir] [-s scale] [-v] [-f script]... [command]...
```

- `-o dir` folder for the screenshots (default `sim/screenshots`)
- `-s scale` size of one display pixel in the PNG (default 4)
- `-v` print the serial output of the firmware
- `-f script` run the commands of a script file

Example: open the menu, go to the Info Menu and save it

```sh
sim/build/opengbw-sim "click; turn 7; click; shot info"
```

## Commands

Commands are separated by new lines or `;`, `#` starts a comment. A line `[name]` starts a section: every section runs from a freshly started firmware with its default settings. A section that does not use `boot` skips the initializing screen: it waits until the firmware has left it.

| Command | Effect |
| --- | --- |
| `wait <ms>` | time passes, the display is updated continuously |
| `turn <detents>` | turns the knob, positive moves down in the menus |
| `click` | presses and releases the button (followed by a short pause outside of games) |
| `hold <ms>` | holds the button down |
| `press`, `release` | presses or releases the button without time passing, for frame exact input |
| `weight <grams>` | reading of the scale |
| `noise <sigma>` | scatter in grams added to every raw reading from here on, `0` turns it off |
| `set <variable> <value>` | sets a firmware variable: `scaleStatus`, `currentSetting`, `currentMenuItem`, `setWeight`, `delayEnd`, `grindFlow`, `cupWeightEmpty`, `setCupWeight`, `setCupWeight2`, `scaleFactor`, `noiseSigma`, `shotCount`, `sleepTime`, `scaleReady`, `scaleMode`, `grindMode`, `debugMode`, `grindFailReason`, `grindTime` (seconds since grinding started), `verifiedAgo` (seconds since the readings count towards the confirmed dose), `confirmedDose`, `flowAtSwitchOff`, `delayUsed`, `delayMeasured` |
| `grind <shot> <seconds> <delay> <flow> <target> <actual>` | adds a grind to the Weight History |
| `boot <ms>` | shows the boot screen for this long (the scale reports no reading yet) |
| `draw` | one more display update without time passing |
| `shot <name>` | saves the display as `<name>.png` |

Not simulated: the scale status task (cup detection, starting and stopping the grinder), use `set scaleStatus` for the grinding screens.
