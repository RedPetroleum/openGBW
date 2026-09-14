#include "common.hpp"
#include "rotary.hpp"
#include "doom_data.hpp"

// Doom: a small first person shooter in the style of Wolfenstein 3D with sprites from Doom.
// Pressing the scale walks forward, pulling it up walks backward, the knob turns, a click fires and
// holding the knob opens the pause menu. Dead enemies drop ammo. Find the exit, locked doors need a key.
//
// Based on doom-nano by daveruiz (https://github.com/daveruiz/doom-nano, commit 2346404): raycaster, enemies,
// sprites, font and level. The doors are based on Doom-Nano-ESP32 by ZelTroN-2k3
// (https://github.com/ZelTroN-2k3/Doom-Nano-ESP32, commit 768fe21).
// Reworked for this firmware: one frame per call, times in seconds instead of frames, knob and scale as controller,
// ammo dropped by enemies, enemies and items do not come back, no shooting through walls, no sound.

// Timing
#define DOOM_INTRO_MS 1500              // logo before the level starts
#define DOOM_FADE_TIME 0.5f             // fade in at the start of the level
#define DOOM_DEATH_TIME 1.8f            // the view sinks this long before the Game Over screen
#define DOOM_EXIT_TIME 0.6f             // fade out at the exit before the Level Clear screen
#define DOOM_MESSAGE_TIME 2.0f          // seconds a message stays at the top of the view
#define DOOM_FLASH_TIME 0.1f            // the view flashes this long when the player is hurt or picks something up

// Knob
#define DOOM_LONG_PRESS_MS 800          // holding the knob this long opens the pause menu
#define DOOM_BUTTON_DEBOUNCE_MS 40      // the knob has to be released this long before the next press counts
#define DOOM_TURN_PER_STEP 0.2f         // radians the view turns per encoder detent (about 11 degrees)
#define DOOM_TURN_EASING 14.0f          // how fast the view follows the knob (1/s)

// Scale
#define DOOM_ZERO_THRESHOLD 6.0f        // within this many grams the zero point follows scale drift
#define DOOM_WALK_MIN_WEIGHT 15.0f      // grams pressed or pulled before the player walks (ignores noise) ...
#define DOOM_WALK_FULL_WEIGHT 60.0f     // ... up to full speed at this weight, forward and backward

// Player
#define DOOM_WALK_SPEED 2.5f            // cells per second at full pressure, forward and backward
#define DOOM_WALK_SLOWEST 0.25f         // speed at the lightest press, relative to full speed
#define DOOM_WALK_EASING 8.0f           // how fast the speed follows the pressure (1/s)
#define DOOM_PLAYER_RADIUS 0.2f         // distance kept to walls
#define DOOM_HEALTH_MAX 100
#define DOOM_MEDIKIT_HEALTH 50
#define DOOM_JOG_SPEED 0.005f           // view and gun bob while walking (radians per ms)
#define DOOM_JOG_HEIGHT 6.0f            // pixels the view bobs at full speed

// Gun
#define DOOM_AMMO_START 30              // bullets at the start of the level
#define DOOM_AMMO_MAX 99
#define DOOM_AMMO_DROP 10               // bullets in the clip a dead enemy drops
#define DOOM_SHOT_TIME 0.27f            // recoil; a click during it fires when it is over
#define DOOM_MUZZLE_TIME 0.13f          // the muzzle flash is shown at the beginning of the recoil
#define DOOM_RAISE_TIME 0.3f            // the gun comes up at the start of the level
#define DOOM_GUN_DAMAGE 30.0f           // damage of a hit in the center of a close enemy ...
#define DOOM_GUN_FULL_RANGE 2.0f        // ... up to this many cells away ...
#define DOOM_GUN_RANGE 8.0f             // ... falling to half at this distance
#define DOOM_HIT_RADIUS 0.35f           // a shot this many cells beside the center of an enemy still hits ...
#define DOOM_AIM_ANGLE 0.1f             // ... or this many radians beside it, so far enemies can be hit between two detents

// Enemies
#define DOOM_ENEMY_HEALTH 100.0f
#define DOOM_ENEMY_SPEED 0.3f           // cells per second on each axis
#define DOOM_ENEMY_VIEW 4.0f            // enemies closer than this many cells chase the player and throw fireballs
#define DOOM_ENEMY_MELEE_DISTANCE 0.3f  // enemies closer than this attack with their claws
#define DOOM_ENEMY_COLLIDER 0.35f       // the player cannot walk closer to an enemy
#define DOOM_ENEMY_MELEE_DAMAGE 8
#define DOOM_ENEMY_FIRST_FIRE 1.33f     // seconds from noticing the player to the first fireball
#define DOOM_ENEMY_FIRE_POSE 0.4f       // seconds the throwing sprite is shown
#define DOOM_ENEMY_FIRE_INTERVAL 2.67f  // seconds until the next fireball, also after being hit
#define DOOM_ENEMY_HIT_TIME 0.27f       // seconds an enemy is stunned by a hit
#define DOOM_ENEMY_DYING_TIME 0.4f      // seconds the dying sprite is shown before the corpse
#define DOOM_ENEMY_MELEE_WINDUP 0.67f   // seconds before the first claw attack
#define DOOM_ENEMY_MELEE_INTERVAL 0.93f // seconds between claw attacks
#define DOOM_ENEMY_MELEE_POSE 0.27f     // seconds the attack sprite is shown after a claw attack
#define DOOM_FIREBALL_SPEED 3.0f        // cells per second
#define DOOM_FIREBALL_RADIUS 0.25f      // a fireball this close hits the player
#define DOOM_FIREBALL_DAMAGE 20
#define DOOM_ITEM_RADIUS 0.3f           // items this close are picked up
#define DOOM_ENTITY_DISTANCE 10.0f      // entities farther away are removed, living enemies come back when seen again
#define DOOM_MAX_ENTITIES 20

// Doors
#define DOOM_MAX_DOORS 32
#define DOOM_DOOR_SPEED 1.5f            // opening per second, 1 is fully open
#define DOOM_DOOR_OPEN_TIME 3.0f        // seconds a door stays open when nobody is in it
#define DOOM_DOOR_PASSABLE 0.7f         // a door is passable when it is open this far

// View
#define DOOM_SCREEN_WIDTH 128
#define DOOM_HALF_WIDTH 64
#define DOOM_VIEW_HEIGHT 56             // 3D view, the HUD is below
#define DOOM_HUD_Y 58
#define DOOM_MAX_DEPTH 12               // cells a ray travels
#define DOOM_MAX_SPRITE_DEPTH 8.0f      // sprites farther away are not drawn
#define DOOM_MIN_WALL_DISTANCE 0.2f     // closer walls are drawn at this distance
#define DOOM_FAR 100.0f                 // depth of columns without a wall
#define DOOM_GUN_TARGET_POS 18          // pixels of the gun above the bottom of the view
#define DOOM_GUN_RECOIL 4               // the gun jumps up this far when firing

