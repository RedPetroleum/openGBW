#include "common.hpp"

// Comet Blaster: score points for surviving and destroying comets flying in from the right.
// The knob moves the ship, pressing the scale fires the laser.

// Timing
#define GAME_READY_MS 2400             // countdown before a run starts
#define GAME_EXPLOSION_MS 1500         // ship explosion before the Game Over screen

// Playfield
#define GAME_SHIP_X 2                  // left edge of the ship
#define GAME_SHIP_WIDTH 12
#define GAME_SHIP_HEIGHT 7
#define GAME_SHIP_STEP 3               // pixels the ship moves per encoder detent
#define GAME_SHIP_EASING 18.0f         // how fast the ship follows the knob (1/s)

// Laser
#define GAME_LASER_MIN_WEIGHT 20.0f     // grams on the scale before the laser fires (ignores noise)
#define GAME_LASER_MAX_WEIGHT 200.0f   // more weight does not make the laser wider or stronger
#define GAME_LASER_MAX_HALF_WIDTH 4    // beam is 1 to 9 pixels wide
#define GAME_LASER_DAMAGE_MIN 4.0f     // damage per second at the lightest press
#define GAME_LASER_DAMAGE_MAX 40.0f    // damage per second at full power

// Energy
#define GAME_ENERGY_MAX 100.0f
#define GAME_ENERGY_START 50.0f        // a run starts with a half full tank
#define GAME_ENERGY_USE_MIN 3.0f       // energy per second at the lightest press
#define GAME_ENERGY_USE_MAX 100.0f      // energy per second at full power
#define GAME_ENERGY_REGEN 3.0f         // slow recharge per second, set to 0 to only gain energy from comets
#define GAME_ENERGY_EMPTY_BLINK 0.4f   // seconds the energy bar blinks after shooting with an empty tank
#define GAME_KILL_ENERGY_BASE 30.0f     // energy for destroying a comet ...
#define GAME_KILL_ENERGY_PER_HP 4.0f   // ... plus this per life of the comet

// Comets
#define GAME_MAX_COMETS 15
#define GAME_COMET_MIN_RADIUS 3
#define GAME_COMET_MAX_RADIUS 10        // lives of a comet are radius - 1
#define GAME_COMET_CORE_RATIO 0.4f     // the laser only damages a comet when it covers this inner part
#define GAME_COMET_MAX_TAIL 16         // longest tail in pixels, comets are removed when it left the screen
#define GAME_SPEED_START 18.0f         // comet speed in pixels per second at the start ...
#define GAME_SPEED_INCREASE 1.0f       // ... increasing by this every second survived ...
#define GAME_SPEED_MAX 110.0f          // ... up to this
#define GAME_SPAWN_START_MS 2000.0f    // time between new comets at the start ...
#define GAME_SPAWN_DECREASE_MS 15.0f   // ... shrinking by this every second survived ...
#define GAME_SPAWN_MIN_MS 800.0f       // ... down to this

// Score
#define GAME_POINTS_PER_SECOND 10.0f   // points for surviving
#define GAME_KILL_POINTS_BASE 20       // points for destroying a comet ...
#define GAME_KILL_POINTS_PER_HP 10     // ... plus this per life of the comet, so big comets give more

// Lives
#define GAME_START_LIVES 1
#define GAME_MAX_LIVES 5
#define GAME_INVULNERABLE_TIME 2.0f    // seconds the ship blinks and cannot be hit after losing a life
#define GAME_EXTRA_LIFE_MIN_INTERVAL 15.0f // seconds between extra lives flying in, chosen randomly ...
#define GAME_EXTRA_LIFE_MAX_INTERVAL 35.0f // ... in this range
#define GAME_EXTRA_LIFE_RADIUS 4       // size for laser and collection, drawn as a heart
#define GAME_EXTRA_LIFE_HP 1.0f        // the laser destroys extra lives quickly (no points), fly into them instead
#define GAME_EXTRA_LIFE_SPEED 0.8f     // extra lives fly slower than comets by this factor

#define GAME_MAX_PARTICLES 32

