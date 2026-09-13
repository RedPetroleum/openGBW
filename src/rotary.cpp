#include "config.hpp"
#include "rotary.hpp"
#include "display.hpp"

// Rotary encoder for user input
AiEsp32RotaryEncoder rotaryEncoder = AiEsp32RotaryEncoder(
    ROTARY_ENCODER_A_PIN,
    ROTARY_ENCODER_B_PIN,
    ROTARY_ENCODER_BUTTON_PIN,
    ROTARY_ENCODER_VCC_PIN,
    ROTARY_ENCODER_STEPS);

// Vars
int encoderDir = 1;   // Direction of the rotary encoder
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
        menuItemsCount = debugMode ? 10 : 9;
        clickCount = 0; // Reset the click count
        return;         // Exit early to prevent other actions
    }

    if (scaleStatus == STATUS_EMPTY)
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
        case 0: // Cup Weight Menu
            scaleStatus = STATUS_IN_SUBMENU;
            currentSetting = 0;
            Serial.println("Cup Menu");
            break;
        case 1: // Calibration Menu
        {
            scaleStatus = STATUS_IN_SUBMENU;
            currentSetting = 1;
            Serial.println("Calibration Menu");
            break;
        }
        case 2: // Offset Menu
            scaleStatus = STATUS_IN_SUBMENU;
            currentSetting = 2;
            Serial.println("Offset Menu");
            break;
        case 3: // Scale Mode Menu
            scaleStatus = STATUS_IN_SUBMENU;
            currentSetting = 3;
            Serial.println("Scale Mode Menu");
            break;
        case 4: // Grinding Mode Menu
            scaleStatus = STATUS_IN_SUBMENU;
            currentSetting = 4;
            Serial.println("Grind Mode Menu");
            break;
        case 5: // Info Menu
            scaleStatus = STATUS_IN_SUBMENU;
            currentSetting = 5;
            Serial.println("Info Menu");
            break;
        case 6: // Sleep Timer Menu
            scaleStatus = STATUS_IN_SUBMENU;
            currentSetting = 8;
            Serial.println("Sleep Timer Menu");
            break;
        case 7: // Exit
            scaleStatus = STATUS_EMPTY;
            rotaryEncoder.setAcceleration(100);
            Serial.println("Exited Menu");
            break;
        case 8: // Reset Menu
            scaleStatus = STATUS_IN_SUBMENU;
            currentSetting = 6;
            Serial.println("Reset Menu");
            break;
        case 9: // Debug Menu
            if (debugMode)
            {
                scaleStatus = STATUS_IN_SUBMENU;
                currentSetting = 9; // Identifier for Debug Menu
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
        case 0: // Cup Weight Menu
        {
            if (scaleWeight > 30)
            { // Ensure cup weight is valid
                setCupWeight = scaleWeight;
                Serial.println(setCupWeight);

                preferences.begin("scale", false);
                preferences.putDouble("cup", setCupWeight);
                preferences.end();

                displayLock = true;
                showCupWeightSetScreen(setCupWeight); // Show confirmation
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
        case 1: // Calibration Menu
        {
            double newCalibrationValue = preferences.getDouble("calibration", 1.0) * (scaleWeight / 100);
            preferences.begin("scale", false);
            preferences.putDouble("calibration", newCalibrationValue);
            preferences.end();
            loadcell.set_scale(newCalibrationValue);
            scaleStatus = STATUS_IN_MENU;
            currentSetting = -1;
            break;
        }
        case 2: // Offset Menu
        {
            preferences.begin("scale", false);
            preferences.putDouble("offset", offset);
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
            displayLock = true;
            showInfoMenu(); // Display info menu
            delay(3000);
            displayLock = false;
            exitToMenu();
            break;
        }
        case 6: // Reset Menu
        {
            if (greset)
            {
                preferences.begin("scale", false);
                preferences.putDouble("calibration", (double)LOADCELL_SCALE_FACTOR);
                setWeight = (double)COFFEE_DOSE_WEIGHT;
                preferences.putDouble("setWeight", (double)COFFEE_DOSE_WEIGHT);
                offset = (double)COFFEE_DOSE_OFFSET;
                preferences.putDouble("offset", (double)COFFEE_DOSE_OFFSET);
                setCupWeight = (double)CUP_WEIGHT;
                preferences.putDouble("cup", (double)CUP_WEIGHT);
                scaleMode = false;
                preferences.putBool("scaleMode", false);
                grindMode = false;
                preferences.putBool("grindMode", false);
                preferences.putUInt("shotCount", 0);
                loadcell.set_scale((double)LOADCELL_SCALE_FACTOR);
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
        }
    }
}

// Handles rotary encoder input for menu navigation and adjustments
void rotary_loop()
{
    if (rotaryEncoder.encoderChanged())
    {
        // Wake the screen if it's asleep
        if (millis() - lastSignificantWeightChangeAt > sleepTime)
        {
            Serial.println("Screen waking due to rotary movement...");
            wakeScreen();
        }
        switch (scaleStatus)
        {
        case STATUS_EMPTY:
        {
            if (screenJustWoke)
            {
                // Skip modifying the set weight if the screen just woke up
                screenJustWoke = false; // Reset the flag
                break;
            }
            // Adjust weight when in scale mode
            int newValue = rotaryEncoder.readEncoder();
            setWeight += ((float)newValue - (float)encoderValue) / 10 * encoderDir;
            encoderValue = newValue;
            preferences.begin("scale", false);
            preferences.putDouble("setWeight", setWeight);
            preferences.end();
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
            if (currentSetting == 2)
            { // Offset menu
                offset += ((float)newValue - (float)encoderValue) * encoderDir / 100;
                encoderValue = newValue;
                if (abs(offset) >= setWeight)
                {
                    offset = setWeight; // Prevent nonsensical offsets
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
                sleepTime += (newValue - encoderValue) * 1000; // Adjust by seconds
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
        if (scaleStatus == STATUS_IN_SUBMENU && currentSetting == 9) // Debug Menu
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