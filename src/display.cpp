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
#define BOOT_BAR_Y 54
#define BOOT_BAR_HEIGHT 9

// Progress bar of the grinding screen, filled towards the set weight
#define GRIND_BAR_Y 39
#define GRIND_BAR_HEIGHT 12
#define GRIND_BAR_EASING 8.0f // how fast the drawn bar follows the reading (1/s), the scale only reports twice a second

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
int menuItemsCount = debugMode ? 12 : 11;      // Total number of menu items

 // Menu items for settings and calibration
MenuItem menuItems[12] = {
    {0, false, "Exit", 0},
    {1, false, "Cup Weight 1", 1, &setCupWeight},
    {2, false, "Cup Weight 2", 1, &setCupWeight2},
    {3, false, "Scale Factor", 1, &scaleFactor},
    {4, false, "Offset", 0.1, &offset},
    {5, false, "Scale Mode", 0},
    {6, false, "Grinding Mode", 0},
    {7, false, "Info Menu", 0},
    {8, false, "Sleep Timer", 0},
    {9, false, "Reset", 0},
    {10, false, "Games", 0},
    // Debug menu placeholder (conditional)
    {11, false, "Debug Menu", 0} // Visible only if debugMode is true
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
    {2, false, "Weight Chart", 0},
    {3, false, "Weight History", 0},
    {4, false, "Zero Shot Count", 0}
};

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
        menuItemsCount = 12; // Include Debug Menu
    } else {
        menuItemsCount = 11; // Exclude Debug Menu
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

// Function to display the offset adjustment menu
void showOffsetMenu()
{
  char buf[16];
  screen.clearBuffer();
  screen.setFontPosTop();
  screen.setFont(u8g2_font_7x14B_tf);           // Set the font for the menu title
  CenterPrintToScreen("Adjust offset", 0);      // Print the menu title
  screen.setFont(u8g2_font_7x13_tr);            // Set the font for the offset value
  snprintf(buf, sizeof(buf), "%3.2fg", offset); // Format the offset value
  CenterPrintToScreen(buf, 28);                 // Print the offset value
  screen.sendBuffer();                          // Send the buffer to the display
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
  snprintf(buf, sizeof(buf), "Weight: %3.1fg", shownWeight);
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
  snprintf(buf, sizeof(buf), "%3.1fg", shownWeight); // Format the scale weight
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
void showWeightChart()
{
  double values[WEIGHT_HISTORY_SIZE];
  int count = 0;
  int64_t oldestTimestamp = millis();
  // Samples are delivered from newest to oldest
  weightHistory.executeOnSamplesSince(0, [&](double value, int64_t ms) {
    if (count < WEIGHT_HISTORY_SIZE)
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
// The header names the columns of the current page: time and offset, or target, actual weight and difference.
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
  RightPrintToScreen(grindHistoryPage == 0 ? "Time  Offset" : "Targ  Act Diff", 0);
  for (int row = 0; row < GRIND_HISTORY_ROWS; row++)
  {
    int index = grindHistoryScroll + row;
    if (index >= grindHistoryCount)
      break;
    GrindRecord &record = grindHistory[index];
    snprintf(buf, sizeof(buf), "#%lu", (unsigned long)record.shot);
    LeftPrintToScreen(buf, 12 + row * 10);
    if (grindHistoryPage == 0)
      snprintf(buf, sizeof(buf), "%.1fs %6.2fg", record.duration, record.offset);
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

    // Display offset
    snprintf(buf, sizeof(buf), "Offset: %3.2fg", offset);
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
    showOffsetMenu();
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
  else if (currentSetting == WEIGHT_CHART_SETTING)
  {
    showWeightChart();
  }
  else if (currentSetting == GRIND_HISTORY_SETTING)
  {
    showGrindHistory();
  }
  else if (currentSetting == GAMES_MENU_SETTING)
  {
    showGamesMenu();
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

    case 2: // Show Weight Chart
        Serial.println("Displaying Weight Chart...");
        currentSetting = WEIGHT_CHART_SETTING; // Graph is drawn by the display task, click returns to the Debug Menu
        return;

    case 3: // Show Weight History (time, offset, target and actual weight of the last grinds)
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
  screenY = BOOT_CENTER_Y - toPixel(tiltedY * scale);
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
static void drawBootScreen()
{
  if (bootStartedAt == 0)
  {
    bootStartedAt = millis();
  }
  unsigned long elapsed = millis() - bootStartedAt;
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

  // The bar fills evenly over the time the scale needs to get ready, the fill keeps one pixel of air
  // to the frame
  float progress = min(1.0f, elapsed / (float)BOOT_READY_MS);
  screen.drawFrame(0, BOOT_BAR_Y, 128, BOOT_BAR_HEIGHT);
  int fill = toPixel(progress * (128 - 4));
  if (fill > 0)
  {
    screen.drawBox(2, BOOT_BAR_Y + 2, fill, BOOT_BAR_HEIGHT - 4);
  }
}

static float grindBarFill = 0;        // Fill of the progress bar as drawn, follows the reading smoothly
static unsigned long grindBarDrawnAt = 0; // Time of the last progress bar update

// Draws how much of the set weight is in the cup. The reading only arrives twice a second, so the bar
// follows it smoothly instead of jumping. It never fills completely: the grinder stops before the set
// weight and the dose counts as reached only once the reading has settled, which is the next screen.
static void drawGrindProgress(float progress)
{
  unsigned long now = millis();
  float dt = min((now - grindBarDrawnAt) / 1000.0f, 0.1f);
  grindBarDrawnAt = now;
  grindBarFill += (progress - grindBarFill) * min(1.0f, dt * GRIND_BAR_EASING);

  screen.drawFrame(0, GRIND_BAR_Y, 128, GRIND_BAR_HEIGHT);
  int inside = 128 - 2; // the fill sits directly against the frame, without a gap
  int fill = constrain((int)(grindBarFill * inside), 0, inside - 1); // always one pixel short of full
  if (fill > 0)
  {
    screen.drawBox(1, GRIND_BAR_Y + 1, fill, GRIND_BAR_HEIGHT - 2);
  }
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

      screen.setFontPosTop();
      screen.setFont(u8g2_font_7x13_tr);
      CenterPrintToScreen(verifying ? "Verifying..." : "Grinding...", 0);

      screen.setFontPosCenter();
      screen.setFont(u8g2_font_7x14B_tf);
      screen.setCursor(3, 26);
      snprintf(buf, sizeof(buf), "%3.1fg", shownWeight - cupWeightEmpty);
      screen.print(buf);

      screen.setFontPosCenter();
      screen.setFont(u8g2_font_unifont_t_symbols);
      screen.drawGlyph(64, 26, 0x2794);

      screen.setFontPosCenter();
      screen.setFont(u8g2_font_7x14B_tf);
      screen.setCursor(84, 26);
      snprintf(buf, sizeof(buf), "%3.1fg", setWeight);
      screen.print(buf);

      drawGrindProgress(setWeight > 0 ? (shownWeight - cupWeightEmpty) / setWeight : 0);

      screen.setFontPosBottom();
      screen.setFont(u8g2_font_7x13_tr);
      // While verifying the grinding time stands still, it is the time the grind took
      double grindSeconds = verifying ? (double)(finishedGrindingAt - startedGrindingAt) / 1000
                                      : (startedGrindingAt > 0 ? (double)(millis() - startedGrindingAt) / 1000 : 0);
      snprintf(buf, sizeof(buf), "%3.1fs", grindSeconds);
      CenterPrintToScreen(buf, 64);
    }
    else if (scaleStatus == STATUS_EMPTY)
    {
      grindBarFill = 0; // the next grind starts with an empty bar

      screen.setFontPosTop();
      screen.setFont(u8g2_font_7x13_tr);
      CenterPrintToScreen("Weight:", 0);

      screen.setFont(u8g2_font_7x14B_tf);
      screen.setFontPosCenter();
      WeightPrintToScreen(abs(shownWeight), 32);

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
      screen.setFontPosTop();
      screen.setFont(u8g2_font_7x13_tr);
      screen.setCursor(0, 0);
      CenterPrintToScreen("Grinding finished", 0);

      screen.setFontPosCenter();
      screen.setFont(u8g2_font_7x14B_tf);
      screen.setCursor(3, 32);
      snprintf(buf, sizeof(buf), "%3.1fg", shownWeight - cupWeightEmpty);
      screen.print(buf);

      screen.setFontPosCenter();
      screen.setFont(u8g2_font_unifont_t_symbols);
      screen.drawGlyph(64, 32, 0x2794);

      screen.setFontPosCenter();
      screen.setFont(u8g2_font_7x14B_tf);
      screen.setCursor(84, 32);
      snprintf(buf, sizeof(buf), "%3.1fg", setWeight);
      screen.print(buf);

      screen.setFontPosBottom();
      screen.setFont(u8g2_font_7x13_tr);
      screen.setCursor(64, 64);
      snprintf(buf, sizeof(buf), "%3.1fs", (double)(finishedGrindingAt - startedGrindingAt) / 1000);
      CenterPrintToScreen(buf, 64);
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
