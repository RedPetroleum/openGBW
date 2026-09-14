#pragma once

#include "config.hpp"
#include "display.hpp"

// Shared by all games, only included by the game sources

#define GAME_FRAME_MS 30               // target frame time (about 33 fps)
#define GAME_MAX_FRAME_DT 0.1f         // longer frames slow the game down instead of letting objects jump
#define GAME_OVER_INPUT_DELAY_MS 800   // clicks are ignored this long after a game ended (no accidental retry)

// Playfield below the header line with bars and score
#define GAME_FIELD_TOP 10
#define GAME_FIELD_BOTTOM 63

// Zero point of the scale while playing
#define GAME_BASELINE_FOLLOW 4.0f      // how fast the zero point follows scale drift while not pressed (1/s)
#define GAME_RETARE_TIME 3.0f          // a press that stays this many seconds ...
#define GAME_RETARE_TOLERANCE 1.0f     // ... within this many grams is not a finger but an offset: the zero point is reset

// One game: reset() starts a new run, frame() updates and draws one frame.
// frame() returns true while the game is actively played, which keeps the display awake.
struct GameDefinition
{
  const char *name;
  void (*reset)();
  bool (*frame)(float dt, int steps, bool click, unsigned long now);
};

void cometBlasterReset();
bool cometBlasterFrame(float dt, int steps, bool click, unsigned long now);
void curveTracerReset();
bool curveTracerFrame(float dt, int steps, bool click, unsigned long now);
void doomReset();
bool doomFrame(float dt, int steps, bool click, unsigned long now);

// Scale pressure
void gameResetPressure();                                    // takes the current reading as zero point
float gamePressedWeight(float dt, float threshold, bool retare, bool pull = false); // grams pressed, see games.cpp

// Screens and flow
int gameSelectChoice(int steps, int choice); // knob selects the first (0) or second (1) option
void gameDrawChoiceScreen(const char *title, const char *text, const char *firstOption, const char *secondOption, int choice);
void gameDrawCountdown(const char *title, const char *line1, const char *line2, unsigned long elapsed, unsigned long duration);
void gameExit();                             // back to the Games submenu

// Best scores in the preferences
unsigned long gameLoadBestScore(const char *key);
void gameSaveBestScore(const char *key, unsigned long score);

// Drawing helpers
static inline float mix(float from, float to, float amount)
{
  return from + (to - from) * amount;
}

static inline int toPixel(float value)
{
  return (int)floor(value + 0.5f);
}

// Bitmaps have one row per line, the highest used bit is the leftmost pixel
static inline bool bitmapPixelSet(const uint16_t *rows, int width, int row, int col)
{
  return rows[row] & (1 << (width - 1 - col));
}

static inline void drawPixelClipped(int x, int y, int top = GAME_FIELD_TOP)
{
  if (x >= 0 && x < 128 && y >= top && y <= GAME_FIELD_BOTTOM)
    screen.drawPixel(x, y);
}

static inline void drawBitmap(const uint16_t *rows, int width, int height, int x, int y, int top = GAME_FIELD_TOP)
{
  for (int row = 0; row < height; row++)
  {
    for (int col = 0; col < width; col++)
    {
      if (bitmapPixelSet(rows, width, row, col))
        drawPixelClipped(x + col, y + row, top);
    }
  }
}
