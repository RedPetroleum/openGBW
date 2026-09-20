#include "config.hpp"
#include "rotary.hpp"
#include "games.hpp"

U8G2_SSD1306_128X64_NONAME_F_HW_I2C screen(U8G2_R0);
TaskHandle_t DisplayTask;

// Time in milliseconds after which the display sleeps (10 seconds)
int sleepTime = SLEEP_AFTER_MS; // loaded from the preferences in setupScale

// Function to center-align and print text to the screen
void CenterPrintToScreen(char const *str, u8g2_uint_t y)
{
  u8g2_uint_t width = screen.getStrWidth(str); // Calculate the text width
  screen.setCursor(128 / 2 - width / 2, y);    // Set the cursor position for center alignment
  screen.print(str);                           // Print the text
}

// Function to left-align and print text to the screen
void LeftPrintToScreen(char const *str, u8g2_uint_t y)
{
  screen.setCursor(5, y);
  screen.print(str);
}

// Function to left-align and highlight text as active on the screen
void LeftPrintActiveToScreen(char const *str, u8g2_uint_t y)
{
  screen.setDrawColor(1); // Set drawing color to white
  screen.drawBox(3, y - 1, 122, 14);
  screen.setDrawColor(0);
  screen.setCursor(5, y);
  screen.print(str);
  screen.setDrawColor(1); // Reset drawing color to white
}

// Function to right-align and print text to the screen
void RightPrintToScreen(char const *str, u8g2_uint_t y)
{
  u8g2_uint_t width = screen.getStrWidth(str); // Calculate the text width
  screen.setCursor(123 - width, y);            // Set the cursor position for right alignment
  screen.print(str);                           // Print the text
}

// Decimal point column for the weights on the main screen ("0.0 g" appears centered)
#define WEIGHT_DECIMAL_X 56
// Narrow gap in pixels between a weight and its unit
#define WEIGHT_UNIT_GAP 2

// Boot screen: a wireframe espresso cup turning on its saucer while the scale tares
#define BOOT_TURN_MS 5000       // time for one full turn of the cup
#define BOOT_TILT 0.38f         // the cup is seen from slightly above, in radians
#define BOOT_CAMERA 6.0f        // distance of the camera, gives the model its perspective
#define BOOT_ZOOM 150.0f        // model units to pixels
#define BOOT_CENTER_Y 26        // row the model is drawn around, the bar takes the lower rows
#define BOOT_CENTER_Y_FRAME 34  // without the bar under it the cup sits lower, in the middle of the frame
#define BOOT_RING_SEGMENTS 20   // corners of a circle of the model
#define BOOT_CUP_TOP 0.62f      // the cup sits between these heights ...
#define BOOT_CUP_BOTTOM -0.42f
#define BOOT_CUP_RADIUS_TOP 0.80f  // ... and tapers from this radius ...
#define BOOT_CUP_RADIUS_BOTTOM 0.52f // ... down to this one
#define BOOT_CUP_STRUTS 8       // vertical lines between the rim and the foot
#define BOOT_HANDLE_RADIUS 0.34f
#define BOOT_HANDLE_X 0.92f     // the handle sits outside the wall, at the height of the middle of the cup
#define BOOT_HANDLE_Y 0.18f
#define BOOT_READY_MS 3000      // time the bar needs to fill: the load cell settles, 20 tare measures
                                // at 10/s follow and then the first reading. The boot screen is not
                                // left before the bar is full, even if the scale is ready earlier
#define BOOT_CROSS_HOLD_MS 200  // the cross has finished turning this long before the scale is ready
#define BOOT_BAR_Y 54
#define BOOT_BAR_HEIGHT 9

// Progress of the grinding screen towards the set weight, shown in the style chosen in the Style menu:
// as a bar, a scale with ticks at both ends and at the progress, by inverting the whole screen or as a
// border growing around it
#define GRIND_BAR_Y 39
#define GRIND_BAR_HEIGHT 12
#define GRIND_BAR_TICK_HEIGHT 9 // height of the ticks at start, progress and set weight
#define GRIND_BAR_DONE_HEIGHT 3 // thickness of the line up to the progress tick, the rest is one pixel
#define GRIND_PROGRESS_HEIGHT 64 // rows of the display, the whole screen is inverted at the set weight
#define PROGRESS_FRAME_THICKNESS 2 // thickness of the border that grows around the screen
#define GRIND_PROGRESS_EASING 8.0f // how fast the drawn progress follows the reading (1/s), the scale only reports twice a second

// Finished screen: how far the dose ended up from the set weight, as a bar from the middle of a scale
// that carries the set weight in its middle. At its finest the scale reaches DEVIATION_RANGE_MIN to
// either side; a deviation that needs more room widens it, so the bar always stays on the scale. The
// ticks keep their value in grams while their spacing shrinks with the growing scale - once more than
// DEVIATION_TICKS_MAX of them would fit on one side they are a gram apart instead of a tenth
#define DEVIATION_RANGE_MIN 0.2   // g, half of the scale at its finest resolution
#define DEVIATION_RANGE_FILL 0.85 // the bar reaches at most this much of the half scale
#define DEVIATION_RANGE_EASING 4.0f // how fast the drawn scale follows a growing deviation (1/s)
#define DEVIATION_TICK_FINE 0.1   // g between two ticks ...
#define DEVIATION_TICK_COARSE 1.0 // ... and once there would be too many of them
#define DEVIATION_TICKS_MAX 10    // ticks per side the fine ones are still drawn at
#define DEVIATION_AXIS_Y 38       // row of the thin line the scale is drawn on
#define DEVIATION_HALF_WIDTH 60   // pixels from the middle of the scale to either end
#define DEVIATION_END_HEIGHT 7    // the marks at the ends stand this tall on the line ...
#define DEVIATION_TICK_HEIGHT 4   // ... the ticks between them this tall
#define DEVIATION_TICK_MIN_GAP 2  // a tick this close to the end of the scale is left out, it would only
                                  // cancel the mark there out again
#define DEVIATION_ZERO_ABOVE 9    // the mark for the set weight starts this far above the line ...
#define DEVIATION_ZERO_HEIGHT 13  // ... and reaches past it
#define DEVIATION_BAR_HEIGHT 5    // thickness of the bar from the middle to the dose

// A weight that rounds to zero is shown without a sign: a reading a few hundredths below the cup
// weight would otherwise appear as "-0.0" while the grinder is running
static double noMinusZero(double weight)
{
  return lround(weight * 10) == 0 ? 0.0 : weight;
}

// Function to print a weight with its decimal point at WEIGHT_DECIMAL_X and a narrow gap before "g"
// Requires a font where all digits have the same advance width
void WeightPrintToScreen(double weight, u8g2_uint_t y)
{
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f", weight);
  const char *decimalPoint = strchr(buf, '.');
  int integerDigits = decimalPoint ? decimalPoint - buf : strlen(buf);
  int digitAdvance = u8g2_GetGlyphWidth(screen.getU8g2(), '0'); // advance incl. spacing, not bounding box
  screen.setCursor(WEIGHT_DECIMAL_X - integerDigits * digitAdvance, y);
  screen.print(buf);
  screen.setCursor(screen.getCursorX() + WEIGHT_UNIT_GAP, y);
  screen.print("g");
}

//MENU 

// Menu items for user interface
int currentMenuItem = 0;      // Index of the current menu item
int currentSetting;           // Index of the current setting being adjusted
int menuItemsCount = debugMode ? 13 : 12;      // Total number of menu items

 // Menu items for settings and calibration