// Ship, the nose points right
static const uint16_t shipRows[GAME_SHIP_HEIGHT] = {
    0b111000000000,
    0b011111000000,
    0b001111011000,
    0b111111111111,
    0b001111111000,
    0b011111000000,
    0b111000000000,
};

// Extra life flying through the playfield
#define GAME_HEART_WIDTH 9
#define GAME_HEART_HEIGHT 8
static const uint16_t heartRows[GAME_HEART_HEIGHT] = {
    0b011000110,
    0b111101111,
    0b111111111,
    0b111111111,
    0b011111110,
    0b001111100,
    0b000111000,
    0b000010000,
};

// Small heart for the lives in the header
#define GAME_HUD_HEART_WIDTH 7
#define GAME_HUD_HEART_HEIGHT 6
static const uint16_t hudHeartRows[GAME_HUD_HEART_HEIGHT] = {
    0b0110110,
    0b1111111,
    0b1111111,
    0b0111110,
    0b0011100,
    0b0001000,
};

struct Comet
{
  bool active;
  float x;
  float y;
  float speed; // pixels per second
  int radius;
  bool extraLife; // heart that gives a life when the ship flies into it
  float hp;
  float maxHp;
  bool hit;    // damaged by the laser in the current frame, drawn flashing
};

struct Particle
{
  float x;
  float y;
  float vx;
  float vy;
  int lifeMs; // 0 = unused
};

enum GameState
{
  GAME_READY,
  GAME_RUNNING,
  GAME_PAUSED,
  GAME_EXPLODING,
  GAME_OVER
};

static GameState gameState = GAME_READY;
static Comet comets[GAME_MAX_COMETS];
static Particle particles[GAME_MAX_PARTICLES];
static float shipY;               // center row of the ship
static float shipTargetY;         // position selected with the knob, the ship glides there
static float energy;
static float survivalTime;        // seconds, only counts while running, drives the difficulty
static float spawnTimer;          // seconds until the next comet
static float extraLifeTimer;      // seconds until the next extra life
static float invulnerableTime;    // seconds left without collisions after losing a life
static float energyBlinkTime;     // seconds left of blinking the energy bar
static int lives;
static unsigned long killPoints;  // points from destroyed comets
static unsigned long stateStartedAt;
static unsigned long bestScore;
static bool newBestScore;
static int menuChoice;            // selected option on the pause and Game Over screens
static unsigned int frameCount;

// Laser of the current frame, calculated in the update and used for drawing
static bool laserOn;
static bool laserBlocked;         // beam ends at a comet
static int laserHalfWidth;
static int laserEndX;

static unsigned long currentScore()
{
  return (unsigned long)(survivalTime * GAME_POINTS_PER_SECOND) + killPoints;
}

// Circles are drawn pixel by pixel because comets are partly outside the screen when entering and leaving
static void drawCircleClipped(int cx, int cy, int r, bool filled)
{
  for (int dy = -r; dy <= r; dy++)
  {
    for (int dx = -r; dx <= r; dx++)
    {
      int d2 = dx * dx + dy * dy;
      if (d2 <= r * r + r && (filled || d2 > r * r - r))
        drawPixelClipped(cx + dx, cy + dy);
    }
  }
}

static void spawnParticles(float x, float y, int count, float maxSpeed)
{
  for (Particle &p : particles)
  {
    if (count == 0)
      break;
    if (p.lifeMs > 0)
      continue;
    float angle = random(0, 628) / 100.0f;
    float speed = random(30, 101) / 100.0f * maxSpeed;
    p = {x, y, cos(angle) * speed, sin(angle) * speed, (int)random(300, 900)};
    count--;
  }
}

void cometBlasterReset()
{
  bestScore = gameLoadBestScore("gameScore");
  for (Comet &c : comets)
    c.active = false;
  for (Particle &p : particles)
    p.lifeMs = 0;
  shipY = shipTargetY = (GAME_FIELD_TOP + GAME_FIELD_BOTTOM) / 2;
  energy = GAME_ENERGY_START;
  survivalTime = 0;
  spawnTimer = 0.5f; // first comet shortly after the start
  extraLifeTimer = random(GAME_EXTRA_LIFE_MIN_INTERVAL * 10, GAME_EXTRA_LIFE_MAX_INTERVAL * 10 + 1) / 10.0f;
  invulnerableTime = 0;
  energyBlinkTime = 0;
  lives = GAME_START_LIVES;
  killPoints = 0;
  gameResetPressure();
  newBestScore = false;
  laserOn = false;
  menuChoice = 0;
  gameState = GAME_READY;
  stateStartedAt = millis();
}

