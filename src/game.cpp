#include "config.hpp"
#include "display.hpp"
#include "game.hpp"
#include "game_common.hpp"

// Add new games here, they appear in the Games submenu in this order
static const GameDefinition games[] = {
    {"Comet Blaster", cometBlasterReset, cometBlasterFrame},
    {"Curve Tracer", curveTracerReset, curveTracerFrame},
    {"Doom Nano", doomReset, doomFrame},
};
static const int gameCount = sizeof(games) / sizeof(games[0]);

static int gamesMenuItem = 0;     // 0 = Back, 1.. = games
static int activeGame = 0;        // index into games
static unsigned long lastFrameAt;

// Zero point of the scale
static float weightBaseline;      // scale reading that counts as "not pressed"
static float steadyMin;           // lowest and highest reading since the press became steady
static float steadyMax;
static float steadyTime;          // seconds the pressed reading has stayed within GAME_RETARE_TOLERANCE

// Input from the rotary task, consumed by the display task
static volatile int pendingSteps = 0;
static volatile bool pendingClick = false;
static volatile bool restartRequested = false;

static const char *gamesMenuName(int item)
{
  return item == 0 ? "Back" : games[item - 1].name;
}

void showGamesMenu()
{
  int itemCount = gameCount + 1;
  screen.clearBuffer();
  screen.setFontPosTop();
  screen.setFont(u8g2_font_7x14B_tf);
  CenterPrintToScreen("Games", 0);
  screen.setFont(u8g2_font_7x13_tr);
  LeftPrintToScreen(gamesMenuName((gamesMenuItem + itemCount - 1) % itemCount), 19);
  LeftPrintActiveToScreen(gamesMenuName(gamesMenuItem), 35);
  LeftPrintToScreen(gamesMenuName((gamesMenuItem + 1) % itemCount), 51);
  screen.sendBuffer();
}

void gamesMenuOnTurn(int steps)
{
  int itemCount = gameCount + 1;
  gamesMenuItem = ((gamesMenuItem + steps) % itemCount + itemCount) % itemCount;
}

void gamesMenuOnClick()
{
  if (gamesMenuItem == 0)
  {
    scaleStatus = STATUS_IN_MENU;
    currentSetting = -1;
    return;
  }
  pendingSteps = 0;
  pendingClick = false;
  activeGame = gamesMenuItem - 1;
  restartRequested = true; // set before the status so the display task never runs an old game
  scaleStatus = STATUS_GAME;
  Serial.printf("Starting %s\n", games[activeGame].name);
}

void gameOnTurn(int steps)
{
  pendingSteps += steps;
}

void gameOnClick()
{
  pendingClick = true;
}

void gameExit()
{
  currentSetting = GAMES_MENU_SETTING; // set before the status so the display never shows another submenu
  scaleStatus = STATUS_IN_SUBMENU;
  Serial.println("Exited game");
}

void gameResetPressure()
{
  weightBaseline = scaleWeight;
  steadyTime = 0;
}

// Returns how many grams the scale is pressed. Below the threshold the zero point follows scale drift.
// With retare, a reading that stays constant above the threshold is taken as the new zero point.
// With pull, pulling the scale up counts as input too (negative weight): the zero point only follows
// within the threshold in both directions and a steady pull is retared like a steady press.
float gamePressedWeight(float dt, float threshold, bool retare, bool pull)
{
  float reading = scaleWeight;
  float weight = reading - weightBaseline;
  if (weight < threshold && (!pull || weight > -threshold))
  {
    weightBaseline += (reading - weightBaseline) * min(1.0f, dt * GAME_BASELINE_FOLLOW);
    steadyTime = 0;
    return weight;
  }
  if (!retare)
    return weight;

  // A finger never presses perfectly steady. A reading that stays constant above the threshold
  // is an offset (scale top stuck after a hard press, creep, something placed on the scale), so tare again
  if (steadyTime == 0 || reading < steadyMax - GAME_RETARE_TOLERANCE || reading > steadyMin + GAME_RETARE_TOLERANCE)
  {
    steadyMin = steadyMax = reading;
    steadyTime = dt; // non-zero marks the window as started
  }
  else
  {
    steadyMin = min(steadyMin, reading);
    steadyMax = max(steadyMax, reading);
    steadyTime += dt;
  }
  if (steadyTime >= GAME_RETARE_TIME)
  {
    Serial.printf("Game: retared, steady offset of %.1fg\n", weight);
    weightBaseline = reading;
    steadyTime = 0;
    return 0;
  }
  return weight;
}

int gameSelectChoice(int steps, int choice)
{
  if (steps > 0)
    return 1;
  if (steps < 0)
    return 0;
  return choice;
}

// Screen with a title, one line of text and two options selected with the knob
void gameDrawChoiceScreen(const char *title, const char *text, const char *firstOption, const char *secondOption, int choice)
{
  screen.setFontPosTop();
  screen.setFont(u8g2_font_7x14B_tf);
  CenterPrintToScreen(title, 0);
  screen.setFont(u8g2_font_6x10_tr);
  CenterPrintToScreen(text, 16);
  screen.setFont(u8g2_font_7x13_tr);
  if (choice == 0)
  {
    LeftPrintActiveToScreen(firstOption, 30);
    LeftPrintToScreen(secondOption, 46);
  }
  else
  {
    LeftPrintToScreen(firstOption, 30);
    LeftPrintActiveToScreen(secondOption, 46);
  }
}

// Title, two lines of instructions and a countdown from 3, drawn over the playfield
void gameDrawCountdown(const char *title, const char *line1, const char *line2, unsigned long elapsed, unsigned long duration)
{
  char buf[4];
  screen.setFontPosTop();
  screen.setFont(u8g2_font_7x14B_tf);
  CenterPrintToScreen(title, 12);
  screen.setFont(u8g2_font_5x7_tr);
  CenterPrintToScreen(line1, 30);
  CenterPrintToScreen(line2, 39);
  screen.setFont(u8g2_font_7x14B_tf);
  snprintf(buf, sizeof(buf), "%lu", 3 - min(elapsed, duration - 1) * 3 / duration);
  CenterPrintToScreen(buf, 49);
}

unsigned long gameLoadBestScore(const char *key)
{
  preferences.begin("scale", false);
  unsigned long score = preferences.getUInt(key, 0);
  preferences.end();
  return score;
}

void gameSaveBestScore(const char *key, unsigned long score)
{
  preferences.begin("scale", false);
  preferences.putUInt(key, score);
  preferences.end();
}

void gameLoop()
{
  const GameDefinition &game = games[activeGame];
  if (restartRequested)
  {
    restartRequested = false;
    game.reset();
    lastFrameAt = millis();
  }

  unsigned long elapsed = millis() - lastFrameAt;
  if (elapsed < GAME_FRAME_MS)
    delay(GAME_FRAME_MS - elapsed);
  unsigned long now = millis();
  float dt = min((now - lastFrameAt) / 1000.0f, GAME_MAX_FRAME_DT);
  lastFrameAt = now;

  int steps = pendingSteps;
  pendingSteps -= steps;
  bool click = pendingClick;
  pendingClick = false;

  // Keep the display awake while playing; on the pause and Game Over screens only on input
  bool playing = game.frame(dt, steps, click, now);
  if (playing || steps != 0 || click)
    lastActivityAt = now;
}