MenuItem menuItems[13] = {
    {0, false, "Exit", 0},
    {1, false, "Cup Weight 1", 1, &setCupWeight},
    {2, false, "Cup Weight 2", 1, &setCupWeight2},
    {3, false, "Scale Factor", 1, &scaleFactor},
    {4, false, "Dead Time", 0.01, &deadTimeEnd},
    {5, false, "Scale Mode", 0},
    {6, false, "Grinding Mode", 0},
    {7, false, "Info Menu", 0},
    {8, false, "Sleep Timer", 0},
    {9, false, "Style", 0},
    {10, false, "Reset", 0},
    {11, false, "Games", 0},
    // Debug menu placeholder (conditional)
    {12, false, "Debug Menu", 0} // Visible only if debugMode is true
};

int debugMenuItemsCount = 5; // Number of items in the Debug Menu
int currentDebugMenuItem = 0; // Current selection in the Debug Menu
int grindHistoryScroll = 0;   // First visible entry in the Weight History
int grindHistoryPage = 0;     // Visible column page of the Weight History
static double historyBaseline = 0;   // Reading of the scale that counts as "not pressed"
static bool historyPageArmed = true; // False until the scale has been released after a page turn
static unsigned long historyInputAt = 0; // Time of the last page navigation update
MenuItem debugMenuItems[5] = {
    {0, false, "Exit", 0},
    {1, false, "Sim Grind", 0},
    {2, false, "Weight Data", 0},
    {3, false, "Weight History", 0},
    {4, false, "Zero Shot Count", 0}
};

int currentStyleMenuItem = 0; // Current selection in the Style submenu
static const char *styleMenuItems[] = {"Exit", "Grinding Screen", "Initializing"}; // at most three, they are shown at once
static const int styleMenuItemsCount = sizeof(styleMenuItems) / sizeof(styleMenuItems[0]);

// The Style submenu, everything that only changes how a screen looks. The whole list fits under the
// title, so it is shown at once with the selection highlighted instead of scrolling like the menus above
void showStyleMenu()
{
    screen.clearBuffer();
    screen.setFontPosTop();
    screen.setFont(u8g2_font_7x14B_tf);
    CenterPrintToScreen("Style", 0);

    screen.setFont(u8g2_font_7x13_tr);
    for (int i = 0; i < styleMenuItemsCount; i++)
    {
        u8g2_uint_t y = 19 + i * 16;
        if (i == currentStyleMenuItem)
        {
            LeftPrintActiveToScreen(styleMenuItems[i], y);
        }
        else
        {
            LeftPrintToScreen(styleMenuItems[i], y);
        }
    }

    screen.sendBuffer();
}

void styleMenuOnTurn(int steps)
{
    currentStyleMenuItem = ((currentStyleMenuItem + steps) % styleMenuItemsCount + styleMenuItemsCount) % styleMenuItemsCount;
}

void styleMenuOnClick()
{
    if (currentStyleMenuItem == 0) // Exit
    {
        scaleStatus = STATUS_IN_MENU;
        currentSetting = -1;
        return;
    }
    currentSetting = currentStyleMenuItem == 1 ? GRIND_SCREEN_SETTING : BOOT_SCREEN_SETTING;
    Serial.println(currentStyleMenuItem == 1 ? "Grinding Screen Menu" : "Initializing Screen Menu");
}

static const char *grindStyleNames[GRIND_STYLE_COUNT] = {"Bar", "Invert", "Frame", "Curve (default)"};

// How the grinding screen shows the progress, turning steps through the styles, clicking saves. There
// are more styles than fit under the title, so the list scrolls around the selection like the menus above
void showGrindScreenMenu()
{
  int prev = (grindScreenStyle + GRIND_STYLE_COUNT - 1) % GRIND_STYLE_COUNT;
  int next = (grindScreenStyle + 1) % GRIND_STYLE_COUNT;

  screen.clearBuffer();
  screen.setFontPosTop();
  screen.setFont(u8g2_font_7x14B_tf);
  CenterPrintToScreen("Grinding Screen", 0);
  screen.setFont(u8g2_font_7x13_tr);
  LeftPrintToScreen(grindStyleNames[prev], 19);
  LeftPrintActiveToScreen(grindStyleNames[grindScreenStyle], 35);
  LeftPrintToScreen(grindStyleNames[next], 51);
  screen.sendBuffer();
}

static const char *bootStyleNames[BOOT_STYLE_COUNT] = {"Bar", "Frame (default)", "Cross"};

// How the initializing screen shows that the scale is getting ready, turning switches, clicking saves
void showBootScreenMenu()
{
  screen.clearBuffer();
  screen.setFontPosTop();
  screen.setFont(u8g2_font_7x14B_tf);
  CenterPrintToScreen("Initializing", 0);
  screen.setFont(u8g2_font_7x13_tr);
  for (int i = 0; i < BOOT_STYLE_COUNT; i++)
  {
    u8g2_uint_t y = 19 + i * 16;
    if (i == bootScreenStyle)
    {
      LeftPrintActiveToScreen(bootStyleNames[i], y);
    }
    else
    {
      LeftPrintToScreen(bootStyleNames[i], y);
    }
  }
  screen.sendBuffer();
}

void showDebugMenu()
{
    int prevIndex = (currentDebugMenuItem - 1) % debugMenuItemsCount;
    int nextIndex = (currentDebugMenuItem + 1) % debugMenuItemsCount;

    // Handle negative index wrap-around
    prevIndex = prevIndex < 0 ? prevIndex + debugMenuItemsCount : prevIndex;

    MenuItem prev = debugMenuItems[prevIndex];
    MenuItem current = debugMenuItems[currentDebugMenuItem];
    MenuItem next = debugMenuItems[nextIndex];

    screen.clearBuffer();
    screen.setFontPosTop();
    screen.setFont(u8g2_font_7x14B_tf);

    // Print "Debug Menu" as title
    CenterPrintToScreen("Debug Menu", 0);

    // Display the previous, current, and next items (regular font like the main menu)
    screen.setFont(u8g2_font_7x13_tr);
    LeftPrintToScreen(prev.menuName, 19);
    LeftPrintActiveToScreen(current.menuName, 35);
    LeftPrintToScreen(next.menuName, 51);

    screen.sendBuffer();
}


void setupMenuItems() {
    if (debugMode) {
        menuItemsCount = 13; // Include Debug Menu
    } else {
        menuItemsCount = 12; // Exclude Debug Menu
    }
}

// The display sleeps when neither the scale nor the knob was used for sleepTime
bool displayAsleep() {
    return millis() - lastActivityAt > (unsigned long)sleepTime;
}

void wakeScreen() {
    // Reset the sleep timer and update the display
    lastActivityAt = millis();
    scaleStatus = STATUS_EMPTY;
    screen.clearBuffer();
    screen.sendBuffer();
}