static void moveShip(int steps, float dt)
{
  float lowest = GAME_FIELD_TOP + GAME_SHIP_HEIGHT / 2;
  float highest = GAME_FIELD_BOTTOM - GAME_SHIP_HEIGHT / 2;
  shipTargetY = constrain(shipTargetY + steps * GAME_SHIP_STEP, lowest, highest);
  shipY += (shipTargetY - shipY) * min(1.0f, dt * GAME_SHIP_EASING);
}

static void updateLaser(float dt)
{
  laserOn = false;
  laserBlocked = false;
  float weight = gamePressedWeight(dt, GAME_LASER_MIN_WEIGHT, true);
  if (weight < GAME_LASER_MIN_WEIGHT)
    return;

  float power = constrain((weight - GAME_LASER_MIN_WEIGHT) / (GAME_LASER_MAX_WEIGHT - GAME_LASER_MIN_WEIGHT), 0.0f, 1.0f);

  // Fires only when the energy covers this frame, so an empty tank really stops the laser
  float energyUse = mix(GAME_ENERGY_USE_MIN, GAME_ENERGY_USE_MAX, power) * dt;
  if (energy < energyUse)
  {
    energyBlinkTime = GAME_ENERGY_EMPTY_BLINK; // shooting with an empty tank
    return;
  }

  energy -= energyUse;
  laserOn = true;
  laserHalfWidth = toPixel(power * GAME_LASER_MAX_HALF_WIDTH);

  // The beam stops at the first comet it touches
  int beamY = toPixel(shipY);
  int noseX = GAME_SHIP_X + GAME_SHIP_WIDTH;
  Comet *target = nullptr;
  float targetX = 128;
  for (Comet &c : comets)
  {
    if (!c.active || c.x + c.radius < noseX || c.x - c.radius >= 128)
      continue;
    float dy = fabs(c.y - beamY);
    if (dy > c.radius + laserHalfWidth)
      continue;
    float nearestDy = max(0.0f, dy - laserHalfWidth);
    float contactX = c.x - sqrt(max(0.0f, c.radius * c.radius - nearestDy * nearestDy));
    if (contactX < targetX)
    {
      targetX = contactX;
      target = &c;
    }
  }
  laserEndX = max(noseX, toPixel(targetX));
  if (target == nullptr)
    return;
  laserBlocked = true;

  // Only a hit near the center damages a comet, grazing its rim just stops the beam; extra lives break on any hit
  if (!target->extraLife && fabs(target->y - beamY) > target->radius * GAME_COMET_CORE_RATIO + laserHalfWidth)
    return;
  target->hit = true;
  target->hp -= mix(GAME_LASER_DAMAGE_MIN, GAME_LASER_DAMAGE_MAX, power) * dt;
  if (target->hp <= 0)
  {
    target->active = false;
    spawnParticles(target->x, target->y, 10, 60);
    if (!target->extraLife)
    {
      energy = min(GAME_ENERGY_MAX, energy + GAME_KILL_ENERGY_BASE + GAME_KILL_ENERGY_PER_HP * target->maxHp);
      killPoints += GAME_KILL_POINTS_BASE + GAME_KILL_POINTS_PER_HP * (unsigned long)target->maxHp;
    }
  }
}