// Blocks of the level, one per cell
enum DoomBlock
{
  BLOCK_FLOOR = 0x0,
  BLOCK_PLAYER = 0x1,
  BLOCK_ENEMY = 0x2,
  BLOCK_DOOR = 0x4,
  BLOCK_LOCKED_DOOR = 0x5,
  BLOCK_EXIT = 0x7,
  BLOCK_MEDIKIT = 0x8,
  BLOCK_KEY = 0x9,
  BLOCK_WALL = 0xF
};

// Clip of bullets dropped by dead enemies, drawn like the items (one bit per pixel, highest bit leftmost)
#define AMMO_WIDTH 16
#define AMMO_HEIGHT 16
static const uint8_t ammoBits[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x11, 0x10, // ...#...#...#....
    0x3b, 0xb8, // ..###.###.###...
    0x2a, 0xa8, // ..#.#.#.#.#.#...
    0x7f, 0xfc, // .#############..
    0x40, 0x04, // .#...........#..
    0x5f, 0xf4, // .#.#########.#..
    0x55, 0x54, // .#.#.#.#.#.#.#..
    0x5f, 0xf4, // .#.#########.#..
    0x40, 0x04, // .#...........#..
    0x7f, 0xfc, // .#############..
};
static const uint8_t ammoMask[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x11, 0x10, 0x3b, 0xb8, 0x3b, 0xb8, 0x7f, 0xfc, 0x7f, 0xfc, 0x7f, 0xfc, 0x7f, 0xfc, 0x7f, 0xfc, 0x7f, 0xfc, 0x7f, 0xfc,
};

enum DoomState
{
  DOOM_INTRO,
  DOOM_PLAYING,
  DOOM_PAUSED,
  DOOM_DYING,
  DOOM_OVER,
  DOOM_EXITING,
  DOOM_CLEAR
};

enum EntityType
{
  ENTITY_ENEMY,
  ENTITY_MEDIKIT,
  ENTITY_KEY,
  ENTITY_AMMO,
  ENTITY_FIREBALL
};

enum EnemyState
{
  ENEMY_STAND,
  ENEMY_ALERT,
  ENEMY_FIRING,
  ENEMY_MELEE,
  ENEMY_HIT,
  ENEMY_DEAD
};

enum DoorState
{
  DOOR_CLOSED,
  DOOR_OPENING,
  DOOR_OPEN,
  DOOR_CLOSING
};

struct DoomVec
{
  float x;
  float y;
};

struct DoomEntity
{
  EntityType type;
  int cell;         // cell it was spawned from (y * width + x), -1 for fireballs
  DoomVec pos;
  EnemyState state;
  float health;
  float angle;      // direction of a fireball
  float distance;   // to the player in cells
  float timer;      // seconds left in the current enemy state
};

struct DoomDoor
{
  int x;
  int y;
  bool locked;
  DoorState state;
  float open;       // 0 closed, 1 open
  float timer;      // seconds until an open door closes
};

static DoomState doomState = DOOM_INTRO;
static unsigned long stateStartedAt;
static float stateTime;               // seconds in the dying and exiting states
static int menuChoice;
static unsigned int frameCount;

// Player
static DoomVec playerPos;
static DoomVec playerDir;             // length 1
static DoomVec playerPlane;           // camera plane, perpendicular to the direction
static int health;
static int keys;
static float speed;                   // cells per second
static float turnRemaining;           // radians the view still has to turn
static float jogging;                 // 0 standing, 1 full speed
static float viewSink;                // pixels the view has sunk when the player died

// Gun
static int ammo;
static float shotTimer;               // recoil left
static bool shotQueued;               // clicked during the recoil
static float raiseTimer;              // time left while the gun comes up

// Level
static DoomEntity entities[DOOM_MAX_ENTITIES];
static int entityCount;
static DoomDoor doors[DOOM_MAX_DOORS];
static int doorCount;
static uint8_t clearedCells[(DOOM_LEVEL_WIDTH * DOOM_LEVEL_HEIGHT + 7) / 8]; // killed enemies and picked up items
static uint8_t ammoDrops[DOOM_LEVEL_WIDTH * DOOM_LEVEL_HEIGHT]; // clips lying in each cell
static int kills;
static int enemyTotal;
static float runTime;                 // seconds played in this level
static unsigned long bestTime;        // seconds, 0 without a finished level
static bool newBestTime;

// Effects
static float zbuffer[DOOM_SCREEN_WIDTH]; // wall distance of each screen column
static float fadeTimer;
static float flashTimer;
static const char *message = "";
static float messageTimer;

// Knob button, read directly so a shot fires on the press and holding it can be told apart.
// The menus use the click from the rotary task instead, which comes on the release: a press that left the game
// would otherwise be released in the Games submenu and start the game again.
static bool buttonDown;
static unsigned long buttonDownAt;
static unsigned long buttonReleasedAt;
static bool longPressHandled;
static bool ignoreClick;              // the release of the long press that opened the pause menu is not a choice

// ---------------------------------------------------------------------------------------------------------------
// Level

static int getBlock(int x, int y)
{
  if (x < 0 || x >= DOOM_LEVEL_WIDTH || y < 0 || y >= DOOM_LEVEL_HEIGHT)
    return BLOCK_FLOOR;
  // Four bits per cell, the first row of the data is the highest y
  int index = (DOOM_LEVEL_HEIGHT - 1 - y) * DOOM_LEVEL_WIDTH + x;
  return (level1[index / 2] >> (x % 2 ? 0 : 4)) & 0x0F;
}

static bool isCleared(int cell)
{
  return clearedCells[cell / 8] & (1 << (cell % 8));
}

static void setCleared(int cell)
{
  clearedCells[cell / 8] |= 1 << (cell % 8);
}

static DoomDoor *doorAt(int x, int y)
{
  for (int i = 0; i < doorCount; i++)
  {
    if (doors[i].x == x && doors[i].y == y)
      return &doors[i];
  }
  return nullptr;
}

static float distanceTo(const DoomVec &a, const DoomVec &b)
{
  return sqrt(sq(a.x - b.x) + sq(a.y - b.y));
}

static void showMessage(const char *text)
{
  message = text;
  messageTimer = DOOM_MESSAGE_TIME;
}