// Function to display the menu with previous, current, and next items
void showMenu()
{
  int prevIndex = (currentMenuItem - 1) % menuItemsCount; // Get the previous menu item index
  int nextIndex = (currentMenuItem + 1) % menuItemsCount; // Get the next menu item index

  // Handle negative index wrap-around
  prevIndex = prevIndex < 0 ? prevIndex + menuItemsCount : prevIndex;
  MenuItem prev = menuItems[prevIndex];          // Previous menu item
  MenuItem current = menuItems[currentMenuItem]; // Current menu item
  MenuItem next = menuItems[nextIndex];          // Next menu item

  char buf[3];
  screen.clearBuffer(); // Clear the display buffer
  screen.setFontPosTop();
  screen.setFont(u8g2_font_7x14B_tf);            // Set the font for the menu title
  CenterPrintToScreen("Menu", 0);                // Print "Menu" title
  screen.setFont(u8g2_font_7x13_tr);             // Set the font for the menu items
  LeftPrintToScreen(prev.menuName, 19);          // Print the previous menu item
  LeftPrintActiveToScreen(current.menuName, 35); // Highlight the current menu item
  LeftPrintToScreen(next.menuName, 51);          // Print the next menu item

  screen.sendBuffer(); // Send the buffer to the display
}

void showSleepTimerMenu() {
    char buf[32];
    screen.clearBuffer();
    screen.setFontPosTop();
    screen.setFont(u8g2_font_7x14B_tf);

    // Display title
    CenterPrintToScreen("Adjust Sleep Timer", 0);

    // Display current sleep timer value in seconds
    screen.setFont(u8g2_font_7x13_tr);
    snprintf(buf, sizeof(buf), "Timer: %d sec", sleepTime / 1000); // Use `sleepTime` here
    CenterPrintToScreen(buf, 32);

    // Display instructions
    LeftPrintToScreen("Turn to adjust", 50);
    screen.sendBuffer();
}

// Function to display the dead time adjustment menu. The dead time is what the grinder still delivers
// after it was switched off; it calibrates itself after every grind and is only adjustable by hand here
void showDeadTimeMenu()
{
  char buf[16];
  screen.clearBuffer();
  screen.setFontPosTop();
  screen.setFont(u8g2_font_7x14B_tf);                // Set the font for the menu title
  CenterPrintToScreen("Adjust dead time", 0);        // Print the menu title
  screen.setFont(u8g2_font_7x13_tr);                 // Set the font for the value
  snprintf(buf, sizeof(buf), "%3.2fs", deadTimeEnd); // Format the dead time
  CenterPrintToScreen(buf, 28);                      // Print the dead time
  screen.sendBuffer();                               // Send the buffer to the display
}

// Function to display the manual scale factor adjustment menu
void showScaleFactorMenu()
{
  char buf[32];
  screen.clearBuffer();
  screen.setFontPosTop();
  screen.setFont(u8g2_font_7x14B_tf);                  // Set the font for the menu title
  CenterPrintToScreen("Scale Factor", 0);              // Print the menu title
  snprintf(buf, sizeof(buf), "%.1f", scaleFactor);     // Format the scale factor
  CenterPrintToScreen(buf, 19);                        // Print the scale factor
  screen.setFont(u8g2_font_7x13_tr);                   // Set the font for the live weight
  snprintf(buf, sizeof(buf), "Weight: %3.1fg", noMinusZero(shownWeight));
  CenterPrintToScreen(buf, 35);                        // Print the live weight for checking
  CenterPrintToScreen("Press to save", 51);            // Print instructions
  screen.sendBuffer();                                 // Send the buffer to the display
}

// Function to display the scale mode menu
void showScaleModeMenu()
{
  screen.clearBuffer();
  screen.setFontPosTop();
  screen.setFont(u8g2_font_7x14B_tf);       // Set the font for the menu title
  CenterPrintToScreen("Set Scale Mode", 0); // Print the menu title
  screen.setFont(u8g2_font_7x13_tr);        // Set the font for the menu items
  if (scaleMode)
  {
    LeftPrintToScreen("GBW (default)", 19);    // Print inactive item
    LeftPrintActiveToScreen("Scale only", 35); // Highlight active item
  }
  else
  {
    LeftPrintActiveToScreen("GBW (default)", 19); // Highlight active item
    LeftPrintToScreen("Scale only", 35); // Print inactive item
  }
  screen.sendBuffer(); // Send the buffer to the display
}

// Function to display the grind mode menu
void showGrindModeMenu()
{
  screen.clearBuffer();
  screen.setFontPosTop();
  screen.setFont(u8g2_font_7x14B_tf);         // Set the font for the menu title
  CenterPrintToScreen("Set Grinder", 0);      // Print the menu title
  CenterPrintToScreen("Start/Stop Mode", 19); // Print the subtitle
  screen.setFont(u8g2_font_7x13_tr);          // Set the font for the menu items
  if (grindMode)
  {
    LeftPrintActiveToScreen("Continuous", 35); // Highlight active item
    LeftPrintToScreen("Impulse", 51);          // Print inactive item
  }
  else
  {
    LeftPrintToScreen("Continuous", 35);    // Print inactive item
    LeftPrintActiveToScreen("Impulse", 51); // Highlight active item
  }
  screen.sendBuffer(); // Send the buffer to the display
}

// Function to display the cup weight adjustment menu
void showCupMenu(char const *title)
{
  char buf[16];
  screen.clearBuffer();
  screen.setFontPosTop();
  screen.setFont(u8g2_font_7x14B_tf);                // Set the font for the menu title
  CenterPrintToScreen(title, 0);                     // Print the menu title
  screen.setFont(u8g2_font_7x13_tr);                 // Set the font for the instructions
  snprintf(buf, sizeof(buf), "%3.1fg", noMinusZero(shownWeight)); // Format the scale weight
  CenterPrintToScreen(buf, 19);                      // Print the scale weight
  LeftPrintToScreen("Place cup, press", 35);         // Print instructions
  LeftPrintToScreen("Turn to cancel", 51);           // Turning leaves without saving
  screen.sendBuffer();                               // Send the buffer to the display
}

void showCupWeightSetScreen(double cupWeight)
{
  char buf[32];
  screen.clearBuffer();
  screen.setFontPosTop();
  screen.setFont(u8g2_font_7x14B_tf);
  CenterPrintToScreen("Cup Weight Set:", 0);
  snprintf(buf, sizeof(buf), "%3.1fg", cupWeight);
  CenterPrintToScreen(buf, 20); // Center the message on the screen

  screen.sendBuffer();
  delay(2000); // Block for 2 seconds to ensure the screen stays visible
}

// Function to display the recorded weights as a line graph (newest reading on the right)
void showWeightData()
{
  double values[WEIGHT_DATA_SIZE];
  int count = 0;
  int64_t oldestTimestamp = millis();
  // Samples are delivered from newest to oldest
  weightData.executeOnSamplesSince(0, [&](double value, int64_t ms) {
    if (count < WEIGHT_DATA_SIZE)
    {
      values[count++] = value;
      oldestTimestamp = ms;
    }
  });

  screen.clearBuffer();
  screen.setFontPosTop();
  screen.setFont(u8g2_font_5x7_tf);
  if (count < 2)
  {
    CenterPrintToScreen("No data yet", 28);
    screen.sendBuffer();
    return;
  }

  double minValue = values[0];
  double maxValue = values[0];
  for (int i = 1; i < count; i++)
  {
    minValue = min(minValue, values[i]);
    maxValue = max(maxValue, values[i]);
  }
  // At least 0.2g, so a quiet scale does not have its last digit blown up over the whole height
  double range = max(maxValue - minValue, 0.2);
  double bottomValue = (minValue + maxValue) / 2 - range / 2; // the readings sit in the middle of the graph

  char buf[32];
  snprintf(buf, sizeof(buf), "%.2fg", scaleWeight);
  LeftPrintToScreen(buf, 0); // Current weight, two decimals to judge how quiet the scale is
  // How much the graph covers: grams over its full height and seconds over its full width
  unsigned long seconds = (millis() - oldestTimestamp) / 1000;
  // Two decimals while the graph is zoomed into the noise, whole grams for a grind
  snprintf(buf, sizeof(buf), range < 10 ? "%.2fg %lus" : "%.0fg %lus", range, seconds);
  RightPrintToScreen(buf, 0);

  // One pixel column per reading, newest on the right and nothing in between: the readings are not
  // stretched over the width, so neighbouring columns are neighbouring measurements
  const int graphTop = 9;
  const int graphBottom = 63;
  for (int i = 0; i < count; i++)
  {
    int y = graphBottom - (values[i] - bottomValue) / range * (graphBottom - graphTop);
    screen.drawPixel(127 - i, y);
  }
  screen.sendBuffer();
}

