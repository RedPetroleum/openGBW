#include "common.hpp"

// Curve Tracer: a line scrolls in from the right and has to be followed with the pressure on the scale.
// More pressure moves the pen up. Staying on the line scores points, leaving it drains the grip bar.

// Timing
#define TRACER_READY_MS 2400            // countdown before a run starts
#define TRACER_END_MS 1200              // the last frame stays visible this long before the Game Over screen

// Pressure
#define TRACER_RANGE_WEIGHT 150.0f      // grams shown over the full height of the playfield
#define TRACER_TARGET_MIN 15.0f         // the line stays between these weights
#define TRACER_TARGET_MAX 130.0f
#define TRACER_START_WEIGHT 40.0f       // flat line at the start to find the right pressure
#define TRACER_START_FLAT 50            // pixels of flat line ahead of the pen at the start
#define TRACER_ZERO_THRESHOLD 5.0f      // below this the zero point follows scale drift

// Pen
#define TRACER_PEN_X 30                 // column of the pen, the line ahead is visible to the right
#define TRACER_PEN_EASING 25.0f         // smooths the steps between scale readings (1/s)
#define TRACER_PEN_RADIUS 2

// Difficulty
#define TRACER_SPEED_START 20.0f        // scroll speed in pixels per second ...
#define TRACER_SPEED_INCREASE 2.1f      // ... increasing by this every second ...
#define TRACER_SPEED_MAX 90.0f          // ... up to this, reached after 33 seconds
#define TRACER_TOLERANCE_START 20.0f    // allowed deviation from the line in grams at the start ...
#define TRACER_TOLERANCE_DECREASE 0.1f  // ... shrinking by this every second ...
#define TRACER_TOLERANCE_MIN 10.0f      // ... down to this, reached after 100 seconds
#define TRACER_LATENCY_MS 200           // the pen lags behind the finger, so it may match the line of this long ago
#define TRACER_SHAPE_TIME 60.0f         // seconds until ramps are steepest and waves highest and shortest
#define TRACER_RAMP_SLOPE_START 1.5f    // steepness of ramps in grams per pixel at the start ...
#define TRACER_RAMP_SLOPE_END 4.0f      // ... and after TRACER_SHAPE_TIME
#define TRACER_WAVE_HEIGHT_START 25.0f  // highest wave in grams at the start ...
#define TRACER_WAVE_HEIGHT_END 60.0f    // ... and after TRACER_SHAPE_TIME
#define TRACER_WAVE_LENGTH_START 90.0f  // wave length in pixels at the start ...
#define TRACER_WAVE_LENGTH_END 40.0f    // ... and after TRACER_SHAPE_TIME

// Grip and score
#define TRACER_GRIP_MAX 100.0f
#define TRACER_GRIP_DRAIN 20.0f         // grip lost per second off the line ...
#define TRACER_GRIP_DRAIN_PER_GRAM 2.0f // ... plus this per gram beyond the tolerance
#define TRACER_GRIP_REGEN 8.0f          // grip regained per second on the line
#define TRACER_GRACE_TIME 3.0f          // seconds after the start without losing grip
#define TRACER_POINTS_PER_SECOND 10.0f  // points per second on the line, half at its edge, full in its center
#define TRACER_COMBO_TIME 4.0f          // seconds on the line to raise the multiplier by one ...
#define TRACER_COMBO_MAX 8              // ... up to this
#define TRACER_COMBO_FORGIVE 0.3f       // leaving the line shorter than this keeps the multiplier

#define TRACER_COLUMNS 128
#define TRACER_NO_TRACE -1.0f

enum TracerState
{
  TRACER_READY,
  TRACER_RUNNING,
  TRACER_PAUSED,
  TRACER_ENDING,
  TRACER_OVER
};

enum SegmentType
{
  SEGMENT_PLATEAU, // hold the pressure steady
  SEGMENT_RAMP,    // smooth change to a new pressure
  SEGMENT_WAVE     // up and back down (or down and back up) one or more times
};

