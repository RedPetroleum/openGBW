#include "config.hpp"
#include "rotary.hpp"
#include "display.hpp"
#include "games.hpp"

// Rotary encoder for user input
AiEsp32RotaryEncoder rotaryEncoder = AiEsp32RotaryEncoder(
    ROTARY_ENCODER_A_PIN,
    ROTARY_ENCODER_B_PIN,
    ROTARY_ENCODER_BUTTON_PIN,
    ROTARY_ENCODER_VCC_PIN,
    ROTARY_ENCODER_STEPS);

// Vars
int encoderDir = -1;  // Direction of the rotary encoder (war 1)
int encoderValue = 0; // Current value of the rotary encoder
static int clickCount = 0;
const unsigned long clickThreshold = 500; // 500ms max interval for rapid clicks

// Incase you can't set something you can exit
void exitToMenu()
{
    if (scaleStatus == STATUS_IN_SUBMENU || scaleStatus == STATUS_INFO_MENU)
    {
        scaleStatus = STATUS_IN_MENU;
        currentSetting = -1;
        Serial.println("Exiting to main menu");
    }
    else if (scaleStatus == STATUS_IN_MENU)
    {
        scaleStatus = STATUS_EMPTY;
        Serial.println("Exiting to empty state");
    }
}

bool debugMode = DEBUG_MODE;
// Handles button clicks on the rotary encoder