// Starts the Weight History at its first page and takes the current reading as the zero point
void resetGrindHistoryInput()
{
  grindHistoryScroll = 0;
  grindHistoryPage = 0;
  historyBaseline = scaleWeight;
  historyPageArmed = true;
  historyInputAt = millis();
}

// Pressing the scale down turns to the next column page of the Weight History, pulling it up turns back.
// While it is not pressed the zero point follows the reading, so a cup standing on the scale is no problem.
static void grindHistoryScaleInput()
{
  unsigned long now = millis();
  float dt = min((now - historyInputAt) / 1000.0f, 0.1f);
  historyInputAt = now;

  double pressed = scaleWeight - historyBaseline;
  if (abs(pressed) < HISTORY_PAGE_RELEASE)
  {
    historyBaseline += pressed * min(1.0f, dt * HISTORY_BASELINE_FOLLOW);
    historyPageArmed = true; // the scale is free again, the next press turns a page
    return;
  }
  if (!historyPageArmed)
    return;
  if (pressed > HISTORY_PAGE_PRESS && grindHistoryPage < GRIND_HISTORY_PAGES - 1)
  {
    grindHistoryPage++;
    historyPageArmed = false;
  }
  else if (pressed < -HISTORY_PAGE_PRESS && grindHistoryPage > 0)
  {
    grindHistoryPage--;
    historyPageArmed = false;
  }
}

// Function to display the last grinds (newest first, turn to scroll, press the scale for the next page).
// The header names the columns of the current page: grinding time, dead time and mass flow at the
// switch-off, or target, actual weight and difference.
void showGrindHistory()
{
  char buf[32];
  grindHistoryScaleInput();
  screen.clearBuffer();
  screen.setFontPosTop();
  screen.setFont(u8g2_font_6x10_tr);
  if (grindHistoryCount == 0)
  {
    CenterPrintToScreen("Weight History", 0);
    CenterPrintToScreen("No grinds yet", 30);
    screen.sendBuffer();
    return;
  }
  LeftPrintToScreen("Shot", 0);
  // Same widths as the values below, so the header sits above its columns
  RightPrintToScreen(grindHistoryPage == 0 ? "Time   Dead g/s" : "Targ  Act Diff", 0);
  for (int row = 0; row < GRIND_HISTORY_ROWS; row++)
  {
    int index = grindHistoryScroll + row;
    if (index >= grindHistoryCount)
      break;
    GrindRecord &record = grindHistory[index];
    snprintf(buf, sizeof(buf), "#%lu", (unsigned long)record.shot);
    LeftPrintToScreen(buf, 12 + row * 10);
    if (grindHistoryPage == 0)
      snprintf(buf, sizeof(buf), "%4.1fs %4.2fs %3.1f", record.duration, record.deadTime, record.flow);
    else
      snprintf(buf, sizeof(buf), "%4.1f %4.1f %+4.1f", record.target, record.actual, record.actual - record.target);
    RightPrintToScreen(buf, 12 + row * 10);
  }
  screen.sendBuffer();
}

// Function to display the reset menu
void showResetMenu()
{
  screen.clearBuffer();
  screen.setFontPosTop();
  screen.setFont(u8g2_font_7x14B_tf);           // Set the font for the menu title
  CenterPrintToScreen("Reset to defaults?", 0); // Print the menu title
  screen.setFont(u8g2_font_7x13_tr);            // Set the font for the menu items
  if (greset)
  {
    LeftPrintActiveToScreen("Confirm", 19); // Highlight active item
    LeftPrintToScreen("Cancel", 35);        // Print inactive item
  }
  else
  {
    LeftPrintToScreen("Confirm", 19);      // Print inactive item
    LeftPrintActiveToScreen("Cancel", 35); // Highlight active item
  }
  screen.sendBuffer(); // Send the buffer to the display
}

void showInfoMenu() {
    char buf[32];

    // Clear the buffer and set font
    screen.clearBuffer();
    screen.setFontPosTop();
    screen.setFont(u8g2_font_7x14B_tf);

    // Display title
    CenterPrintToScreen("System Info", 0);

    // Display cup weight (smaller font so four lines fit)
    screen.setFont(u8g2_font_6x10_tr);
    snprintf(buf, sizeof(buf), "Cups: %.1f/%.1fg", setCupWeight, setCupWeight2);
    LeftPrintToScreen(buf, 16);

    // Display the dead time the grinder is stopped with
    snprintf(buf, sizeof(buf), "Dead Time: %3.2fs", deadTimeEnd);
    LeftPrintToScreen(buf, 28);

    // Display scale factor
    snprintf(buf, sizeof(buf), "Scale Factor: %.1f", scaleFactor);
    LeftPrintToScreen(buf, 40);

    // Display shot count
    snprintf(buf, sizeof(buf), "Shot Count: %u", shotCount);
    LeftPrintToScreen(buf, 52);

    // Send buffer to the display
    screen.sendBuffer();

    // No unnecessary delays or clearing here
}

void showDebugModeStatus(bool debugMode)
{
    displayLock = true; // Lock the display while showing the message
    screen.clearBuffer();
    screen.setFont(u8g2_font_7x14B_tf);
    CenterPrintToScreen(debugMode ? "Debug Mode Enabled" : "Debug Mode Disabled", 32);
    screen.sendBuffer();
    delay(2000); // Show the message for 2 seconds
    displayLock = false; // Unlock the display
}


// Function to display the appropriate menu or setting based on the current state
void showSetting()
{
  if (currentSetting == 0)
  {
    showCupMenu("Cup Weight 1");
  }
  else if (currentSetting == 11)
  {
    showCupMenu("Cup Weight 2");
  }
  else if (currentSetting == 2)
  {
    showDeadTimeMenu();
  }
  else if (currentSetting == 3)
  {
    showScaleModeMenu();
  }
  else if (currentSetting == 4)
  {
    showGrindModeMenu();
  }
  else if (currentSetting == 6)
  {
    showResetMenu();
  }
  else if (currentSetting == 5)
  {
    showInfoMenu();
  }
  else if (currentSetting == 8)
  {
    showSleepTimerMenu();
  }
  else if (currentSetting == 9) {
    showDebugMenu();
  }
  else if (currentSetting == 10)
  {
    showScaleFactorMenu();
  }
  else if (currentSetting == WEIGHT_DATA_SETTING)
  {
    showWeightData();
  }
  else if (currentSetting == GRIND_HISTORY_SETTING)
  {
    showGrindHistory();
  }
  else if (currentSetting == GAMES_MENU_SETTING)
  {
    showGamesMenu();
  }
  else if (currentSetting == STYLE_MENU_SETTING)
  {
    showStyleMenu();
  }
  else if (currentSetting == GRIND_SCREEN_SETTING)
  {
    showGrindScreenMenu();
  }
  else if (currentSetting == BOOT_SCREEN_SETTING)
  {
    showBootScreenMenu();
  }

}