static void startLevel()
{
  entityCount = 0;
  doorCount = 0;
  enemyTotal = 0;
  kills = 0;
  memset(clearedCells, 0, sizeof(clearedCells));
  memset(ammoDrops, 0, sizeof(ammoDrops));
  playerPos = {1.5f, 1.5f};
  for (int y = 0; y < DOOM_LEVEL_HEIGHT; y++)
  {
    for (int x = 0; x < DOOM_LEVEL_WIDTH; x++)
    {
      int block = getBlock(x, y);
      if (block == BLOCK_PLAYER)
        playerPos = {x + 0.5f, y + 0.5f};
      else if (block == BLOCK_ENEMY)
        enemyTotal++;
      else if ((block == BLOCK_DOOR || block == BLOCK_LOCKED_DOOR) && doorCount < DOOM_MAX_DOORS)
        doors[doorCount++] = {x, y, block == BLOCK_LOCKED_DOOR, DOOR_CLOSED, 0, 0};
    }
  }
  playerDir = {1, 0};
  playerPlane = {0, -0.66f};

  health = DOOM_HEALTH_MAX;
  keys = 0;
  speed = 0;
  turnRemaining = 0;
  jogging = 0;
  viewSink = 0;
  ammo = DOOM_AMMO_START;
  shotTimer = 0;
  shotQueued = false;
  raiseTimer = DOOM_RAISE_TIME;
  runTime = 0;
  fadeTimer = DOOM_FADE_TIME;
  flashTimer = 0;
  messageTimer = 0;
  menuChoice = 0;
  newBestTime = false;
  for (int x = 0; x < DOOM_SCREEN_WIDTH; x++)
    zbuffer[x] = DOOM_FAR;

  gameResetPressure();
  doomState = DOOM_PLAYING;
}

void doomReset()
{
  bestTime = gameLoadBestScore("doomTime");
  startLevel();
  doomState = DOOM_INTRO;
  stateStartedAt = millis();

  // The click that started the game must not fire, a knob still held must not open the pause menu
  buttonDown = rotaryEncoder.isEncoderButtonDown();
  buttonReleasedAt = millis();
  longPressHandled = true;
}

// ---------------------------------------------------------------------------------------------------------------
// Input

static void readButton(unsigned long now, bool &pressed, bool &longPress)
{
  bool down = rotaryEncoder.isEncoderButtonDown();
  pressed = false;
  longPress = false;
  if (down && !buttonDown)
  {
    pressed = now - buttonReleasedAt >= DOOM_BUTTON_DEBOUNCE_MS; // a bouncing contact is not a new press
    buttonDownAt = now;
    longPressHandled = false;
  }
  else if (!down && buttonDown)
  {
    buttonReleasedAt = now;
  }
  if (down && !longPressHandled && now - buttonDownAt >= DOOM_LONG_PRESS_MS)
  {
    longPress = true;
    longPressHandled = true;
  }
  buttonDown = down;
}

// ---------------------------------------------------------------------------------------------------------------
// Movement and doors

static void openDoor(DoomDoor &door)
{
  if (door.state == DOOR_OPENING || door.state == DOOR_OPEN)
    return;
  if (door.locked)
  {
    if (keys == 0)
    {
      showMessage("YOU NEED A KEY");
      return;
    }
    keys--;
    door.locked = false; // stays unlocked, the key is used only once
  }
  door.state = DOOR_OPENING;
}

static bool doorPassable(const DoomDoor &door)
{
  return door.open >= DOOM_DOOR_PASSABLE && door.state != DOOR_CLOSING;
}

// Walls and closed doors block enemies and fireballs
static bool solidForMover(int x, int y)
{
  int block = getBlock(x, y);
  if (block == BLOCK_WALL)
    return true;
  if (block == BLOCK_DOOR || block == BLOCK_LOCKED_DOOR)
  {
    DoomDoor *door = doorAt(x, y);
    return door && !doorPassable(*door);
  }
  return false;
}

// Like solidForMover, but the player opens the doors he walks into
static bool solidForPlayer(float fx, float fy)
{
  int x = (int)floor(fx);
  int y = (int)floor(fy);
  int block = getBlock(x, y);
  if (block == BLOCK_DOOR || block == BLOCK_LOCKED_DOOR)
  {
    DoomDoor *door = doorAt(x, y);
    if (door && !doorPassable(*door))
    {
      openDoor(*door);
      return true;
    }
    return false;
  }
  return block == BLOCK_WALL;
}

static bool blockedByEnemy(const DoomVec &from, const DoomVec &to)
{
  for (int i = 0; i < entityCount; i++)
  {
    const DoomEntity &e = entities[i];
    if (e.type != ENTITY_ENEMY || e.state == ENEMY_DEAD)
      continue;
    float distance = distanceTo(to, e.pos);
    if (distance < DOOM_ENEMY_COLLIDER && distance < distanceTo(from, e.pos))
      return true; // walking away from an enemy is always allowed
  }
  return false;
}

// Moves the player one axis after the other, so he slides along walls
static void movePlayer(float dx, float dy)
{
  const float r = DOOM_PLAYER_RADIUS;
  if (dx != 0)
  {
    DoomVec to = {playerPos.x + dx, playerPos.y};
    float edge = to.x + (dx > 0 ? r : -r);
    if (!solidForPlayer(edge, to.y - r) && !solidForPlayer(edge, to.y + r) && !blockedByEnemy(playerPos, to))
      playerPos = to;
  }
  if (dy != 0)
  {
    DoomVec to = {playerPos.x, playerPos.y + dy};
    float edge = to.y + (dy > 0 ? r : -r);
    if (!solidForPlayer(to.x - r, edge) && !solidForPlayer(to.x + r, edge) && !blockedByEnemy(playerPos, to))
      playerPos = to;
  }
}

// Moves an enemy or a fireball, returns false when a wall or a closed door is in the way
static bool moveMover(DoomVec &pos, float dx, float dy)
{
  bool moved = true;
  if (!solidForMover((int)floor(pos.x + dx), (int)floor(pos.y)))
    pos.x += dx;
  else
    moved = false;
  if (!solidForMover((int)floor(pos.x), (int)floor(pos.y + dy)))
    pos.y += dy;
  else
    moved = false;
  return moved;
}

static bool doorOccupied(const DoomDoor &door)
{
  const float reach = 0.5f + DOOM_PLAYER_RADIUS;
  if (fabs(playerPos.x - (door.x + 0.5f)) < reach && fabs(playerPos.y - (door.y + 0.5f)) < reach)
    return true;
  for (int i = 0; i < entityCount; i++)
  {
    if ((int)floor(entities[i].pos.x) == door.x && (int)floor(entities[i].pos.y) == door.y)
      return true;
  }
  return false;
}