static TracerState tracerState = TRACER_READY;
static float targetColumns[TRACER_COLUMNS]; // weight of the line in each screen column
static float traceColumns[TRACER_COLUMNS];  // weight of the pen when it passed the column, left of the pen only
static float scrollRemainder;               // fraction of a pixel scrolled
static float runTime;                       // seconds, only counts while running, drives the difficulty
static float penWeight;                     // smoothed pressure shown by the pen
static float grip;
static float score;
static int combo;                           // score multiplier
static float comboTime;                     // seconds on the line since the multiplier was raised
static float offLineTime;                   // seconds since the pen left the line
static bool onLine;
static unsigned long stateStartedAt;
static unsigned long bestScore;
static bool newBestScore;
static int menuChoice;
static unsigned int frameCount;

// Generator of the line, produces one column after the other
static SegmentType segmentType;
static int segmentLength;                   // columns
static int segmentPos;                      // columns already produced
static float segmentFrom;                   // weight at the start
static float segmentTo;                     // weight at the end of a ramp
static float segmentHeight;                 // signed height of a wave
static float segmentWavelength;             // columns per wave
static float segmentEnd;                    // weight the segment ends with, the next one starts there

static float tolerance()
{
  return max(TRACER_TOLERANCE_MIN, TRACER_TOLERANCE_START - TRACER_TOLERANCE_DECREASE * runTime);
}

static int weightToY(float weight)
{
  float y = GAME_FIELD_BOTTOM - weight / TRACER_RANGE_WEIGHT * (GAME_FIELD_BOTTOM - GAME_FIELD_TOP);
  return constrain(toPixel(y), GAME_FIELD_TOP, GAME_FIELD_BOTTOM);
}

static void startSegment()
{
  float shape = min(1.0f, runTime / TRACER_SHAPE_TIME);
  float from = segmentEnd;
  segmentFrom = from;
  segmentPos = 0;

  // Never two plateaus in a row, they would just be one long plateau
  int roll = random(segmentType == SEGMENT_PLATEAU ? 25 : 0, 100);
  if (roll < 25)
  {
    segmentType = SEGMENT_PLATEAU;
    segmentLength = random(30, 81);
  }
  else if (roll < 65)
  {
    segmentType = SEGMENT_RAMP;
    float to = from;
    for (int attempt = 0; attempt < 10 && fabs(to - from) < 20; attempt++)
      to = random(TRACER_TARGET_MIN, TRACER_TARGET_MAX + 1);
    segmentTo = to;
    segmentLength = max(12, toPixel(fabs(to - from) / mix(TRACER_RAMP_SLOPE_START, TRACER_RAMP_SLOPE_END, shape)));
    segmentEnd = to;
  }
  else
  {
    segmentType = SEGMENT_WAVE;
    float height = random(15, toPixel(mix(TRACER_WAVE_HEIGHT_START, TRACER_WAVE_HEIGHT_END, shape)) + 1);
    bool roomUp = from + height <= TRACER_TARGET_MAX;
    bool roomDown = from - height >= TRACER_TARGET_MIN;
    if (roomUp && roomDown)
      segmentHeight = random(0, 2) ? height : -height;
    else if (roomUp)
      segmentHeight = height;
    else if (roomDown)
      segmentHeight = -height;
    else
      segmentHeight = TRACER_TARGET_MAX - from >= from - TRACER_TARGET_MIN ? TRACER_TARGET_MAX - from : TRACER_TARGET_MIN - from; // toward the larger room
    int waves = random(1, 4);
    float wavelength = mix(TRACER_WAVE_LENGTH_START, TRACER_WAVE_LENGTH_END, shape) * random(80, 121) / 100.0f;
    segmentLength = max(waves * 10, toPixel(wavelength * waves));
    segmentWavelength = (float)segmentLength / waves;
  }
  if (segmentType != SEGMENT_RAMP)
    segmentEnd = from;
}

// Weight of the next column of the line; ramps and waves use cosine curves so there are no kinks
static float nextTargetColumn()
{
  if (segmentPos >= segmentLength)
    startSegment();
  segmentPos++;
  switch (segmentType)
  {
  case SEGMENT_RAMP:
    return mix(segmentFrom, segmentTo, (1 - cos(PI * segmentPos / segmentLength)) / 2);
  case SEGMENT_WAVE:
    return segmentFrom + segmentHeight * (1 - cos(2 * PI * segmentPos / segmentWavelength)) / 2;
  default:
    return segmentFrom;
  }
}