void handleDebugMenuAction()
{
    switch (currentDebugMenuItem)
    {
    case 0: // Exit Debug Menu
      Serial.println("Exiting Debug Menu...");
      exitToMenu(); // Return to Main Menu
      break;

    case 1: // Simulate Grinding
        Serial.println("Simulating Grinding...");
        scaleStatus = STATUS_GRINDING_IN_PROGRESS; // Temporarily change the state for grinding simulation
        startedGrindingAt = millis();
        setWeight = 20.0;     // Example weight
        cupWeightEmpty = 5.0; // Example cup weight
        delay(5000);          // Simulate grinding for 5 seconds
        scaleStatus = STATUS_IN_SUBMENU; // Return to Debug Menu state
        currentSetting = 9;
        exitToMenu();
        break;

    case 2: // Show Weight Data
        Serial.println("Displaying Weight Data...");
        currentSetting = WEIGHT_DATA_SETTING; // Graph is drawn by the display task, click returns to the Debug Menu
        return;

    case 3: // Show Weight History (time, dead time, flow, target and actual weight of the last grinds)
        Serial.println("Displaying Weight History...");
        resetGrindHistoryInput();
        currentSetting = GRIND_HISTORY_SETTING; // Drawn by the display task, turn to scroll, press the scale to page, click returns
        return;

    case 4: // Reset Shot Count
      Serial.println("Resetting Shot Count...");
      shotCount = 0;
      preferences.begin("scale", false);
      preferences.putUInt("shotCount", shotCount);
      preferences.end();
      // Show confirmation message
      displayLock = true;
      screen.clearBuffer();
      screen.setFont(u8g2_font_7x14B_tf);
      CenterPrintToScreen("Shot Count Reset", 32);
      screen.sendBuffer();
      delay(2000);
      displayLock = false;

      // Stay in the Debug Menu
      scaleStatus = STATUS_IN_SUBMENU;
      currentSetting = 9;
      exitToMenu();
      break;
    }
    showDebugMenu(); // Update the Debug Menu display after action
}


static unsigned long bootStartedAt = 0; // First frame of the boot screen

// True while the bar of the boot screen has not filled up yet
static bool bootScreenBusy()
{
  return bootStartedAt == 0 || millis() - bootStartedAt < BOOT_READY_MS;
}

// Rounds a drawing coordinate to the nearest pixel
static int toPixel(float value)
{
  return (int)lroundf(value);
}

// Turns a point of the wireframe model around the upright axis, tilts it towards the viewer
// and projects it onto the display
static void bootProject(float x, float y, float z, float turn, int &screenX, int &screenY)
{
  float turnedX = x * cos(turn) + z * sin(turn);
  float turnedZ = -x * sin(turn) + z * cos(turn);
  float tiltedY = y * cos(BOOT_TILT) - turnedZ * sin(BOOT_TILT);
  float tiltedZ = y * sin(BOOT_TILT) + turnedZ * cos(BOOT_TILT);
  float scale = BOOT_ZOOM / (BOOT_CAMERA - tiltedZ);
  screenX = 64 + toPixel(turnedX * scale);
  screenY = (bootScreenStyle == BOOT_STYLE_FRAME ? BOOT_CENTER_Y_FRAME : BOOT_CENTER_Y) - toPixel(tiltedY * scale);
}

// Draws a circle of the model lying flat at the given height
static void bootDrawRing(float radius, float height, float turn)
{
  int previousX = 0, previousY = 0;
  for (int corner = 0; corner <= BOOT_RING_SEGMENTS; corner++)
  {
    float angle = corner * 2 * PI / BOOT_RING_SEGMENTS;
    int x, y;
    bootProject(cos(angle) * radius, height, sin(angle) * radius, turn, x, y);
    if (corner > 0)
    {
      screen.drawLine(previousX, previousY, x, y);
    }
    previousX = x;
    previousY = y;
  }
}

// Draws one frame of the boot screen: an espresso cup as a turning wireframe model above a bar
// that fills while the scale tares
static void drawProgressFrame(float shown, bool complete); // drawn with the progress screens below

// The cross style of the initializing screen: nothing but a cross through the middle of the screen that
// turns a quarter clockwise while the scale gets ready. Its arms are only limited by the edge of the
// display, so they are always as long as they can be and grow and shrink as it turns.
static void drawBootCross(float progress)
{
  const float centerX = 63.5f, centerY = 31.5f;
  float angle = progress * PI / 2; // clockwise, the rows grow downwards
  for (int arm = 0; arm < 4; arm++)
  {
    float dx = cos(angle + arm * PI / 2);
    float dy = sin(angle + arm * PI / 2);
    float length = 1000; // how far the arm goes before it leaves the display
    if (fabsf(dx) > 0.001f)
    {
      length = min(length, ((dx > 0 ? 127.0f : 0.0f) - centerX) / dx);
    }
    if (fabsf(dy) > 0.001f)
    {
      length = min(length, ((dy > 0 ? 63.0f : 0.0f) - centerY) / dy);
    }
    // The ends are rounded as a distance from the middle, so an arm that runs straight stays straight
    int x = lroundf(centerX), y = lroundf(centerY);
    screen.drawLine(x, y, constrain(x + (int)lroundf(dx * length), 0, 127),
                    constrain(y + (int)lroundf(dy * length), 0, 63));
  }
}

static void drawBootScreen()
{
  if (bootStartedAt == 0)
  {
    bootStartedAt = millis();
  }
  unsigned long elapsed = millis() - bootStartedAt;
  // All styles fill evenly over the time the scale needs to get ready
  float progress = min(1.0f, elapsed / (float)BOOT_READY_MS);
  if (bootScreenStyle == BOOT_STYLE_CROSS)
  {
    // This style is the whole screen, the cup stays out of it. The cross turns a little faster than the
    // scale needs and then stands upright for the last BOOT_CROSS_HOLD_MS
    drawBootCross(min(1.0f, elapsed / (float)(BOOT_READY_MS - BOOT_CROSS_HOLD_MS)));
    return;
  }
  float turn = elapsed % BOOT_TURN_MS / (float)BOOT_TURN_MS * 2 * PI;

  bootDrawRing(BOOT_CUP_RADIUS_TOP, BOOT_CUP_TOP, turn);
  bootDrawRing(BOOT_CUP_RADIUS_BOTTOM, BOOT_CUP_BOTTOM, turn);

  // The wall between the rim and the foot
  for (int strut = 0; strut < BOOT_CUP_STRUTS; strut++)
  {
    float angle = strut * 2 * PI / BOOT_CUP_STRUTS;
    int topX, topY, bottomX, bottomY;
    bootProject(cos(angle) * BOOT_CUP_RADIUS_TOP, BOOT_CUP_TOP, sin(angle) * BOOT_CUP_RADIUS_TOP, turn, topX, topY);
    bootProject(cos(angle) * BOOT_CUP_RADIUS_BOTTOM, BOOT_CUP_BOTTOM, sin(angle) * BOOT_CUP_RADIUS_BOTTOM, turn, bottomX, bottomY);
    screen.drawLine(topX, topY, bottomX, bottomY);
  }

  // The handle is a ring standing upright next to the wall, so it swings around the cup while it turns
  int previousX = 0, previousY = 0;
  for (int corner = 0; corner <= BOOT_RING_SEGMENTS; corner++)
  {
    float angle = corner * 2 * PI / BOOT_RING_SEGMENTS;
    int x, y;
    bootProject(BOOT_HANDLE_X + cos(angle) * BOOT_HANDLE_RADIUS, BOOT_HANDLE_Y + sin(angle) * BOOT_HANDLE_RADIUS, 0, turn, x, y);
    if (corner > 0)
    {
      screen.drawLine(previousX, previousY, x, y);
    }
    previousX = x;
    previousY = y;
  }

  if (bootScreenStyle == BOOT_STYLE_FRAME)
  {
    drawProgressFrame(progress, true); // the border closes when the scale is ready
    return;
  }
  // The bar keeps one pixel of air to its frame
  screen.drawFrame(0, BOOT_BAR_Y, 128, BOOT_BAR_HEIGHT);
  int fill = toPixel(progress * (128 - 4));
  if (fill > 0)
  {
    screen.drawBox(2, BOOT_BAR_Y + 2, fill, BOOT_BAR_HEIGHT - 4);
  }
}