void rotary_onButtonClick()
{
    unsigned long currentTime = millis();
    static unsigned long lastTimePressed = 0; // Timestamp of the last button press
    static int clickCount = 0;                // Number of clicks

    // Handle rapid clicks for debug mode
    if (currentTime - lastTimePressed < clickThreshold)
    {
        clickCount++;
    }
    else
    {
        clickCount = 1; // Reset click count if too much time has passed
    }
    lastTimePressed = currentTime;

    // Check for 4 rapid clicks to toggle debug mode
    if (clickCount >= 4)
    {
        debugMode = !debugMode; // Toggle debug mode
        Serial.print("Debug Mode: ");
        Serial.println(debugMode ? "Enabled" : "Disabled");

        // Use the display method from display.cpp
        showDebugModeStatus(debugMode);
        menuItemsCount = debugMode ? 13 : 12;
        clickCount = 0; // Reset the click count
        return;         // Exit early to prevent other actions
    }

    if (scaleStatus == STATUS_GRINDING_FAILED)
    {
        // Leave the failed state, a cup still on the scale starts a new grind
        scaleStatus = STATUS_EMPTY;
        Serial.println("Grinding failure reset");
    }
    else if (scaleStatus == STATUS_EMPTY)
    {
        // Enter the menu when the scale is empty
        scaleStatus = STATUS_IN_MENU;
        currentMenuItem = 0;
        rotaryEncoder.setAcceleration(0);
        Serial.println("Entering Menu...");
    }
    else if (scaleStatus == STATUS_IN_MENU)
    {
        // Navigate through the menu items
        switch (currentMenuItem)
        {
        case 0: // Exit
            scaleStatus = STATUS_EMPTY;
            rotaryEncoder.setAcceleration(100);
            Serial.println("Exited Menu");
            break;
        case 1: // Cup Weight 1 Menu
            scaleStatus = STATUS_IN_SUBMENU;
            currentSetting = 0;
            Serial.println("Cup 1 Menu");
            break;
        case 2: // Cup Weight 2 Menu
            scaleStatus = STATUS_IN_SUBMENU;
            currentSetting = 11;
            Serial.println("Cup 2 Menu");
            break;
        case 3: // Scale Factor Menu
            scaleStatus = STATUS_IN_SUBMENU;
            currentSetting = 10;
            encoderValue = rotaryEncoder.readEncoder();
            rotaryEncoder.setAcceleration(100); // Faster adjustment when turning quickly
            Serial.println("Scale Factor Menu");
            break;
        case 4: // Dead Time Menu
            scaleStatus = STATUS_IN_SUBMENU;
            currentSetting = 2;
            Serial.println("Dead Time Menu");
            break;
        case 5: // Scale Mode Menu
            scaleStatus = STATUS_IN_SUBMENU;
            currentSetting = 3;
            Serial.println("Scale Mode Menu");
            break;
        case 6: // Grinding Mode Menu
            scaleStatus = STATUS_IN_SUBMENU;
            currentSetting = 4;
            Serial.println("Grind Mode Menu");
            break;
        case 7: // Info Menu
            scaleStatus = STATUS_IN_SUBMENU;
            currentSetting = 5;
            Serial.println("Info Menu");
            break;
        case 8: // Sleep Timer Menu
            scaleStatus = STATUS_IN_SUBMENU;
            currentSetting = 8;
            Serial.println("Sleep Timer Menu");
            break;
        case 9: // Style Menu
            currentSetting = STYLE_MENU_SETTING;
            scaleStatus = STATUS_IN_SUBMENU;
            currentStyleMenuItem = 0; // Start on "Exit"
            Serial.println("Style Menu");
            break;
        case 10: // Reset Menu
            scaleStatus = STATUS_IN_SUBMENU;
            currentSetting = 6;
            Serial.println("Reset Menu");
            break;
        case 11: // Games Menu
            currentSetting = GAMES_MENU_SETTING;
            scaleStatus = STATUS_IN_SUBMENU;
            Serial.println("Games Menu");
            break;
        case 12: // Debug Menu
            if (debugMode)
            {
                scaleStatus = STATUS_IN_SUBMENU;
                currentSetting = 9; // Identifier for Debug Menu
                currentDebugMenuItem = 0; // Start on "Exit"
                Serial.println("Entering Debug Menu");
            }
            break;
        }
    }
    else if (scaleStatus == STATUS_IN_SUBMENU)
    {
        // Handle submenu actions based on the current setting
        switch (currentSetting)
        {
        case 0:  // Cup Weight 1 Menu
        case 11: // Cup Weight 2 Menu
        {
            if (scaleWeight > 30)
            { // Ensure cup weight is valid
                bool isFirstCup = currentSetting == 0;
                double &cupWeight = isFirstCup ? setCupWeight : setCupWeight2;
                cupWeight = scaleWeight;
                Serial.println(cupWeight);

                preferences.begin("scale", false);
                preferences.putDouble(isFirstCup ? "cup" : "cup2", cupWeight);
                preferences.end();

                displayLock = true;
                showCupWeightSetScreen(cupWeight); // Show confirmation
                displayLock = false;

                exitToMenu();
            }
            else
            {
                Serial.println("Failsafe: Exiting cup weight menu due to zero weight");
                exitToMenu();
            }
            break;
        }
        case 2: // Dead Time Menu
        {
            preferences.begin("scale", false);
            preferences.putDouble("deadtime", deadTimeEnd);
            preferences.end();
            scaleStatus = STATUS_IN_MENU;
            currentSetting = -1;
            break;
        }
        case 3: // Scale Mode Menu
        {
            preferences.begin("scale", false);
            preferences.putBool("scaleMode", scaleMode);
            preferences.end();
            scaleStatus = STATUS_IN_MENU;
            currentSetting = -1;
            break;
        }
        case 4: // Grinding Mode Menu
        {
            preferences.begin("scale", false);
            preferences.putBool("grindMode", grindMode);
            preferences.end();
            scaleStatus = STATUS_IN_MENU;
            currentSetting = -1;
            break;
        }
        case 5: // Info Menu
        {
            exitToMenu(); // Info is shown while in the submenu, click returns to menu
            break;
        }
        case 6: // Reset Menu
        {
            if (greset)
            {
                preferences.begin("scale", false);
                scaleFactor = (double)LOADCELL_SCALE_FACTOR;
                preferences.putDouble("calibration", scaleFactor);
                setWeight = (double)COFFEE_DOSE_WEIGHT;
                preferences.putDouble("setWeight", (double)COFFEE_DOSE_WEIGHT);
                deadTimeEnd = (double)DEAD_TIME_END_DEFAULT;
                preferences.putDouble("deadtime", (double)DEAD_TIME_END_DEFAULT);
                setCupWeight = (double)CUP_WEIGHT;
                preferences.putDouble("cup", (double)CUP_WEIGHT);
                setCupWeight2 = (double)CUP_WEIGHT_2;
                preferences.putDouble("cup2", (double)CUP_WEIGHT_2);
                scaleMode = false;
                preferences.putBool("scaleMode", false);
                grindMode = true;
                preferences.putBool("grindMode", true);
                grindScreenInvert = false;
                preferences.putBool("grindInvert", false);
                shotCount = SHOT_COUNT_DEFAULT;
                preferences.putUInt("shotCount", shotCount);
                loadcell.set_scale(scaleFactor);
                preferences.end();
            }
            scaleStatus = STATUS_IN_MENU;
            currentSetting = -1;
            break;
        }
        case 8: // Sleep Timer Menu
        {
            preferences.begin("scale", false);
            preferences.putInt("sleepTime", sleepTime);
            preferences.end();
            scaleStatus = STATUS_IN_MENU;
            currentSetting = -1;
            break;
        }
        case 9: // Debug Menu
        {
            int newValue = rotaryEncoder.readEncoder();
            int debugOption = (newValue - encoderValue) % 3;               // Cycle through options
            debugOption = debugOption < 0 ? 3 + debugOption : debugOption; // Wrap negative values
            encoderValue = newValue;

            if (rotaryEncoder.isEncoderButtonClicked())
            {
                switch (debugOption)
                {
                case 0: // Simulate Grinding
                    Serial.println("Simulating Grinding...");
                    scaleStatus = STATUS_GRINDING_IN_PROGRESS;
                    startedGrindingAt = millis();
                    setWeight = 20.0;     // Example weight
                    cupWeightEmpty = 5.0; // Example cup weight
                    break;

                case 1: // Show Weight History
                    Serial.println("Displaying Weight History...");
                    // Add logic to display weight history
                    break;

                case 2: // Reset Shot Count
                    Serial.println("Resetting Shot Count...");
                    shotCount = 0;
                    preferences.begin("scale", false);
                    preferences.putUInt("shotCount", shotCount);
                    preferences.end();
                    break;
                }
            }
            exitToMenu();
            break;
        }
        case 10: // Scale Factor Menu
        {
            preferences.begin("scale", false);
            preferences.putDouble("calibration", scaleFactor);
            preferences.end();
            rotaryEncoder.setAcceleration(0);
            scaleStatus = STATUS_IN_MENU;
            currentSetting = -1;
            break;
        }
        case GAMES_MENU_SETTING: // Games Menu
        {
            gamesMenuOnClick(); // Starts the selected game or returns to the main menu
            break;
        }
        case STYLE_MENU_SETTING: // Style Menu
        {
            styleMenuOnClick(); // Opens the selected style setting or returns to the main menu
            break;
        }
        case GRIND_SCREEN_SETTING: // Grinding screen style
        {
            preferences.begin("scale", false);
            preferences.putBool("grindInvert", grindScreenInvert);
            preferences.end();
            currentSetting = STYLE_MENU_SETTING; // Back to the Style Menu
            break;
        }
        case WEIGHT_DATA_SETTING: // Weight Data view
        case GRIND_HISTORY_SETTING: // Weight History view
        {
            currentSetting = 9; // Back to the Debug Menu
            break;
        }
        }
    }
}