void curveTracerReset()
{
  bestScore = gameLoadBestScore("tracerScore");
  runTime = 0;
  scrollRemainder = 0;
  penWeight = 0;
  grip = TRACER_GRIP_MAX;
  score = 0;
  combo = 1;
  comboTime = 0;
  offLineTime = 0;
  onLine = false;
  newBestScore = false;
  menuChoice = 0;

  // Flat start up to a bit ahead of the pen, the generated line follows
  segmentType = SEGMENT_PLATEAU;
  segmentFrom = segmentEnd = TRACER_START_WEIGHT;
  segmentLength = TRACER_PEN_X + TRACER_START_FLAT;
  segmentPos = 0;
  for (int x = 0; x < TRACER_COLUMNS; x++)
  {
    targetColumns[x] = nextTargetColumn();
    traceColumns[x] = TRACER_NO_TRACE;
  }

  gameResetPressure();
  tracerState = TRACER_READY;
  stateStartedAt = millis();
}

static float scrollSpeed()
{
  return min(TRACER_SPEED_MAX, TRACER_SPEED_START + TRACER_SPEED_INCREASE * runTime);
}

static void scroll(float dt)
{
  scrollRemainder += scrollSpeed() * dt;
  while (scrollRemainder >= 1)
  {
    scrollRemainder -= 1;
    memmove(targetColumns, targetColumns + 1, (TRACER_COLUMNS - 1) * sizeof(float));
    memmove(traceColumns, traceColumns + 1, (TRACER_COLUMNS - 1) * sizeof(float));
    targetColumns[TRACER_COLUMNS - 1] = nextTargetColumn();
    traceColumns[TRACER_COLUMNS - 1] = TRACER_NO_TRACE;
  }
}

static void updatePen(float dt)
{
  // No retare: holding a plateau perfectly steady is the point of the game
  float weight = max(0.0f, gamePressedWeight(dt, TRACER_ZERO_THRESHOLD, false));
  penWeight += (weight - penWeight) * min(1.0f, dt * TRACER_PEN_EASING);
}

static void updateRunning(float dt, unsigned long now)
{
  runTime += dt;
  scroll(dt);
  updatePen(dt);
  traceColumns[TRACER_PEN_X] = penWeight;

  // The scale reading and the pen smoothing lag behind the finger: compare the pen with the part of the line
  // that passed the pen within TRACER_LATENCY_MS and take the closest match
  float allowed = tolerance();
  int lagColumns = toPixel(scrollSpeed() * TRACER_LATENCY_MS / 1000.0f);
  float error = fabs(penWeight - targetColumns[TRACER_PEN_X]);
  for (int x = max(0, TRACER_PEN_X - lagColumns); x < TRACER_PEN_X; x++)
    error = min(error, (float)fabs(penWeight - targetColumns[x]));
  onLine = error <= allowed;
  if (onLine)
  {
    offLineTime = 0;
    score += TRACER_POINTS_PER_SECOND * combo * mix(0.5f, 1.0f, 1 - error / allowed) * dt;
    comboTime += dt;
    if (comboTime >= TRACER_COMBO_TIME && combo < TRACER_COMBO_MAX)
    {
      combo++;
      comboTime = 0;
    }
    grip = min(TRACER_GRIP_MAX, grip + TRACER_GRIP_REGEN * dt);
    return;
  }

  offLineTime += dt;
  if (offLineTime > TRACER_COMBO_FORGIVE)
  {
    combo = 1;
    comboTime = 0;
  }
  if (runTime < TRACER_GRACE_TIME)
    return;
  grip -= (TRACER_GRIP_DRAIN + TRACER_GRIP_DRAIN_PER_GRAM * (error - allowed)) * dt;
  if (grip > 0)
    return;

  grip = 0;
  unsigned long finalScore = score;
  if (finalScore > bestScore)
  {
    bestScore = finalScore;
    newBestScore = true;
    gameSaveBestScore("tracerScore", bestScore);
  }
  tracerState = TRACER_ENDING;
  stateStartedAt = now;
  Serial.printf("Curve Tracer over with %lu points after %.1fs\n", finalScore, runTime);
}