static float grindProgressShown = 0;  // Progress as drawn, follows the reading smoothly
static unsigned long grindProgressDrawnAt = 0; // Time of the last progress update

// The progress as a bar: a scale from the empty cup to the set weight, the ground part drawn as a
// thicker line than the part that is still missing
static void drawGrindBar(float shown, bool complete)
{
  int centerY = GRIND_BAR_Y + GRIND_BAR_HEIGHT / 2;
  int tickY = centerY - GRIND_BAR_TICK_HEIGHT / 2;
  int span = 127; // the ticks of start and set weight sit on the first and the last column
  int progressX = (int)ceilf(shown * span);
  progressX = constrain(progressX, 0, complete ? span : span - 1);

  screen.drawHLine(0, centerY, 128); // the whole scale, the ground part is drawn over it thicker
  screen.drawBox(0, centerY - GRIND_BAR_DONE_HEIGHT / 2, progressX + 1, GRIND_BAR_DONE_HEIGHT);
  screen.drawVLine(0, tickY, GRIND_BAR_TICK_HEIGHT);
  screen.drawVLine(progressX, tickY, GRIND_BAR_TICK_HEIGHT);
  screen.drawVLine(127, tickY, GRIND_BAR_TICK_HEIGHT);
}

// The progress as a border growing around the screen: four arms start in the middle of the top and the
// bottom edge, run outwards to the corners and from there along the sides back towards the middle, so
// when it is done the border is closed. Drawn like the inversion, a lit pixel under an arm goes dark.
// Used by the grinding screen and by the initializing screen.
static void drawProgressFrame(float shown, bool complete)
{
  int t = PROGRESS_FRAME_THICKNESS;
  int armLength = 64 + (32 - t); // half an edge outwards, then along the side to the middle
  int drawn = (int)ceilf(shown * armLength);
  drawn = constrain(drawn, 0, complete ? armLength : armLength - 1);
  int along = min(drawn, 64);  // pixels on the top or bottom edge
  int down = drawn - along;    // pixels on the left or right side, the corner belongs to the edge

  screen.setDrawColor(2);
  if (along > 0)
  {
    screen.drawBox(64 - along, 0, along, t);       // top left
    screen.drawBox(64, 0, along, t);               // top right
    screen.drawBox(64 - along, 64 - t, along, t);  // bottom left
    screen.drawBox(64, 64 - t, along, t);          // bottom right
  }
  if (down > 0)
  {
    screen.drawBox(0, t, t, down);                 // left side from the top
    screen.drawBox(128 - t, t, t, down);           // right side from the top
    screen.drawBox(0, 64 - t - down, t, down);     // left side from the bottom
    screen.drawBox(128 - t, 64 - t - down, t, down); // right side from the bottom
  }
  screen.setDrawColor(1);
}

// The progress by inverting the screen from the bottom up
static void invertGrindScreen(float shown, bool complete)
{
  int rows = (int)ceilf(shown * GRIND_PROGRESS_HEIGHT);
  rows = constrain(rows, 0, complete ? GRIND_PROGRESS_HEIGHT : GRIND_PROGRESS_HEIGHT - 1);
  if (rows > 0)
  {
    screen.setDrawColor(2); // XOR: lit pixels go dark and the background lights up
    screen.drawBox(0, GRIND_PROGRESS_HEIGHT - rows, 128, rows);
    screen.setDrawColor(1);
  }
}

// Shows how much of the set weight is in the cup, in the style chosen in the Style menu. The reading
// only arrives twice a second, so the progress grows smoothly instead of jumping. While grinding it
// stays one pixel short of the end: the grinder stops before the set weight. Once the grinder is off
// and only the reading still has to settle, it runs all the way. Has to be called last, inverting
// takes everything that has been drawn before with it.
static void showGrindProgress(float progress, bool complete)
{
  unsigned long now = millis();
  float dt = min((now - grindProgressDrawnAt) / 1000.0f, 0.1f);
  grindProgressDrawnAt = now;
  grindProgressShown += ((complete ? 1.0f : progress) - grindProgressShown) * min(1.0f, dt * GRIND_PROGRESS_EASING);

  // The easing only approaches the target, so the last pixels are rounded up to reach the end
  switch (grindScreenStyle)
  {
  case GRIND_STYLE_INVERT:
    invertGrindScreen(grindProgressShown, complete);
    break;
  case GRIND_STYLE_FRAME:
    drawProgressFrame(grindProgressShown, complete);
    break;
  default:
    drawGrindBar(grindProgressShown, complete);
    break;
  }
}

static float deviationRangeShown = 0; // half of the drawn scale in grams, follows the deviation smoothly
static unsigned long deviationDrawnAt = 0; // Time of the last update of the scale

// The deviation of the dose from the set weight: the set weight is the mark in the middle, the bar
// runs from there to where the dose ended up. The scale grows with the deviation, see the
// DEVIATION_ defines above
static void drawDeviationScale(double deviation)
{
  float wanted = max((float)DEVIATION_RANGE_MIN, (float)(fabs(deviation) / DEVIATION_RANGE_FILL));
  unsigned long now = millis();
  float dt = min((now - deviationDrawnAt) / 1000.0f, 0.1f);
  deviationDrawnAt = now;
  // The first frame of a grind starts on the scale it needs, from there it follows smoothly
  deviationRangeShown = deviationRangeShown <= 0
                            ? wanted
                            : deviationRangeShown +
                                  (wanted - deviationRangeShown) * min(1.0f, dt * DEVIATION_RANGE_EASING);
  float range = deviationRangeShown;

  int y = DEVIATION_AXIS_Y, half = DEVIATION_HALF_WIDTH, zeroX = 64;
  screen.drawHLine(zeroX - half, y, 2 * half + 1);
  int doseX = zeroX + (int)lroundf(constrain(deviation / range, -1.0f, 1.0f) * half);
  screen.drawBox(min(doseX, zeroX), y - DEVIATION_BAR_HEIGHT, abs(doseX - zeroX) + 1, DEVIATION_BAR_HEIGHT);

  // The marks are drawn over the bar: where it covers them they stay readable as dark notches in it
  screen.setDrawColor(2); // XOR
  screen.drawVLine(zeroX - half, y - DEVIATION_END_HEIGHT + 1, DEVIATION_END_HEIGHT);
  screen.drawVLine(zeroX + half, y - DEVIATION_END_HEIGHT + 1, DEVIATION_END_HEIGHT);

  float tickStep = range > DEVIATION_TICKS_MAX * DEVIATION_TICK_FINE ? DEVIATION_TICK_COARSE
                                                                     : DEVIATION_TICK_FINE;
  for (int tick = 1; tick * tickStep <= range; tick++)
  {
    int dx = (int)lroundf(tick * tickStep / range * half);
    if (half - dx < DEVIATION_TICK_MIN_GAP)
    {
      break; // it would fall on the mark at the end of the scale and rub it out
    }
    screen.drawVLine(zeroX - dx, y - DEVIATION_TICK_HEIGHT + 1, DEVIATION_TICK_HEIGHT);
    screen.drawVLine(zeroX + dx, y - DEVIATION_TICK_HEIGHT + 1, DEVIATION_TICK_HEIGHT);
  }

  screen.drawVLine(zeroX, y - DEVIATION_ZERO_ABOVE, DEVIATION_ZERO_HEIGHT); // the set weight
  screen.setDrawColor(1);
}