static void updateDoors(float dt)
{
  for (int i = 0; i < doorCount; i++)
  {
    DoomDoor &door = doors[i];
    switch (door.state)
    {
    case DOOR_OPENING:
      door.open += DOOM_DOOR_SPEED * dt;
      if (door.open >= 1)
      {
        door.open = 1;
        door.state = DOOR_OPEN;
        door.timer = DOOM_DOOR_OPEN_TIME;
      }
      break;
    case DOOR_OPEN:
      if (doorOccupied(door))
        door.timer = DOOM_DOOR_OPEN_TIME;
      door.timer -= dt;
      if (door.timer <= 0)
        door.state = DOOR_CLOSING;
      break;
    case DOOR_CLOSING:
      if (doorOccupied(door))
      {
        door.state = DOOR_OPENING; // never closes on someone
        break;
      }
      door.open -= DOOM_DOOR_SPEED * dt;
      if (door.open <= 0)
      {
        door.open = 0;
        door.state = DOOR_CLOSED;
      }
      break;
    default:
      break;
    }
  }
}

static void rotateView(float angle)
{
  float c = cos(angle);
  float s = sin(angle);
  playerDir = {playerDir.x * c - playerDir.y * s, playerDir.x * s + playerDir.y * c};
  playerPlane = {playerPlane.x * c - playerPlane.y * s, playerPlane.x * s + playerPlane.y * c};
}

// ---------------------------------------------------------------------------------------------------------------
// Entities

static void removeEntity(int index)
{
  entityCount--;
  for (int i = index; i < entityCount; i++)
    entities[i] = entities[i + 1];
}

static bool isSpawned(int cell, EntityType type)
{
  for (int i = 0; i < entityCount; i++)
  {
    if (entities[i].cell == cell && entities[i].type == type)
      return true;
  }
  return false;
}

// Enemies and items are created when a ray passes their cell
static void spawnEntity(int block, int x, int y)
{
  int cell = y * DOOM_LEVEL_WIDTH + x;
  EntityType type = block == BLOCK_ENEMY ? ENTITY_ENEMY : block == BLOCK_KEY ? ENTITY_KEY : ENTITY_MEDIKIT;
  if (entityCount >= DOOM_MAX_ENTITIES || isCleared(cell) || isSpawned(cell, type))
    return;
  DoomVec pos = {x + 0.5f, y + 0.5f};
  if (distanceTo(pos, playerPos) >= DOOM_ENTITY_DISTANCE)
    return;
  entities[entityCount++] = {type, cell, pos, ENEMY_STAND, DOOM_ENEMY_HEALTH, 0, distanceTo(pos, playerPos), 0};
}

// Ammo dropped by enemies, created when it is dropped and again when a ray passes its cell
static void spawnAmmo(int cell)
{
  if (entityCount >= DOOM_MAX_ENTITIES || isSpawned(cell, ENTITY_AMMO))
    return;
  DoomVec pos = {cell % DOOM_LEVEL_WIDTH + 0.5f, cell / DOOM_LEVEL_WIDTH + 0.5f};
  if (distanceTo(pos, playerPos) >= DOOM_ENTITY_DISTANCE)
    return;
  entities[entityCount++] = {ENTITY_AMMO, cell, pos, ENEMY_STAND, 0, 0, distanceTo(pos, playerPos), 0};
}

static void spawnFireball(const DoomVec &from)
{
  if (entityCount >= DOOM_MAX_ENTITIES)
    return;
  float angle = atan2(playerPos.y - from.y, playerPos.x - from.x);
  entities[entityCount++] = {ENTITY_FIREBALL, -1, from, ENEMY_STAND, 0, angle, distanceTo(from, playerPos), 0};
}

static void hurtPlayer(int damage)
{
  health = max(0, health - damage);
  flashTimer = DOOM_FLASH_TIME;
}

static void updateEnemy(DoomEntity &e, float dt)
{
  if (e.health <= 0)
  {
    if (e.state != ENEMY_DEAD)
    {
      e.state = ENEMY_DEAD;
      e.timer = DOOM_ENEMY_DYING_TIME;
      setCleared(e.cell); // the corpse stays, but the enemy does not come back
      kills++;
      int dropCell = (int)floor(e.pos.y) * DOOM_LEVEL_WIDTH + (int)floor(e.pos.x);
      ammoDrops[dropCell] = min(255, ammoDrops[dropCell] + 1);
      spawnAmmo(dropCell);
    }
    return;
  }
  if (health == 0)
  {
    e.state = ENEMY_STAND;
    return;
  }
  if (e.state == ENEMY_HIT || e.state == ENEMY_FIRING)
  {
    if (e.timer <= 0)
    {
      e.state = ENEMY_ALERT;
      e.timer = DOOM_ENEMY_FIRE_INTERVAL;
    }
    return;
  }

  if (e.distance > DOOM_ENEMY_MELEE_DISTANCE && e.distance < DOOM_ENEMY_VIEW)
  {
    if (e.state != ENEMY_ALERT)
    {
      e.state = ENEMY_ALERT;
      e.timer = DOOM_ENEMY_FIRST_FIRE;
    }
    else if (e.timer <= 0)
    {
      spawnFireball(e.pos);
      e.state = ENEMY_FIRING;
      e.timer = DOOM_ENEMY_FIRE_POSE;
    }
    else
    {
      // Walks straight towards the player on both axes
      float step = DOOM_ENEMY_SPEED * dt;
      float dx = playerPos.x > e.pos.x ? step : playerPos.x < e.pos.x ? -step : 0;
      float dy = playerPos.y > e.pos.y ? step : playerPos.y < e.pos.y ? -step : 0;
      moveMover(e.pos, dx, dy);
    }
  }
  else if (e.distance <= DOOM_ENEMY_MELEE_DISTANCE)
  {
    if (e.state != ENEMY_MELEE)
    {
      e.state = ENEMY_MELEE;
      e.timer = DOOM_ENEMY_MELEE_WINDUP;
    }
    else if (e.timer <= 0)
    {
      hurtPlayer(DOOM_ENEMY_MELEE_DAMAGE);
      e.timer = DOOM_ENEMY_MELEE_INTERVAL;
    }
  }
  else
  {
    e.state = ENEMY_STAND;
  }
}