// Returns false when there was no room for a new comet or extra life
static bool spawnComet(bool extraLife)
{
  for (Comet &c : comets)
  {
    if (c.active)
      continue;
    int radius = extraLife ? GAME_EXTRA_LIFE_RADIUS : random(GAME_COMET_MIN_RADIUS, GAME_COMET_MAX_RADIUS + 1);
    // Try a few heights so a new comet does not overlap one that just entered
    for (int attempt = 0; attempt < 5; attempt++)
    {
      int y = random(GAME_FIELD_TOP + radius, GAME_FIELD_BOTTOM - radius + 1);
      bool free = true;
      for (Comet &other : comets)
      {
        if (other.active && other.x > 128 - 2 * GAME_COMET_MAX_RADIUS && fabs(other.y - y) < other.radius + radius + 2)
          free = false;
      }
      if (!free)
        continue;
      float speed = min(GAME_SPEED_MAX, GAME_SPEED_START + GAME_SPEED_INCREASE * survivalTime) * random(80, 121) / 100.0f;
      if (extraLife)
        speed *= GAME_EXTRA_LIFE_SPEED;
      float hp = extraLife ? GAME_EXTRA_LIFE_HP : radius - 1;
      c = {true, 128.0f + radius, (float)y, speed, radius, extraLife, hp, hp, false};
      return true;
    }
    return false;
  }
  return false;
}

static void moveComets(float dt)
{
  for (Comet &c : comets)
  {
    if (!c.active)
      continue;
    c.hit = false;
    c.x -= c.speed * dt;
    if (c.x + c.radius + GAME_COMET_MAX_TAIL < 0)
      c.active = false;
  }
}

static void moveParticles(float dt)
{
  for (Particle &p : particles)
  {
    if (p.lifeMs <= 0)
      continue;
    p.x += p.vx * dt;
    p.y += p.vy * dt;
    p.lifeMs -= (int)(dt * 1000);
  }
}

// Checks every pixel of the ship against the comet, so already the outer rim of a comet is deadly
static bool touchesShip(const Comet &c)
{
  int cx = toPixel(c.x);
  int cy = toPixel(c.y);
  if (cx - c.radius > GAME_SHIP_X + GAME_SHIP_WIDTH)
    return false;
  int top = toPixel(shipY) - GAME_SHIP_HEIGHT / 2;
  for (int row = 0; row < GAME_SHIP_HEIGHT; row++)
  {
    for (int col = 0; col < GAME_SHIP_WIDTH; col++)
    {
      int dx = GAME_SHIP_X + col - cx;
      int dy = top + row - cy;
      if (bitmapPixelSet(shipRows, GAME_SHIP_WIDTH, row, col) && dx * dx + dy * dy <= c.radius * c.radius + c.radius) // same shape as drawn
        return true;
    }
  }
  return false;
}

static void destroyShip()
{
  laserOn = false;
  energyBlinkTime = 0;
  spawnParticles(GAME_SHIP_X + GAME_SHIP_WIDTH / 2, shipY, 24, 50);
  unsigned long score = currentScore();
  if (score > bestScore)
  {
    bestScore = score;
    newBestScore = true;
    gameSaveBestScore("gameScore", bestScore);
  }
  gameState = GAME_EXPLODING;
  stateStartedAt = millis();
  Serial.printf("Game over with %lu points after %.1fs\n", score, survivalTime);
}

static void handleCollisions()
{
  for (Comet &c : comets)
  {
    if (!c.active || !touchesShip(c))
      continue;
    if (c.extraLife)
    {
      c.active = false;
      lives = min(GAME_MAX_LIVES, lives + 1);
      spawnParticles(c.x, c.y, 8, 40);
    }
    else if (invulnerableTime <= 0)
    {
      if (--lives == 0)
      {
        destroyShip();
        return;
      }
      // Lose a life: the comet breaks, the ship survives and blinks for a moment
      c.active = false;
      spawnParticles(c.x, c.y, 10, 60);
      spawnParticles(GAME_SHIP_X + GAME_SHIP_WIDTH / 2, shipY, 8, 40);
      invulnerableTime = GAME_INVULNERABLE_TIME;
    }
  }
}