// The grind as a curve: how the weight in the cup grew over the whole grind, with the set weight as a
// dashed line and the moment the grinder was switched off marked in it. The readings are recorded while
// grinding and drawn stretched over the width, so the curve always spans the whole grind - it only gets
// wider as long as the grind is shorter than CURVE_MIN_SPAN_MS, after that the time axis stretches.
#define CURVE_SAMPLES 256    // recorded readings, a grind longer than that halves its resolution
#define CURVE_SAMPLE_MS 100  // time between two recorded readings at the start
#define CURVE_MIN_SPAN_MS 5000 // time the width covers at least, so a short grind is not blown up
#define CURVE_HEADER_BOTTOM 13 // last row of the current weight in the header
#define CURVE_TOP 15         // rows the curve is drawn in, one pixel below the header ...
#define CURVE_BOTTOM 52
#define CURVE_BASELINE 54    // ... and the dotted line under it
#define CURVE_HEADROOM 1.0f  // the set weight is the top of the scale, an overshoot pushes it down a little

static int16_t curveWeights[CURVE_SAMPLES]; // recorded weights in hundredths of a gram
static int curveSampleMs = CURVE_SAMPLE_MS; // time one recorded reading stands for at the moment
static int curveCount = 0;                  // readings recorded so far
static long curveOffMs = -1;                // time the grinder was switched off at, -1 while it still runs

static void resetGrindCurve()
{
  curveSampleMs = CURVE_SAMPLE_MS;
  curveCount = 0;
  curveOffMs = -1;
}

// Records the current weight. A grind that is longer than the buffer keeps every second reading and
// records half as often from then on, so however long it takes the whole grind stays in the curve.
static void recordGrindCurve(unsigned long elapsed, float weight)
{
  while (elapsed / curveSampleMs >= CURVE_SAMPLES)
  {
    for (int i = 0; i < CURVE_SAMPLES / 2; i++)
    {
      curveWeights[i] = curveWeights[i * 2 + 1];
    }
    curveCount = CURVE_SAMPLES / 2;
    curveSampleMs *= 2;
  }
  int index = elapsed / curveSampleMs;
  for (int i = curveCount; i <= index; i++) // readings missed by a slow frame keep the last weight
  {
    curveWeights[i] = (int16_t)lroundf(weight * 100);
  }
  curveCount = index + 1;
}

// The whole grinding screen in the curve style, drawn instead of the weights and the progress
static void showGrindCurve(bool verifying)
{
  char buf[16];
  double weight = shownWeight - cupWeightEmpty;
  // The curve keeps growing while verifying: what falls after the switch-off belongs to the grind
  unsigned long elapsed = startedGrindingAt > 0 ? millis() - startedGrindingAt : 0;
  unsigned long grindMs = verifying && startedGrindingAt > 0 ? finishedGrindingAt - startedGrindingAt : elapsed;
  recordGrindCurve(elapsed, weight);
  if (verifying && curveOffMs < 0)
  {
    curveOffMs = grindMs; // the grinder went off when the screen started verifying
  }

  // The set weight sits below the top edge, the overshoot has to fit in as well
  float top = max((float)(setWeight * CURVE_HEADROOM), 1.0f); // setWeight is a double, max() needs one type
  for (int i = 0; i < curveCount; i++)
  {
    top = max(top, curveWeights[i] / 100.0f);
  }
  long span = max((long)elapsed, (long)CURVE_MIN_SPAN_MS); // time the width covers
  auto curveX = [&](long ms) { return (int)(ms * 127 / span); };
  auto curveY = [&](float grams) {
    int y = CURVE_BOTTOM - (int)(grams / top * (CURVE_BOTTOM - CURVE_TOP));
    return constrain(y, CURVE_TOP, CURVE_BOTTOM);
  };

  screen.clearBuffer();
  screen.setFontPosTop();
  screen.setFont(u8g2_font_7x14B_tf); // the weight in the cup is the number to read from across the room
  snprintf(buf, sizeof(buf), "%.1fg", noMinusZero(weight));
  LeftPrintToScreen(buf, 0);
  screen.setFont(u8g2_font_5x7_tf);
  screen.setFontPosBottom();
  snprintf(buf, sizeof(buf), "%.1fg", setWeight);
  RightPrintToScreen(buf, CURVE_HEADER_BOTTOM + 1); // the set weight stands next to it, on its baseline
  screen.setFontPosTop();

  // The set weight as a dashed line
  int targetY = curveY(setWeight);
  for (int x = 0; x < 128; x += 4)
  {
    screen.drawPixel(x, targetY);
    screen.drawPixel(x + 1, targetY);
  }

  // The curve, after the switch-off dotted: that part is what still fell out of the grinder
  int previousX = 0;
  int previousY = curveY(curveWeights[0] / 100.0f);
  for (int i = 1; i < curveCount; i++)
  {
    int x = curveX((long)i * curveSampleMs);
    int y = curveY(curveWeights[i] / 100.0f);
    if (curveOffMs >= 0 && (long)i * curveSampleMs > curveOffMs)
    {
      if (x % 2 == 0)
      {
        screen.drawPixel(x, y);
      }
    }
    else
    {
      screen.drawLine(previousX, previousY, x, y);
    }
    previousX = x;
    previousY = y;
  }

  // The moment the grinder was switched off
  if (curveOffMs >= 0)
  {
    int offX = curveX(curveOffMs);
    for (int y = CURVE_TOP; y <= CURVE_BOTTOM; y += 3)
    {
      screen.drawPixel(offX, y);
    }
    int offIndex = constrain((int)(curveOffMs / curveSampleMs), 0, max(0, curveCount - 1));
    screen.drawBox(offX - 1, curveY(curveWeights[offIndex] / 100.0f) - 1, 3, 3);
  }

  // The ground the curve stands on, dotted so it stays behind the curve
  for (int x = 0; x < 128; x += 2)
  {
    screen.drawPixel(x, CURVE_BASELINE);
  }

  // The numbers of the grind: how long it took, how fast it ran and how far it is from the set weight
  screen.setCursor(0, 56);
  snprintf(buf, sizeof(buf), "%.1fs", grindMs / 1000.0); // the grinding time, it stands still while verifying
  screen.print(buf);
  screen.setCursor(42, 56);
  snprintf(buf, sizeof(buf), "%.2fg/s", grindMs > 500 ? weight / (grindMs / 1000.0) : 0.0);
  screen.print(buf);
  screen.setCursor(95, 56);
  snprintf(buf, sizeof(buf), "%+.1fg", noMinusZero(weight - setWeight));
  screen.print(buf);
}