// Handles rotary encoder input for menu navigation and adjustments
void rotary_loop()
{
    if (rotaryEncoder.encoderChanged())
    {
        if (displayAsleep())
        {
            // Turning only wakes the display, the turn itself changes nothing
            Serial.println("Screen waking due to rotary movement...");
            encoderValue = rotaryEncoder.readEncoder();
            wakeScreen();
            return;
        }
        lastActivityAt = millis(); // Turning keeps the display awake
        switch (scaleStatus)
        {
        case STATUS_EMPTY:
        {
            // Adjust weight when in scale mode
            int newValue = rotaryEncoder.readEncoder();
            setWeight += ((float)newValue - (float)encoderValue) / 10 * encoderDir;
            encoderValue = newValue;
            preferences.begin("scale", false);
            preferences.putDouble("setWeight", setWeight);
            preferences.end();
            break;
        }
        case STATUS_GAME:
        {
            // Move the ship, positive steps move it down like in the menus
            int newValue = rotaryEncoder.readEncoder();
            gameOnTurn((newValue - encoderValue) * -encoderDir);
            encoderValue = newValue;
            break;
        }
        case STATUS_IN_MENU:
        {
            // Navigate through menu items
            int newValue = rotaryEncoder.readEncoder();
            currentMenuItem = (currentMenuItem + (newValue - encoderValue) * -encoderDir) % menuItemsCount;
            currentMenuItem = currentMenuItem < 0 ? menuItemsCount + currentMenuItem : currentMenuItem;
            encoderValue = newValue;
            Serial.println(currentMenuItem);
            break;
        }
        case STATUS_IN_SUBMENU:
        {
            int newValue = rotaryEncoder.readEncoder();
            if (currentSetting == 0 || currentSetting == 11)
            { // Cup weight menus: turning leaves without saving
                encoderValue = newValue;
                exitToMenu();
            }
            else if (currentSetting == GAMES_MENU_SETTING)
            { // Games Menu
                gamesMenuOnTurn((newValue - encoderValue) * -encoderDir);
                encoderValue = newValue;
            }
            else if (currentSetting == STYLE_MENU_SETTING)
            { // Style Menu
                styleMenuOnTurn((newValue - encoderValue) * -encoderDir);
                encoderValue = newValue;
            }
            else if (currentSetting == GRIND_SCREEN_SETTING)
            { // Grinding screen style, turning switches between the two
                grindScreenInvert = !grindScreenInvert;
                encoderValue = newValue;
            }
            else if (currentSetting == GRIND_HISTORY_SETTING)
            { // Weight History: scroll through the recorded grinds
                grindHistoryScroll += (newValue - encoderValue) * -encoderDir;
                encoderValue = newValue;
                grindHistoryScroll = constrain(grindHistoryScroll, 0, max(0, grindHistoryCount - GRIND_HISTORY_ROWS));
            }
            else if (currentSetting == 2)
            { // Dead time menu, a hundredth of a second per detent
                deadTimeEnd += ((float)newValue - (float)encoderValue) * encoderDir / 100;
                encoderValue = newValue;
                deadTimeEnd = constrain(deadTimeEnd, DEAD_TIME_MIN, DEAD_TIME_MAX);
            }
            else if (currentSetting == 10)
            { // Scale factor menu, applied immediately so the live weight can be checked
                int delta = newValue - encoderValue;
                encoderValue = newValue;
                if (abs(delta) < 1000) // Ignore jumps from encoder boundary wrap-around
                {
                    scaleFactor += delta * encoderDir;
                    if (scaleFactor < 1)
                        scaleFactor = 1; // Scale factor must never be zero
                    loadcell.set_scale(scaleFactor);
                }
            }
            else if (currentSetting == 3)
            {
                scaleMode = !scaleMode;
            }
            else if (currentSetting == 4)
            {
                grindMode = !grindMode;
            }
            else if (currentSetting == 6)
            {
                greset = !greset;
            }
            else if (currentSetting == 8)
            {                                                  // Sleep Timer menu
                sleepTime += (newValue - encoderValue) * encoderDir * 1000; // Adjust by seconds
                if (sleepTime < 5000)
                    sleepTime = 5000; // Minimum sleep time: 5 seconds
                if (sleepTime > 600000)
                    sleepTime = 600000; // Maximum sleep time: 10 minutes
                encoderValue = newValue;
            }
            else if (scaleStatus == STATUS_IN_SUBMENU && currentSetting == 9) // Debug Menu
            {
                int newValue = rotaryEncoder.readEncoder();
                currentDebugMenuItem = (currentDebugMenuItem + (newValue - encoderValue) * -encoderDir) % debugMenuItemsCount;
                currentDebugMenuItem = currentDebugMenuItem < 0 ? debugMenuItemsCount + currentDebugMenuItem : currentDebugMenuItem;
                encoderValue = newValue;
                showDebugMenu(); // Update the Debug Menu display
            }
            break;
        }
        }
    }
    if (rotaryEncoder.isEncoderButtonClicked())
    {
        if (displayAsleep())
        {
            // A click only wakes the display
            Serial.println("Screen waking due to button click...");
            wakeScreen();
            return;
        }
        lastActivityAt = millis(); // Clicking keeps the display awake
        if (scaleStatus == STATUS_GAME)
        {
            gameOnClick(); // Handled by the game, bypasses the rapid click debug toggle
        }
        else if (scaleStatus == STATUS_IN_SUBMENU && currentSetting == 9) // Debug Menu
        {
            handleDebugMenuAction(); // Perform the selected debug menu action
        }
        else
        {
            rotary_onButtonClick(); // Existing button click handling
        }
    }
}

// ISR for reading encoder changes
void readEncoderISR()
{
    rotaryEncoder.readEncoder_ISR();
}