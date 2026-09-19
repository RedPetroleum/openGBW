# Display simulator, see README.md
# make               builds sim/build/opengbw-sim
# make screenshots   runs screenshots.txt, the PNGs land in sim/screenshots
# make readme-screenshots   also copies the screenshots of the main README to screenshots/

ROOT := ..
U8G2 := $(ROOT)/.pio/libdeps/esp32_usb/U8g2/src
BUILD := build

FIRMWARE_SOURCES := $(ROOT)/src/display.cpp $(ROOT)/src/grindlog.cpp $(ROOT)/src/rotary.cpp $(ROOT)/src/scale.cpp $(wildcard $(ROOT)/src/games/*.cpp)
U8G2_SOURCES := $(wildcard $(U8G2)/clib/*.c) $(U8G2)/U8g2lib.cpp

# ARDUINO changes the layout of the U8g2 structs, so it is defined for all files
FLAGS := -O2 -DARDUINO -Istubs -I$(ROOT)/include -I$(ROOT)/lib/MathBuffer/src -I$(U8G2) -ffunction-sections -fdata-sections -MMD -MP
CFLAGS := $(FLAGS)
CXXFLAGS := $(FLAGS) -std=gnu++2a -Wno-deprecated-declarations

FIRMWARE_OBJECTS := $(patsubst $(ROOT)/src/%.cpp,$(BUILD)/firmware/%.o,$(FIRMWARE_SOURCES))
U8G2_OBJECTS := $(patsubst $(U8G2)/%,$(BUILD)/u8g2/%.o,$(U8G2_SOURCES))
OBJECTS := $(BUILD)/sim.o $(FIRMWARE_OBJECTS) $(U8G2_OBJECTS)

$(BUILD)/opengbw-sim: $(OBJECTS)
	$(CXX) -o $@ $^ -lz $(if $(filter Darwin,$(shell uname)),-Wl,-dead_strip,-Wl,--gc-sections)

$(U8G2_SOURCES):
	@echo "U8g2 is missing, run 'pio pkg install' in $(abspath $(ROOT)) first" && false

$(BUILD)/sim.o: sim.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/firmware/%.o: $(ROOT)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/u8g2/%.c.o: $(U8G2)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/u8g2/%.cpp.o: $(U8G2)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

screenshots: $(BUILD)/opengbw-sim
	cd $(ROOT) && sim/$(BUILD)/opengbw-sim -f sim/screenshots.txt

# Copies the screenshots used in the main README to screenshots/
readme-screenshots: screenshots
	cp screenshots/main.png screenshots/grinding.png screenshots/comet-blaster.png screenshots/curve-tracer.png screenshots/doom-fire.png $(ROOT)/screenshots/

clean:
	rm -rf $(BUILD) screenshots

.PHONY: screenshots readme-screenshots clean
-include $(OBJECTS:.o=.d)