static void updateRunning(float dt)
{
  survivalTime += dt;

  spawnTimer -= dt;
  if (spawnTimer <= 0)
  {
    spawnComet(false);
    float interval = max(GAME_SPAWN_MIN_MS, GAME_SPAWN_START_MS - GAME_SPAWN_DECREASE_MS * survivalTime);
    spawnTimer = interval / 1000.0f * random(70, 131) / 100.0f;
  }

  extraLifeTimer -= dt;
  if (extraLifeTimer <= 0)
  {
    if (spawnComet(true))
      extraLifeTimer = random(GAME_EXTRA_LIFE_MIN_INTERVAL * 10, GAME_EXTRA_LIFE_MAX_INTERVAL * 10 + 1) / 10.0f;
    else
      extraLifeTimer = 1; // no room right now, try again shortly
  }

  invulnerableTime = max(0.0f, invulnerableTime - dt);
  energyBlinkTime = max(0.0f, energyBlinkTime - dt);
  moveComets(dt);
  updateLaser(dt);
  energy = min(GAME_ENERGY_MAX, energy + GAME_ENERGY_REGEN * dt);
  moveParticles(dt);
  handleCollisions();
}

static void drawHud()
{
  // Energy bar, blinks when shooting with an empty tank
  if (energyBlinkTime <= 0 || frameCount % 6 < 3)
    screen.drawFrame(0, 0, 79, 8);
  int fill = toPixel(energy / GAME_ENERGY_MAX * 75);
  if (fill > 0)
    screen.drawBox(2, 2, fill, 4);

  char buf[12];
  screen.setFont(u8g2_font_5x7_tr);
  screen.setFontPosTop();

  // Lives, placed so a score with up to 6 digits still fits on the right
  drawBitmap(hudHeartRows, GAME_HUD_HEART_WIDTH, GAME_HUD_HEART_HEIGHT, 82, 1, 0);
  snprintf(buf, sizeof(buf), "%d", lives);
  screen.drawStr(91, 1, buf);

  // Score
  snprintf(buf, sizeof(buf), "%lu", currentScore());
  screen.drawStr(128 - screen.getStrWidth(buf), 1, buf);

  screen.drawHLine(0, GAME_FIELD_TOP - 1, 128);
}

static void drawShip()
{
  if (invulnerableTime > 0 && frameCount % 6 < 3)
    return; // blinks after losing a life
  int top = toPixel(shipY) - GAME_SHIP_HEIGHT / 2;
  drawBitmap(shipRows, GAME_SHIP_WIDTH, GAME_SHIP_HEIGHT, GAME_SHIP_X, top);
  // Flickering engine flame
  screen.drawPixel(GAME_SHIP_X - 1, top + GAME_SHIP_HEIGHT / 2);
  if (frameCount % 2)
    screen.drawPixel(GAME_SHIP_X - 2, top + GAME_SHIP_HEIGHT / 2);
}

static void drawLaser()
{
  if (!laserOn)
    return;
  int beamY = toPixel(shipY);
  int noseX = GAME_SHIP_X + GAME_SHIP_WIDTH;
  int length = min(laserEndX, 128) - noseX;
  if (length <= 0)
    return;

  // Solid core, the outermost rows of wide beams flicker
  int solidHalf = laserHalfWidth > 1 ? laserHalfWidth - 1 : laserHalfWidth;
  screen.drawBox(noseX, beamY - solidHalf, length, 2 * solidHalf + 1);
  if (laserHalfWidth > 1)
  {
    for (int x = noseX; x < noseX + length; x++)
    {
      if ((x + frameCount) % 3 != 0)
      {
        screen.drawPixel(x, beamY - laserHalfWidth);
        screen.drawPixel(x, beamY + laserHalfWidth);
      }
    }
  }

  // Sparks where the beam hits a comet
  if (laserBlocked)
  {
    for (int i = 0; i < 3; i++)
    {
      int side = random(0, 2) ? 1 : -1;
      drawPixelClipped(laserEndX - random(0, 4), beamY + side * (laserHalfWidth + random(1, 4)));
    }
  }
}