static void updateEntities(float dt)
{
  int i = 0;
  while (i < entityCount)
  {
    DoomEntity &e = entities[i];
    e.distance = distanceTo(e.pos, playerPos);
    e.timer = max(0.0f, e.timer - dt);

    if (e.distance > DOOM_ENTITY_DISTANCE)
    {
      removeEntity(i);
      continue;
    }

    switch (e.type)
    {
    case ENTITY_ENEMY:
      updateEnemy(e, dt);
      break;

    case ENTITY_FIREBALL:
      if (e.distance < DOOM_FIREBALL_RADIUS && health > 0)
      {
        hurtPlayer(DOOM_FIREBALL_DAMAGE);
        removeEntity(i);
        continue;
      }
      if (!moveMover(e.pos, cos(e.angle) * DOOM_FIREBALL_SPEED * dt, sin(e.angle) * DOOM_FIREBALL_SPEED * dt))
      {
        removeEntity(i);
        continue;
      }
      break;

    case ENTITY_MEDIKIT:
      if (e.distance < DOOM_ITEM_RADIUS && health > 0 && health < DOOM_HEALTH_MAX)
      {
        health = min(DOOM_HEALTH_MAX, health + DOOM_MEDIKIT_HEALTH);
        flashTimer = DOOM_FLASH_TIME;
        setCleared(e.cell);
        removeEntity(i);
        continue;
      }
      break;

    case ENTITY_AMMO:
      if (e.distance < DOOM_ITEM_RADIUS && health > 0 && ammo < DOOM_AMMO_MAX)
      {
        ammo = min(DOOM_AMMO_MAX, ammo + DOOM_AMMO_DROP * ammoDrops[e.cell]);
        ammoDrops[e.cell] = 0;
        flashTimer = DOOM_FLASH_TIME;
        removeEntity(i);
        continue;
      }
      break;

    case ENTITY_KEY:
      if (e.distance < DOOM_ITEM_RADIUS && health > 0)
      {
        keys++;
        flashTimer = DOOM_FLASH_TIME;
        showMessage("KEY");
        setCleared(e.cell);
        removeEntity(i);
        continue;
      }
      break;
    }
    i++;
  }
}

// Position relative to the camera: x to the right, y is the depth
static DoomVec toView(const DoomVec &pos)
{
  float sx = pos.x - playerPos.x;
  float sy = pos.y - playerPos.y;
  float invDet = 1.0f / (playerPlane.x * playerDir.y - playerDir.x * playerPlane.y);
  return {invDet * (playerDir.y * sx - playerDir.x * sy), invDet * (-playerPlane.y * sx + playerPlane.x * sy)};
}

// Hits the closest living enemy in the line of fire, unless a wall is in front of it
static void fire()
{
  ammo--;
  shotTimer = DOOM_SHOT_TIME;

  DoomEntity *target = nullptr;
  float targetOffset = 0; // 0 in the center, 1 at the edge
  float targetDepth = DOOM_FAR;
  for (int i = 0; i < entityCount; i++)
  {
    DoomEntity &e = entities[i];
    if (e.type != ENTITY_ENEMY || e.state == ENEMY_DEAD)
      continue;
    DoomVec view = toView(e.pos);
    float allowed = max(DOOM_HIT_RADIUS, view.y * DOOM_AIM_ANGLE);
    if (view.y <= 0.1f || fabs(view.x) > allowed || view.y >= targetDepth)
      continue;
    int column = constrain(toPixel(DOOM_HALF_WIDTH * (1 + view.x / view.y)), 0, DOOM_SCREEN_WIDTH - 1);
    if (zbuffer[column] < view.y)
      continue;
    target = &e;
    targetOffset = fabs(view.x) / allowed;
    targetDepth = view.y;
  }
  if (!target)
    return;

  float aim = 1 - 0.5f * targetOffset; // half damage at the edge
  float range = 1 - 0.5f * constrain((targetDepth - DOOM_GUN_FULL_RANGE) / (DOOM_GUN_RANGE - DOOM_GUN_FULL_RANGE), 0.0f, 1.0f);
  target->health -= DOOM_GUN_DAMAGE * aim * range;
  if (target->health > 0)
  {
    target->state = ENEMY_HIT;
    target->timer = DOOM_ENEMY_HIT_TIME;
  }
}

// ---------------------------------------------------------------------------------------------------------------
// Drawing

static bool bitmapBit(const uint8_t *bits, int byteWidth, int x, int y)
{
  return bits[y * byteWidth + x / 8] & (0x80 >> (x % 8));
}

// Dither patterns from black (0) to white (GRADIENT_COUNT - 1)
static bool gradientPixel(int x, int y, int intensity)
{
  if (intensity <= 0)
    return false;
  if (intensity >= GRADIENT_COUNT - 1)
    return true;
  int index = intensity * GRADIENT_WIDTH * GRADIENT_HEIGHT + (y * GRADIENT_WIDTH) % (GRADIENT_WIDTH * GRADIENT_HEIGHT) + (x / GRADIENT_HEIGHT) % GRADIENT_WIDTH;
  return gradient[index] & (0x80 >> (x % 8));
}

static void drawChar(int x, int y, char ch)
{
  const char *map = FONT_CHAR_MAP;
  const char *found = strchr(map, ch);
  int c = found && ch ? found - map : 0;
  for (int line = 0; line < FONT_CHAR_HEIGHT; line++)
  {
    uint8_t b = bmp_font[line * FONT_BYTES_PER_ROW + c / 2]; // two characters per byte
    for (int n = 0; n < FONT_CHAR_WIDTH; n++)
    {
      if (b & (0x80 >> ((c % 2 ? 4 : 0) + n)))
        screen.drawPixel(x + n, y + line);
    }
  }
}

static int textWidth(const char *text)
{
  return max(0, (int)strlen(text) * (FONT_CHAR_WIDTH + 1) - 1);
}

static void drawText(int x, int y, const char *text)
{
  for (; *text && x < DOOM_SCREEN_WIDTH; text++, x += FONT_CHAR_WIDTH + 1)
    drawChar(x, y, *text);
}