static void drawHud()
{
  // Grip bar, blinks while grip is lost
  bool losingGrip = tracerState == TRACER_RUNNING && !onLine && runTime >= TRACER_GRACE_TIME;
  if (!losingGrip || frameCount % 6 < 3)
    screen.drawFrame(0, 0, 79, 8);
  int fill = toPixel(grip / TRACER_GRIP_MAX * 75);
  if (fill > 0)
    screen.drawBox(2, 2, fill, 4);

  char buf[12];
  screen.setFont(u8g2_font_5x7_tr);
  screen.setFontPosTop();
  snprintf(buf, sizeof(buf), "x%d", combo);
  screen.drawStr(82, 1, buf);
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)score);
  screen.drawStr(128 - screen.getStrWidth(buf), 1, buf);

  screen.drawHLine(0, GAME_FIELD_TOP - 1, 128);
}

static void drawPlayfield()
{
  // The line: solid ahead of the pen, dotted where it has passed
  for (int x = 1; x < TRACER_COLUMNS; x++)
  {
    if (x > TRACER_PEN_X)
      screen.drawLine(x - 1, weightToY(targetColumns[x - 1]), x, weightToY(targetColumns[x]));
    else if (x % 2 == 0)
      screen.drawPixel(x, weightToY(targetColumns[x]));
  }

  // What the pen has drawn
  for (int x = 1; x <= TRACER_PEN_X; x++)
  {
    if (traceColumns[x - 1] != TRACER_NO_TRACE && traceColumns[x] != TRACER_NO_TRACE)
      screen.drawLine(x - 1, weightToY(traceColumns[x - 1]), x, weightToY(traceColumns[x]));
  }

  // Pen: filled on the line, hollow and blinking off the line
  int penY = constrain(weightToY(penWeight), GAME_FIELD_TOP + TRACER_PEN_RADIUS, GAME_FIELD_BOTTOM - TRACER_PEN_RADIUS);
  if (onLine || tracerState == TRACER_READY)
    screen.drawDisc(TRACER_PEN_X, penY, TRACER_PEN_RADIUS);
  else if (frameCount % 6 < 4)
    screen.drawCircle(TRACER_PEN_X, penY, TRACER_PEN_RADIUS + 1);
}

bool curveTracerFrame(float dt, int steps, bool click, unsigned long now)
{
  frameCount++;
  bool playing = tracerState == TRACER_READY || tracerState == TRACER_RUNNING || tracerState == TRACER_ENDING;

  char buf[32];
  switch (tracerState)
  {
  case TRACER_READY:
    updatePen(dt); // settles the zero point before the run
    if (click || now - stateStartedAt >= TRACER_READY_MS)
      tracerState = TRACER_RUNNING; // a click skips the countdown
    drawHud();
    if (tracerState == TRACER_READY)
      gameDrawCountdown("CURVE TRACER", "Press scale: pen up", "Follow the line", now - stateStartedAt, TRACER_READY_MS);
    else
      drawPlayfield();
    break;

  case TRACER_RUNNING:
    if (click)
    {
      tracerState = TRACER_PAUSED; // this frame is still drawn, the pause screen follows
      menuChoice = 0;
    }
    else
    {
      updateRunning(dt, now);
    }
    drawHud();
    drawPlayfield();
    break;

  case TRACER_PAUSED:
    menuChoice = gameSelectChoice(steps, menuChoice);
    if (click)
    {
      if (menuChoice == 0)
        tracerState = TRACER_RUNNING;
      else
        gameExit();
    }
    snprintf(buf, sizeof(buf), "Score: %lu", (unsigned long)score);
    gameDrawChoiceScreen("Paused", buf, "Continue", "Exit", menuChoice);
    break;

  case TRACER_ENDING:
    if (now - stateStartedAt >= TRACER_END_MS)
    {
      tracerState = TRACER_OVER;
      stateStartedAt = now;
      menuChoice = 0;
    }
    drawHud();
    drawPlayfield();
    break;

  case TRACER_OVER:
    menuChoice = gameSelectChoice(steps, menuChoice);
    if (click && now - stateStartedAt >= GAME_OVER_INPUT_DELAY_MS)
    {
      if (menuChoice == 0)
        curveTracerReset();
      else
        gameExit();
    }
    if (newBestScore)
      snprintf(buf, sizeof(buf), "%lu  New best!", (unsigned long)score);
    else
      snprintf(buf, sizeof(buf), "%lu  Best %lu", (unsigned long)score, bestScore);
    gameDrawChoiceScreen("Game Over", buf, "Retry", "Exit", menuChoice);
    break;
  }
  return playing;
}