// Draws the current state once (also used by the display simulator in sim/)
void refreshDisplay()
{
  char buf[64];

  if (displayLock)
  {
    delay(50); // Skip updating the display while locked
    return;
  }

  screen.clearBuffer(); // Clear the display buffer
  screen.clearBuffer(); // Clear the display buffer
  if (displayAsleep())
  {
    screen.sendBuffer(); // Send the buffer to the display to "sleep"
    delay(100);
    scaleStatus = STATUS_EMPTY;
    return;
  }

  if (scaleLastUpdatedAt == 0 || bootScreenBusy())
  {
    drawBootScreen(); // stays up until the scale is ready and the bar has filled up
  }
  else if (!scaleReady)
  {
    screen.setFontPosTop();
    screen.drawStr(0, 20, "SCALE ERROR");
  }
  else
  {
    if (scaleStatus == STATUS_GRINDING_IN_PROGRESS || scaleStatus == STATUS_GRINDING_VERIFYING)
    {
      // The grinder has already stopped while verifying, only the reading still has to settle
      bool verifying = scaleStatus == STATUS_GRINDING_VERIFYING;

      if (grindScreenStyle == GRIND_STYLE_CURVE)
      {
        showGrindCurve(verifying); // draws the whole screen, the weights and the progress are in the curve
        screen.sendBuffer();
        return;
      }

      screen.setFontPosTop();
      screen.setFont(u8g2_font_7x13_tr);
      CenterPrintToScreen(verifying ? "Verifying..." : "Grinding...", 0);

      screen.setFontPosCenter();
      screen.setFont(u8g2_font_7x14B_tf);
      screen.setCursor(3, 26);
      snprintf(buf, sizeof(buf), "%3.1fg", noMinusZero(shownWeight - cupWeightEmpty));
      screen.print(buf);

      screen.setFontPosCenter();
      screen.setFont(u8g2_font_unifont_t_symbols);
      screen.drawGlyph(64, 26, 0x2794);

      screen.setFontPosCenter();
      screen.setFont(u8g2_font_7x14B_tf);
      screen.setCursor(84, 26);
      snprintf(buf, sizeof(buf), "%3.1fg", setWeight);
      screen.print(buf);

      screen.setFontPosBottom();
      screen.setFont(u8g2_font_7x13_tr);
      // While verifying the grinding time stands still, it is the time the grind took
      double grindSeconds = verifying ? (double)(finishedGrindingAt - startedGrindingAt) / 1000
                                      : (startedGrindingAt > 0 ? (double)(millis() - startedGrindingAt) / 1000 : 0);
      snprintf(buf, sizeof(buf), "%3.1fs", grindSeconds);
      CenterPrintToScreen(buf, 64);

      showGrindProgress(setWeight > 0 ? (shownWeight - cupWeightEmpty) / setWeight : 0, verifying);
    }
    else if (scaleStatus == STATUS_EMPTY)
    {
      grindProgressShown = 0; // the next grind starts at the beginning again
      deviationRangeShown = 0;
      resetGrindCurve();

      screen.setFontPosTop();
      screen.setFont(u8g2_font_7x13_tr);
      CenterPrintToScreen("Weight:", 0);

      screen.setFont(u8g2_font_7x14B_tf);
      screen.setFontPosCenter();
      WeightPrintToScreen(noMinusZero(shownWeight), 32); // negative weights keep their sign

      // Set weight is aligned on the decimal point below the measured weight, "Set:" sits left of it
      screen.setFont(u8g2_font_7x13_tf);
      screen.setFontPosCenter();
      LeftPrintToScreen("Set:", 50);
      WeightPrintToScreen(setWeight, 50);
    }
    else if (scaleStatus == STATUS_GRINDING_FAILED)
    {
      screen.setFontPosTop();
      screen.setFont(u8g2_font_7x14B_tf);
      CenterPrintToScreen("Grinding failed", 0);

      screen.setFontPosTop();
      screen.setFont(u8g2_font_7x13_tr);
      CenterPrintToScreen(grindFailReason, 18);
      CenterPrintToScreen("Press knob", 36);
      CenterPrintToScreen("to reset", 50);
    }
    else if (scaleStatus == STATUS_GRINDING_FINISHED)
    {
      // What the grind is judged by: the average of the readings since the dose was confirmed, which
      // gets quieter the longer the cup stands, and how far it ended up from the set weight
      double dose = noMinusZero(verifiedDose());
      double deviation = dose - setWeight;
      double shownDeviation = lround(deviation * 100) == 0 ? 0.0 : deviation; // no "-0.00"

      screen.setFontPosTop();
      screen.setFont(u8g2_font_logisoso16_tf);
      snprintf(buf, sizeof(buf), shownDeviation == 0 ? "%.2f g" : "%+.2f g", shownDeviation);
      CenterPrintToScreen(buf, 1);

      drawDeviationScale(deviation);

      // The dose in bold with the set weight next to it, the two together centered
      char setBuf[16];
      snprintf(buf, sizeof(buf), "%.1fg", dose);
      snprintf(setBuf, sizeof(setBuf), " / %.1fg", setWeight);
      screen.setFontPosBottom();
      screen.setFont(u8g2_font_7x14B_tf);
      int doseWidth = screen.getStrWidth(buf);
      screen.setFont(u8g2_font_7x13_tr);
      int setWidth = screen.getStrWidth(setBuf);
      screen.setFont(u8g2_font_7x14B_tf);
      screen.setCursor(64 - (doseWidth + setWidth) / 2, 64);
      screen.print(buf);
      screen.setFont(u8g2_font_7x13_tr);
      screen.print(setBuf);
    }
    else if (scaleStatus == STATUS_IN_MENU)
    {
      showMenu();
    }
    else if (scaleStatus == STATUS_IN_SUBMENU)
    {
      showSetting();
    }
    else if (scaleStatus == STATUS_INFO_MENU)
    {
      showInfoMenu(); // Continuously display the Info Menu while in this state
      delay(100);     // Add a small delay to avoid rapid screen updates
      return;         // Skip the rest of the update logic
    }
    else if (scaleStatus == STATUS_GAME)
    {
      gameLoop(); // Updates and draws one frame, paces itself
    }
  }
  screen.sendBuffer(); // Send the buffer to the display
}

// Task to update the display with the current state
void updateDisplay(void *parameter)
{
  for (;;)
  {
    refreshDisplay();
  }
}

// Function to initialize the display and start the display update task
void setupDisplay()
{
  screen.begin();                    // Initialize the display
  screen.setFont(u8g2_font_7x13_tr); // Set the default font
  screen.setFontPosTop();
  screen.clearBuffer();
  drawBootScreen(); // First frame of the boot screen, the display task continues the animation
  screen.sendBuffer();

  // Create a task to update the display
  xTaskCreatePinnedToCore(
      updateDisplay, /* Function to implement the task */
      "Display",     /* Name of the task */
      10000,         /* Stack size in words */
      NULL,          /* Task input parameter */
      0,             /* Priority of the task */
      &DisplayTask,  /* Task handle */
      1);            /* Core where the task should run */
}