static void renderMap(float viewBob)
{
  for (int x = 0; x < DOOM_SCREEN_WIDTH; x++)
  {
    float cameraX = 2.0f * x / DOOM_SCREEN_WIDTH - 1;
    float rayX = playerDir.x + playerPlane.x * cameraX;
    float rayY = playerDir.y + playerPlane.y * cameraX;
    int mapX = (int)floor(playerPos.x);
    int mapY = (int)floor(playerPos.y);
    float deltaX = rayX == 0 ? 1e30f : fabs(1 / rayX);
    float deltaY = rayY == 0 ? 1e30f : fabs(1 / rayY);
    int stepX = rayX < 0 ? -1 : 1;
    int stepY = rayY < 0 ? -1 : 1;
    float sideX = (rayX < 0 ? playerPos.x - mapX : mapX + 1.0f - playerPos.x) * deltaX;
    float sideY = (rayY < 0 ? playerPos.y - mapY : mapY + 1.0f - playerPos.y) * deltaY;

    bool hit = false;
    int side = 0;
    float distance = 0;
    float wallX = 0;        // where the ray hits the wall, 0 to 1
    DoomDoor *door = nullptr;
    for (int depth = 0; !hit && depth < DOOM_MAX_DEPTH; depth++)
    {
      if (sideX < sideY)
      {
        sideX += deltaX;
        mapX += stepX;
        side = 0;
      }
      else
      {
        sideY += deltaY;
        mapY += stepY;
        side = 1;
      }

      int block = getBlock(mapX, mapY);
      if (block == BLOCK_WALL || block == BLOCK_DOOR || block == BLOCK_LOCKED_DOOR)
      {
        distance = side == 0 ? (mapX - playerPos.x + (1 - stepX) / 2.0f) / rayX : (mapY - playerPos.y + (1 - stepY) / 2.0f) / rayY;
        wallX = side == 0 ? playerPos.y + distance * rayY : playerPos.x + distance * rayX;
        wallX -= floor(wallX);
        if (block == BLOCK_WALL)
        {
          hit = true;
        }
        else
        {
          // A door slides to the side, the ray passes through the open part
          door = doorAt(mapX, mapY);
          hit = !door || wallX >= door->open;
          if (!hit)
            door = nullptr;
        }
      }
      else if (mapX >= 0 && mapX < DOOM_LEVEL_WIDTH && mapY >= 0 && mapY < DOOM_LEVEL_HEIGHT)
      {
        if (block == BLOCK_ENEMY || block == BLOCK_MEDIKIT || block == BLOCK_KEY)
          spawnEntity(block, mapX, mapY);
        int cell = mapY * DOOM_LEVEL_WIDTH + mapX;
        if (ammoDrops[cell])
          spawnAmmo(cell);
      }
    }

    if (!hit)
    {
      zbuffer[x] = DOOM_FAR;
      continue;
    }
    distance = max(distance, DOOM_MIN_WALL_DISTANCE);
    zbuffer[x] = distance;

    int lineHeight = (int)(DOOM_VIEW_HEIGHT / distance);
    int center = DOOM_VIEW_HEIGHT / 2 + (int)(viewBob / distance);
    int top = center - lineHeight / 2;
    int bottom = center + lineHeight / 2;
    int yFrom = max(0, top);
    int yTo = min(DOOM_VIEW_HEIGHT - 1, bottom);

    if (door)
    {
      int texX = constrain((int)((wallX - door->open) * BMP_DOOR_WIDTH), 0, BMP_DOOR_WIDTH - 1);
      for (int y = yFrom; y <= yTo; y++)
      {
        int texY = constrain((y - top) * BMP_DOOR_HEIGHT / max(1, bottom - top), 0, BMP_DOOR_HEIGHT - 1);
        bool pixel = bitmapBit(bmp_door_bits, BMP_DOOR_WIDTH / 8, texX, texY);
        if (pixel && (!door->locked || (x + y) % 2)) // locked doors are darker
          screen.drawPixel(x, y);
      }
    }
    else
    {
      int intensity = constrain(GRADIENT_COUNT - (int)(distance / DOOM_MAX_DEPTH * GRADIENT_COUNT) - side * 2, 0, GRADIENT_COUNT - 1);
      for (int y = yFrom; y <= yTo; y++)
      {
        if (gradientPixel(x, y, intensity))
          screen.drawPixel(x, y);
      }
    }
  }
}

// Scaled sprite with a mask, hidden behind closer walls column by column
static void drawSprite(int x, int y, const uint8_t *bits, const uint8_t *mask, int w, int h, int frame, float distance)
{
  int tw = (int)(w / distance);
  int th = (int)(h / distance);
  if (tw < 1 || th < 1)
    return;
  int byteWidth = w / 8;
  const uint8_t *frameBits = bits + byteWidth * h * frame;
  const uint8_t *frameMask = mask + byteWidth * h * frame;
  for (int tx = 0; tx < tw; tx++)
  {
    int sx = x + tx;
    if (sx < 0 || sx >= DOOM_SCREEN_WIDTH || zbuffer[sx] < distance)
      continue;
    int srcX = tx * w / tw;
    for (int ty = 0; ty < th; ty++)
    {
      int sy = y + ty;
      if (sy < 0 || sy >= DOOM_VIEW_HEIGHT)
        continue;
      int srcY = ty * h / th;
      if (!bitmapBit(frameMask, byteWidth, srcX, srcY))
        continue;
      screen.setDrawColor(bitmapBit(frameBits, byteWidth, srcX, srcY) ? 1 : 0);
      screen.drawPixel(sx, sy);
    }
  }
  screen.setDrawColor(1);
}

static int enemyFrame(const DoomEntity &e, unsigned long now)
{
  switch (e.state)
  {
  case ENEMY_ALERT:
    return (now / 500) % 2; // walking
  case ENEMY_FIRING:
    return 2;
  case ENEMY_HIT:
    return 3;
  case ENEMY_MELEE:
    return e.timer > DOOM_ENEMY_MELEE_INTERVAL - DOOM_ENEMY_MELEE_POSE ? 2 : 1;
  case ENEMY_DEAD:
    return e.timer > 0 ? 3 : 4;
  default:
    return 0;
  }
}

static void renderEntities(float viewBob, unsigned long now)
{
  // Far to close, so closer sprites are drawn over farther ones
  for (int i = 1; i < entityCount; i++)
  {
    for (int j = i; j > 0 && entities[j - 1].distance < entities[j].distance; j--)
    {
      DoomEntity swap = entities[j];
      entities[j] = entities[j - 1];
      entities[j - 1] = swap;
    }
  }

  for (int i = 0; i < entityCount; i++)
  {
    const DoomEntity &e = entities[i];
    DoomVec view = toView(e.pos);
    if (view.y <= 0.1f || view.y > DOOM_MAX_SPRITE_DEPTH)
      continue;
    float screenX = DOOM_HALF_WIDTH * (1 + view.x / view.y);
    float screenY = DOOM_VIEW_HEIGHT / 2 + viewBob / view.y;
    if (screenX < -DOOM_HALF_WIDTH || screenX > DOOM_SCREEN_WIDTH + DOOM_HALF_WIDTH)
      continue;

    switch (e.type)
    {
    case ENTITY_ENEMY:
      drawSprite(screenX - BMP_IMP_WIDTH / 2 / view.y, screenY - 8 / view.y, bmp_imp_bits, bmp_imp_mask,
                 BMP_IMP_WIDTH, BMP_IMP_HEIGHT, enemyFrame(e, now), view.y);
      break;
    case ENTITY_FIREBALL:
      drawSprite(screenX - BMP_FIREBALL_WIDTH / 2 / view.y, screenY - BMP_FIREBALL_HEIGHT / 2 / view.y, bmp_fireball_bits,
                 bmp_fireball_mask, BMP_FIREBALL_WIDTH, BMP_FIREBALL_HEIGHT, 0, view.y);
      break;
    case ENTITY_MEDIKIT:
    case ENTITY_KEY:
      drawSprite(screenX - BMP_ITEMS_WIDTH / 2 / view.y, screenY + 5 / view.y, bmp_items_bits, bmp_items_mask,
                 BMP_ITEMS_WIDTH, BMP_ITEMS_HEIGHT, e.type == ENTITY_KEY ? 1 : 0, view.y);
      break;
    case ENTITY_AMMO:
      drawSprite(screenX - AMMO_WIDTH / 2 / view.y, screenY + 5 / view.y, ammoBits, ammoMask, AMMO_WIDTH, AMMO_HEIGHT, 0, view.y);
      break;
    }
  }
}