static void drawComets()
{
  for (Comet &c : comets)
  {
    if (!c.active)
      continue;
    int cx = toPixel(c.x);
    int cy = toPixel(c.y);
    int r = c.radius;

    if (c.extraLife)
    {
      if (!c.hit || frameCount % 2) // flickers while the laser breaks it
        drawBitmap(heartRows, GAME_HEART_WIDTH, GAME_HEART_HEIGHT, cx - GAME_HEART_WIDTH / 2, cy - GAME_HEART_HEIGHT / 2);
      continue;
    }

    // Tail behind the comet, faster comets have longer tails
    int tailLength = min(GAME_COMET_MAX_TAIL, 3 + toPixel(c.speed / 12));
    for (int i = 0; i < tailLength; i++)
    {
      if ((i + frameCount) % 2)
        continue;
      drawPixelClipped(cx + r + 2 + i, cy);
      if (i < tailLength * 2 / 3)
      {
        drawPixelClipped(cx + r + 1 + i, cy - r / 2);
        drawPixelClipped(cx + r + 1 + i, cy + r / 2);
      }
    }

    if (c.hit && frameCount % 2)
    {
      drawCircleClipped(cx, cy, r, true); // flash while the laser damages it
    }
    else
    {
      drawCircleClipped(cx, cy, r, false);
      drawCircleClipped(cx, cy, max(1, toPixel(r * GAME_COMET_CORE_RATIO)), true); // the core has to be hit
    }

    // Remaining lives
    if (c.hp < c.maxHp)
    {
      int barY = cy - r - 3;
      if (barY < GAME_FIELD_TOP)
        barY = cy + r + 2;
      int width = max(1, toPixel(2 * r * c.hp / c.maxHp));
      for (int x = 0; x < width; x++)
        drawPixelClipped(cx - r + x, barY);
    }
  }
}

static void drawParticles()
{
  for (Particle &p : particles)
  {
    if (p.lifeMs > 0)
      drawPixelClipped(toPixel(p.x), toPixel(p.y));
  }
}

bool cometBlasterFrame(float dt, int steps, bool click, unsigned long now)
{
  frameCount++;
  bool playing = gameState == GAME_READY || gameState == GAME_RUNNING || gameState == GAME_EXPLODING;

  char buf[32];
  switch (gameState)
  {
  case GAME_READY:
    moveShip(steps, dt);
    gamePressedWeight(dt, GAME_LASER_MIN_WEIGHT, true); // settle the zero point before the laser is used
    if (click || now - stateStartedAt >= GAME_READY_MS)
      gameState = GAME_RUNNING; // a click skips the countdown
    drawHud();
    drawShip();
    if (gameState == GAME_READY)
      gameDrawCountdown("COMET BLASTER", "Knob: move ship", "Press scale: laser", now - stateStartedAt, GAME_READY_MS);
    break;

  case GAME_RUNNING:
    if (click)
    {
      gameState = GAME_PAUSED; // this frame is still drawn, the pause screen follows
      menuChoice = 0;
    }
    else
    {
      moveShip(steps, dt);
      updateRunning(dt);
    }
    drawHud();
    drawComets();
    drawLaser();
    if (gameState != GAME_EXPLODING)
      drawShip();
    drawParticles();
    break;

  case GAME_PAUSED:
    menuChoice = gameSelectChoice(steps, menuChoice);
    if (click)
    {
      if (menuChoice == 0)
        gameState = GAME_RUNNING;
      else
        gameExit();
    }
    snprintf(buf, sizeof(buf), "Score: %lu", currentScore());
    gameDrawChoiceScreen("Paused", buf, "Continue", "Exit", menuChoice);
    break;

  case GAME_EXPLODING:
    moveComets(dt);
    moveParticles(dt);
    if (now - stateStartedAt >= GAME_EXPLOSION_MS)
    {
      gameState = GAME_OVER;
      stateStartedAt = now;
      menuChoice = 0;
    }
    drawHud();
    drawComets();
    drawParticles();
    break;

  case GAME_OVER:
    menuChoice = gameSelectChoice(steps, menuChoice);
    if (click && now - stateStartedAt >= GAME_OVER_INPUT_DELAY_MS)
    {
      if (menuChoice == 0)
        cometBlasterReset();
      else
        gameExit();
    }
    if (newBestScore)
      snprintf(buf, sizeof(buf), "%lu  New best!", currentScore());
    else
      snprintf(buf, sizeof(buf), "%lu  Best %lu", currentScore(), bestScore);
    gameDrawChoiceScreen("Game Over", buf, "Retry", "Exit", menuChoice);
    break;
  }
  return playing;
}