static void renderGun(unsigned long now)
{
  float gunPos = DOOM_GUN_TARGET_POS;
  gunPos += DOOM_GUN_RECOIL * shotTimer / DOOM_SHOT_TIME;
  if (raiseTimer > 0)
    gunPos *= 1 - raiseTimer / DOOM_RAISE_TIME;
  gunPos -= viewSink * 2;

  int x = 48 + toPixel(sin(now * DOOM_JOG_SPEED) * 10 * jogging);
  int y = DOOM_VIEW_HEIGHT - toPixel(gunPos) + toPixel(fabs(cos(now * DOOM_JOG_SPEED)) * 8 * jogging);

  if (shotTimer > DOOM_SHOT_TIME - DOOM_MUZZLE_TIME)
  {
    for (int j = 0; j < BMP_FIRE_HEIGHT; j++)
    {
      for (int i = 0; i < BMP_FIRE_WIDTH; i++)
      {
        if (y - 11 + j < DOOM_VIEW_HEIGHT && bitmapBit(bmp_fire_bits, BMP_FIRE_WIDTH / 8, i, j))
          screen.drawPixel(x + 6 + i, y - 11 + j);
      }
    }
  }

  for (int j = 0; j < BMP_GUN_HEIGHT && y + j < DOOM_VIEW_HEIGHT; j++)
  {
    for (int i = 0; i < BMP_GUN_WIDTH; i++)
    {
      if (!bitmapBit(bmp_gun_mask, BMP_GUN_WIDTH / 8, i, j))
        continue;
      screen.setDrawColor(bitmapBit(bmp_gun_bits, BMP_GUN_WIDTH / 8, i, j) ? 1 : 0);
      screen.drawPixel(x + i, y + j);
    }
  }
  screen.setDrawColor(1);
}

static void renderHud()
{
  char buf[8];
  drawText(2, DOOM_HUD_Y, "{}"); // health symbol
  snprintf(buf, sizeof(buf), "%d", health);
  drawText(12, DOOM_HUD_Y, buf);
  drawText(36, DOOM_HUD_Y, "[]"); // key symbol
  snprintf(buf, sizeof(buf), "%d", keys);
  drawText(46, DOOM_HUD_Y, buf);

  // Bullet symbol and ammo on the right, blinking when empty
  if (ammo == 0 && frameCount % 8 < 4)
    return;
  snprintf(buf, sizeof(buf), "%d", ammo);
  int x = DOOM_SCREEN_WIDTH - textWidth(buf);
  drawText(x, DOOM_HUD_Y, buf);
  screen.drawPixel(x - 4, DOOM_HUD_Y);
  screen.drawBox(x - 5, DOOM_HUD_Y + 1, 3, 5);
}

static void renderMessage()
{
  if (messageTimer <= 0)
    return;
  int width = textWidth(message);
  int x = (DOOM_SCREEN_WIDTH - width) / 2;
  screen.setDrawColor(0);
  screen.drawBox(x - 2, 0, width + 4, FONT_CHAR_HEIGHT + 3);
  screen.setDrawColor(1);
  drawText(x, 1, message);
}

// Covers the screen with a black dither pattern, GRADIENT_COUNT - 1 is fully black
static void fadeScreen(int intensity)
{
  screen.setDrawColor(0);
  for (int y = 0; y < 64; y++)
  {
    for (int x = 0; x < DOOM_SCREEN_WIDTH; x++)
    {
      if (gradientPixel(x, y, intensity))
        screen.drawPixel(x, y);
    }
  }
  screen.setDrawColor(1);
}

static void renderView(unsigned long now)
{
  float viewBob = fabs(sin(now * DOOM_JOG_SPEED)) * DOOM_JOG_HEIGHT * jogging - viewSink;
  renderMap(viewBob);
  renderEntities(viewBob, now);
  renderGun(now);
  renderHud();
  renderMessage();

  if (flashTimer > 0)
  {
    screen.setDrawColor(2); // invert the view
    screen.drawBox(0, 0, DOOM_SCREEN_WIDTH, DOOM_VIEW_HEIGHT);
    screen.setDrawColor(1);
  }
}

static void drawLogo()
{
  int left = (DOOM_SCREEN_WIDTH - BMP_LOGO_WIDTH) / 2;
  int top = (64 - BMP_LOGO_HEIGHT) / 3;
  for (int j = 0; j < BMP_LOGO_HEIGHT; j++)
  {
    for (int i = 0; i < BMP_LOGO_WIDTH; i++)
    {
      if (bitmapBit(bmp_logo_bits, BMP_LOGO_WIDTH / 8, i, j))
        screen.drawPixel(left + i, top + j);
    }
  }
}

// ---------------------------------------------------------------------------------------------------------------
// Game

static void updatePlaying(float dt, int steps, bool pressed)
{
  runTime += dt;
  fadeTimer = max(0.0f, fadeTimer - dt);
  flashTimer = max(0.0f, flashTimer - dt);
  messageTimer = max(0.0f, messageTimer - dt);
  raiseTimer = max(0.0f, raiseTimer - dt);

  // Knob: the view follows the detents smoothly, clockwise turns right
  turnRemaining -= steps * DOOM_TURN_PER_STEP;
  float turn = turnRemaining * min(1.0f, dt * DOOM_TURN_EASING);
  turnRemaining -= turn;
  rotateView(turn);

  // Scale: pressing walks forward, pulling walks backward
  float weight = gamePressedWeight(dt, DOOM_ZERO_THRESHOLD, true, true);
  float targetSpeed = 0;
  if (weight >= DOOM_WALK_MIN_WEIGHT)
  {
    float pressure = min(1.0f, (weight - DOOM_WALK_MIN_WEIGHT) / (DOOM_WALK_FULL_WEIGHT - DOOM_WALK_MIN_WEIGHT));
    targetSpeed = mix(DOOM_WALK_SLOWEST, 1.0f, pressure) * DOOM_WALK_SPEED;
  }
  else if (weight <= -DOOM_WALK_MIN_WEIGHT)
  {
    float pull = min(1.0f, (-weight - DOOM_WALK_MIN_WEIGHT) / (DOOM_WALK_FULL_WEIGHT - DOOM_WALK_MIN_WEIGHT));
    targetSpeed = -mix(DOOM_WALK_SLOWEST, 1.0f, pull) * DOOM_WALK_SPEED;
  }
  speed += (targetSpeed - speed) * min(1.0f, dt * DOOM_WALK_EASING);
  if (fabs(speed) < 0.01f)
    speed = 0;
  jogging = fabs(speed) / DOOM_WALK_SPEED;
  movePlayer(playerDir.x * speed * dt, playerDir.y * speed * dt);

  // Gun
  shotTimer = max(0.0f, shotTimer - dt);
  if (pressed && raiseTimer <= 0)
  {
    if (ammo == 0)
      showMessage("NO AMMO");
    else
      shotQueued = true;
  }
  if (shotQueued && shotTimer <= 0)
  {
    shotQueued = false;
    fire();
  }

  updateEntities(dt);
  updateDoors(dt);

  if (getBlock((int)floor(playerPos.x), (int)floor(playerPos.y)) == BLOCK_EXIT)
  {
    doomState = DOOM_EXITING;
    stateTime = 0;
    unsigned long seconds = max(1UL, (unsigned long)runTime);
    if (bestTime == 0 || seconds < bestTime)
    {
      bestTime = seconds;
      newBestTime = true;
      gameSaveBestScore("doomTime", bestTime);
    }
    Serial.printf("Doom: level clear after %lus, %d of %d kills\n", seconds, kills, enemyTotal);
  }
  else if (health == 0)
  {
    doomState = DOOM_DYING;
    stateTime = 0;
    Serial.printf("Doom: died after %.0fs, %d of %d kills\n", runTime, kills, enemyTotal);
  }
}

static void formatTime(char *buf, size_t size, unsigned long seconds)
{
  snprintf(buf, size, "%lu:%02lu", seconds / 60, seconds % 60);
}

bool doomFrame(float dt, int steps, bool click, unsigned long now)
{
  frameCount++;
  bool pressed;
  bool longPress;
  readButton(now, pressed, longPress);
  bool playing = doomState == DOOM_INTRO || doomState == DOOM_PLAYING ||
                 doomState == DOOM_DYING || doomState == DOOM_EXITING;

  char buf[32];
  char time[12];
  switch (doomState)
  {
  case DOOM_INTRO:
    gamePressedWeight(dt, DOOM_ZERO_THRESHOLD, false, true); // settles the zero point before the level
    if (pressed || now - stateStartedAt >= DOOM_INTRO_MS)
    {
      doomState = DOOM_PLAYING; // a press skips the logo, the level fades in
      longPressHandled = true;  // holding on does not open the pause menu
      renderView(now);
      fadeScreen(GRADIENT_COUNT - 1);
    }
    else
    {
      drawLogo();
    }
    break;

  case DOOM_PLAYING:
    if (longPress)
    {
      doomState = DOOM_PAUSED; // this frame is still drawn, the pause screen follows
      menuChoice = 0;
      ignoreClick = true;
      shotQueued = false;
      renderView(now);
      break;
    }
    updatePlaying(dt, steps, pressed);
    renderView(now);
    if (fadeTimer > 0)
      fadeScreen(toPixel(fadeTimer / DOOM_FADE_TIME * (GRADIENT_COUNT - 1)));
    break;

  case DOOM_PAUSED:
    menuChoice = gameSelectChoice(steps, menuChoice);
    if (click && ignoreClick)
    {
      ignoreClick = false;
    }
    else if (click)
    {
      if (menuChoice == 0)
      {
        doomState = DOOM_PLAYING;
        gameResetPressure(); // the scale may have drifted during the pause
      }
      else
      {
        gameExit();
      }
    }
    snprintf(buf, sizeof(buf), "Kills: %d/%d", kills, enemyTotal);
    gameDrawChoiceScreen("Paused", buf, "Continue", "Exit", menuChoice);
    break;

  case DOOM_DYING:
    stateTime += dt;
    flashTimer = max(0.0f, flashTimer - dt);
    viewSink = min(1.0f, stateTime / 0.6f) * 10; // the view sinks to the floor
    jogging = 0;
    updateEntities(dt);
    updateDoors(dt);
    renderView(now);
    if (stateTime >= DOOM_DEATH_TIME)
    {
      doomState = DOOM_OVER;
      stateStartedAt = now;
      menuChoice = 0;
    }
    break;

  case DOOM_OVER:
    menuChoice = gameSelectChoice(steps, menuChoice);
    if (click && now - stateStartedAt >= GAME_OVER_INPUT_DELAY_MS)
    {
      if (menuChoice == 0)
        startLevel();
      else
        gameExit();
    }
    snprintf(buf, sizeof(buf), "Kills: %d/%d", kills, enemyTotal);
    gameDrawChoiceScreen("Game Over", buf, "Retry", "Exit", menuChoice);
    break;

  case DOOM_EXITING:
    stateTime += dt;
    jogging = 0;
    renderView(now);
    fadeScreen(min(GRADIENT_COUNT - 1, toPixel(stateTime / DOOM_EXIT_TIME * (GRADIENT_COUNT - 1))));
    if (stateTime >= DOOM_EXIT_TIME)
    {
      doomState = DOOM_CLEAR;
      stateStartedAt = now;
      menuChoice = 0;
    }
    break;

  case DOOM_CLEAR:
    menuChoice = gameSelectChoice(steps, menuChoice);
    if (click && now - stateStartedAt >= GAME_OVER_INPUT_DELAY_MS)
    {
      if (menuChoice == 0)
        startLevel();
      else
        gameExit();
    }
    formatTime(time, sizeof(time), max(1UL, (unsigned long)runTime));
    if (newBestTime)
    {
      snprintf(buf, sizeof(buf), "%s  New best!", time);
    }
    else
    {
      char best[12];
      formatTime(best, sizeof(best), bestTime);
      snprintf(buf, sizeof(buf), "%s  Best %s", time, best);
    }
    gameDrawChoiceScreen("Level Clear", buf, "Retry", "Exit", menuChoice);
    break;
  }
  return playing;
}
