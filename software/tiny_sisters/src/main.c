// ===================================================================================
// Project:   Tiny Sisters - Jump'n'Run Game for CH32V003
// Version:   v1.0
// Year:      2026
// Github:    https://github.com/wagiminator
// EasyEDA:   https://easyeda.com/wagiminator
// License:   http://creativecommons.org/licenses/by-sa/3.0/
// ===================================================================================
//
// Description:
// ------------
// A small Giana Sisters/Super Mario Bros-like jump'n'run for the CH32V003 Mini Game
// Console. Run and jump through sixteen scrolling levels: meadows, hills, caves,
// lakes, forests, deep caves, the sky and castles. Jump on owls, beetles, frogs,
// bees and fish, but beware of the spiky hedgehogs. Bump your head against the
// bonus blocks to get diamonds. The magic wheel turns the sister into a punk who
// can smash bricks, the dream orb lets her throw bouncing dream bubbles. Collect
// 100 diamonds for an extra life and reach the exit gate of each level before the
// time runs out. Half way through each level there is an invisible checkpoint.
// At the end of each castle the owl king is waiting for you.
//
// Controls:
// ---------
// Left/Right:  walk
// Up:          jump (hold longer to jump higher)
// Fire button: throw a dream bubble (with the dream orb), start game
//
// Scoring:
// --------
// Diamond 50, brick 10, owl 100, beetle 150, frog 200, bee 200, fish 200,
// hedgehog 400 (not by stomping), owl king 5000, wheel/orb 1000,
// time bonus 10 per second.
// Extra life for every 100 diamonds.
//
// Compilation Settings:
// ---------------------
// - Make sure GCC toolchain (gcc-riscv64-unknown-elf, newlib) and Python3 with rvprog
//   are installed. In addition, Linux requires access rights to WCH-LinkE programmer.
// - Connect the WCH-LinkE programmer to the MCU board.
// - Run 'make flash'.


#include <driver.h>           // TinyJoypad conversion driver
#include <spritebank.h>       // grafix
#include <levels.h>           // level maps

// ===================================================================================
// Game Settings
// ===================================================================================
#define FRAME_TIME    (33 * DLY_MS_TIME)  // ~30 frames per second
#define ROWS          7       // tile rows of the playfield (page 1..7, page 0: status line)
#ifndef RING
#define RING          32      // columns of the map ring buffer (power of two)
#endif
#define MAX_ENEMIES   6       // enemies at the same time
#define MAX_BALLS     2       // dream bubbles
#define MAX_DEBRIS    4       // pieces of a smashed brick
#define LIVES         3       // lives at game start
#define TIME_LIMIT    200     // seconds per level
#define CAM_POS       52      // screen position of the sister when scrolling

// Physics (all speeds in 1/16 pixels per frame)
#define WALK_ACC      3       // acceleration
#define WALK_MAX      22      // top speed
#define JUMP_SPEED    50      // initial jump speed
#define GRAVITY       6       // gravity
#define GRAVITY_HOLD  3       // gravity while the jump button is held
#define FALL_MAX      64      // terminal velocity
#define STOMP_SPEED   40      // bounce off an enemy

// Tiles
enum {T_EMPTY, T_TOP, T_GROUND, T_BRICK, T_BONUS, T_POWER, T_USED, T_STONE,
      T_DIAMOND, T_SPIKES, T_CLOUD, T_BUSH, T_BRIDGE, T_GOAL, T_WATER, T_PIPE};
#define SOLID         0x80FE  // solid tiles (bit mask)
#define IS_SOLID(t)   ((SOLID >> (t)) & 1)

// Enemies, enemy states, items, render modes, sprite flags
enum {E_NONE, E_OWL, E_BEETLE, E_FROG, E_BEE, E_SPIKY, E_FISH, E_BOSS};
enum {S_ALIVE, S_SQUASHED, S_FALLING};
enum {I_NONE, I_WHEEL, I_ORB};
enum {R_TITLE, R_GAME, R_INTRO, R_GAMEOVER, R_WIN};
#define F_MIRROR      1
#define F_VFLIP       2

// ===================================================================================
// Game Objects
// ===================================================================================
typedef struct {
  int16_t x;                  // position in pixels
  int16_t y;                  // position in 1/16 pixels
  int16_t vy;                 // vertical speed
  int8_t  dir;                // walking direction (-1, +1)
  uint8_t type;               // E_NONE: free slot
  uint8_t state;
  uint8_t t;                  // timer
} ENEMY;

typedef struct {
  int16_t x;                  // position in pixels
  int16_t y;                  // position in 1/16 pixels
  int16_t vy;
  int8_t  dx;                 // horizontal speed in pixels per frame
  uint8_t on;
} PARTICLE;

ENEMY    enemy[MAX_ENEMIES];
PARTICLE ball[MAX_BALLS];
PARTICLE debris[MAX_DEBRIS];
uint8_t  ring[RING][ROWS];          // decoded map columns around the camera
uint8_t  buf[128];                  // one display page

// Player
int32_t  px;                        // position in 1/16 pixels
int16_t  py, vx, vy;
uint8_t  onGround, coyote, jumpLatch, fireLatch, facing, power, invuln;
uint8_t  dying, cleared, walkAnim;

// Level and game progress
uint8_t  level, levelLen, loop, cpReached, spawnOn, gameOver;
uint16_t levelStart, timeLeft;
int16_t  camX, loadedTo;
uint8_t  frame, timeTick, lives, diamonds, endTimer;
uint32_t score, hiscore;

// Bumped block, item and diamond popping out of a box
int16_t  bumpCol;
int8_t   bumpRow;
uint8_t  bumpT;
uint8_t  itemType, itemRise;
int16_t  itemX, itemY, itemVY;
int8_t   itemDir;
int16_t  popX, popY;
uint8_t  popT;

// Owl king at the end of the castle levels
uint8_t  bossHP, bossHit;

// Message in the middle of the screen
const char *msgText;
uint8_t  msgTimer;

// Movement table for bees (sine)
const int8_t SINE[32] = {
  0, 1, 2, 3, 4, 5, 5, 6, 6, 6, 5, 5, 4, 3, 2, 1,
  0, -1, -2, -3, -4, -5, -5, -6, -6, -6, -5, -5, -4, -3, -2, -1
};

// Distant hills (heights in pixels, drawn with half scrolling speed)
const uint8_t HILLS[32] = {
  2, 4, 6, 8, 10, 12, 13, 14, 14, 13, 12, 10, 8, 7, 6, 6,
  6, 7, 8, 9, 10, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 1
};

// Points for the enemy types
const uint16_t POINTS[] = {0, 100, 150, 200, 200, 400, 200, 5000};

// ===================================================================================
// Function Prototypes
// ===================================================================================
void TitleScreen(void);
void IntroScreen(void);
void GameOverScreen(void);
void WinScreen(void);
void NewGame(void);
void StartLevel(void);
void PlayLevel(void);
void UpdateGame(void);
void Tiny_Flip(uint8_t mode);
void WaitFrame(uint32_t start);
void MapReset(int16_t col);
void MapUpdate(void);
uint8_t Tile(int16_t col, int8_t row);
void PlayerUpdate(void);
void PlayerHurt(void);
void PlayerDie(void);
void HitBlock(int16_t col, int8_t row);
void EnemySpawn(uint8_t type, int16_t col);
void EnemiesUpdate(void);
void EnemyKill(ENEMY *e);
void BallsUpdate(void);
void ItemUpdate(void);
void DebrisUpdate(void);
void AddScore(uint16_t points);
void AddDiamond(void);
void ShowMessage(const char *text, uint8_t time);
uint8_t Collide(int16_t x1, int16_t y1, uint8_t w1, uint8_t h1, int16_t x2, int16_t y2, uint8_t w2, uint8_t h2);

// ===================================================================================
// Main Function
// ===================================================================================
int main(void) {
  // Setup
  JOY_init();

  // Loop
  while(1) {
    TitleScreen();
    NewGame();
    while(!gameOver) {
      IntroScreen();
      StartLevel();
      PlayLevel();
      if(cleared) {
        if(++level >= LEVELS) {                     // all levels done: next loop
          WinScreen();
          level = 0; loop++;
        }
        cpReached = 0;
      }
      else if(--lives == 0) gameOver = 1;
    }
    GameOverScreen();
  }
}

// ===================================================================================
// Screens
// ===================================================================================

// Title screen with the scrolling first level; returns when the fire button is pressed
void TitleScreen(void) {
  level = 0; cpReached = 0; spawnOn = 0;
  levelStart = LEVEL_START[0]; levelLen = LEVEL_LEN[0];
  MapReset(0);
  while(JOY_act_pressed());                       // wait until fire button released
  while(1) {
    uint32_t start = STK->CNT;
    frame++;
    if(frame & 1) {
      if(++camX > levelLen * 8 - 128) MapReset(0);
      MapUpdate();
    }
    Tiny_Flip(R_TITLE);
    if(JOY_act_pressed()) break;
    JOY_random();                                 // stir the random generator
    WaitFrame(start);
  }
  JOY_sound(120, 40); JOY_sound(160, 40); JOY_sound(200, 40); JOY_sound(240, 80);
}

// Level number and lives
void IntroScreen(void) {
  for(frame = 0; frame < 75; frame++) {
    uint32_t start = STK->CNT;
    Tiny_Flip(R_INTRO);
    WaitFrame(start);
  }
}

void GameOverScreen(void) {
  if(score > hiscore) hiscore = score;
  JOY_sound(120, 100); JOY_sound(90, 100); JOY_sound(60, 200);
  frame = 0;
  while(JOY_act_pressed());
  while(JOY_act_released() || frame < 30) {
    uint32_t start = STK->CNT;
    frame++;
    Tiny_Flip(R_GAMEOVER);
    WaitFrame(start);
  }
}

void WinScreen(void) {
  static const uint8_t MELODY[] = {150, 170, 190, 200, 190, 200, 230, 0};
  for(const uint8_t *m = MELODY; *m; m++) JOY_sound(*m, 120);
  for(frame = 0; frame < 150; frame++) {
    uint32_t start = STK->CNT;
    Tiny_Flip(R_WIN);
    WaitFrame(start);
  }
}

// Keep a steady frame rate
void WaitFrame(uint32_t start) {
  while((int32_t)(STK->CNT - start) < FRAME_TIME);
}

// ===================================================================================
// Game Control
// ===================================================================================
void NewGame(void) {
  score = 0; lives = LIVES; diamonds = 0; power = 0;
  level = 0; loop = 0; cpReached = 0; gameOver = 0;
}

// Set up the current level (at the start or at the checkpoint)
void StartLevel(void) {
  uint8_t i;
  int16_t c0 = cpReached ? LEVEL_CP[level] : 1;
  for(i = 0; i < MAX_ENEMIES; i++) enemy[i].type = E_NONE;
  for(i = 0; i < MAX_BALLS; i++)   ball[i].on = 0;
  for(i = 0; i < MAX_DEBRIS; i++)  debris[i].on = 0;
  itemType = I_NONE; popT = 0; bumpT = 0; msgTimer = 0;
  levelStart = LEVEL_START[level]; levelLen = LEVEL_LEN[level];

  // Map and camera (no enemies on the first screen)
  spawnOn = 0;
  MapReset(c0 > 2 ? c0 - 2 : 0);
  spawnOn = 1;

  // Sister stands on the ground of column c0
  px = (int32_t)(c0 << 3) << 4;
  for(i = ROWS - 1; i; i--) if(IS_SOLID(Tile(c0, i)) && !IS_SOLID(Tile(c0, i - 1))) break;
  py = ((i - 1) << 3) << 4;
  vx = vy = 0; facing = 0; onGround = 1;
  invuln = 0; dying = 0; cleared = 0; endTimer = 0;
  jumpLatch = 1; fireLatch = 1;
  timeLeft = TIME_LIMIT; timeTick = 0;
  frame = 0;
}

// Run the level until it is cleared or the sister has lost a life
void PlayLevel(void) {
  static const uint8_t TUNE[] = {100, 150, 200, 150, 200, 240, 0};
  for(const uint8_t *m = TUNE; *m; m++) JOY_sound(*m, 50);
  while(1) {
    uint32_t start = STK->CNT;
    UpdateGame();
    Tiny_Flip(R_GAME);
    WaitFrame(start);
    if(endTimer && --endTimer == 0) return;
  }
}

// One frame of the game
void UpdateGame(void) {
  frame++;

  // Time
  if(!dying && !cleared && ++timeTick >= 30) {
    timeTick = 0;
    if(timeLeft) timeLeft--;
    if(timeLeft <= 30) JOY_sound(220, 10);
    if(!timeLeft) {ShowMessage("TIME UP", 60); PlayerDie();}
  }

  // Level cleared: count the remaining time as bonus
  if(cleared) {
    if(timeLeft) {
      uint8_t n = timeLeft > 4 ? 5 : timeLeft;
      timeLeft -= n; AddScore(n * 10);
      JOY_sound(200 + (frame & 31), 4);
      if(!timeLeft) endTimer = 45;
    }
    if(walkAnim < 250) walkAnim++;
  }

  PlayerUpdate();
  MapUpdate();
  EnemiesUpdate();
  BallsUpdate();
  ItemUpdate();
  DebrisUpdate();

  if(bumpT) bumpT--;
  if(popT)  {popT--; popY--;}
  if(msgTimer) msgTimer--;
}

void AddScore(uint16_t points) {
  score += points;
}

void AddDiamond(void) {
  AddScore(50);
  JOY_sound(230, 6); JOY_sound(250, 8);
  if(++diamonds >= 100) {
    diamonds = 0;
    if(lives < 9) lives++;
    ShowMessage("EXTRA LIFE", 60);
    JOY_sound(180, 40); JOY_sound(220, 40); JOY_sound(250, 60);
  }
}

void ShowMessage(const char *text, uint8_t time) {
  msgText = text; msgTimer = time;
}

// Bounding box collision
uint8_t Collide(int16_t x1, int16_t y1, uint8_t w1, uint8_t h1, int16_t x2, int16_t y2, uint8_t w2, uint8_t h2) {
  return (x1 < x2 + w2) && (x2 < x1 + w1) && (y1 < y2 + h2) && (y2 < y1 + h1);
}

// ===================================================================================
// Map
// ===================================================================================

// Decode one column of the level into the ring buffer, spawn its enemy
void MapLoad(int16_t col) {
  const uint8_t *p = &LEVEL_COLS[LEVEL_DATA[levelStart + col] << 2];
  uint8_t *r = ring[col & (RING - 1)];
  r[0] = p[0] & 15; r[1] = p[0] >> 4;
  r[2] = p[1] & 15; r[3] = p[1] >> 4;
  r[4] = p[2] & 15; r[5] = p[2] >> 4;
  r[6] = p[3] & 15;
  if(spawnOn && (p[3] >> 4)) EnemySpawn(p[3] >> 4, col);
}

// Load the columns up to the right edge of the screen (and a bit more)
void MapUpdate(void) {
  int16_t last = (camX >> 3) + 17;
  if(last >= levelLen) last = levelLen - 1;
  while(loadedTo < last) MapLoad(++loadedTo);
}

void MapReset(int16_t col) {
  camX = col << 3;
  loadedTo = col - 1;
  MapUpdate();
}

// Tile at map position; left of the level and right of the end is solid stone
uint8_t Tile(int16_t col, int8_t row) {
  if(row < 0 || row >= ROWS) return T_EMPTY;
  if(col < 0 || col >= levelLen) return T_STONE;
  return ring[col & (RING - 1)][row];
}

void SetTile(int16_t col, int8_t row, uint8_t t) {
  ring[col & (RING - 1)][row] = t;
}

// Tile at pixel position (playfield coordinates)
uint8_t TileAt(int16_t x, int16_t y) {
  if(y < 0) return T_EMPTY;
  return Tile(x >> 3, y >> 3);
}

uint8_t SolidAt(int16_t x, int16_t y) {
  return IS_SOLID(TileAt(x, y));
}

// Ground for standing on: solid tile or the top of a bridge
uint8_t GroundAt(int16_t x, int16_t y) {
  uint8_t t = TileAt(x, y);
  return IS_SOLID(t) || (t == T_BRIDGE && !(y & 4));
}

// Surface row: first free row above solid ground in a column
int16_t SurfaceY(int16_t col) {
  for(int8_t r = ROWS - 1; r > 0; r--)
    if(IS_SOLID(Tile(col, r)) && !IS_SOLID(Tile(col, r - 1))) return (r - 1) << 3;
  return 0;
}

// ===================================================================================
// Player
// ===================================================================================
void PlayerUpdate(void) {
  int16_t x, y, ny, i;

  // Dying: jump up and fall off the screen
  if(dying) {
    vy += 4;
    py += vy;
    if(++dying > 75) endTimer = 1;
    return;
  }

  // Level cleared: walk into the exit gate
  if(cleared) {
    if(walkAnim < 24) {px += 16; facing = 0;}
    vx = 0;
    x = px >> 4; y = py >> 4;
    if(!GroundAt(x + 2, y + 8) && !GroundAt(x + 5, y + 8)) py += 32;
    return;
  }

  // Walk
  if(JOY_left_pressed()) {
    vx -= WALK_ACC; facing = F_MIRROR;
    if(vx < -WALK_MAX) vx = -WALK_MAX;
  }
  else if(JOY_right_pressed()) {
    vx += WALK_ACC; facing = 0;
    if(vx > WALK_MAX) vx = WALK_MAX;
  }
  else {                                          // friction
    if(vx > 2) vx -= 2;
    else if(vx < -2) vx += 2;
    else vx = 0;
  }

  // Horizontal movement and walls
  px += vx;
  x = px >> 4; y = py >> 4;
  if(vx > 0 && (SolidAt(x + 6, y) || SolidAt(x + 6, y + 7))) {
    px = (int32_t)((((x + 6) & ~7) - 7) << 4); vx = 0;
  }
  if(vx < 0 && (SolidAt(x + 1, y) || SolidAt(x + 1, y + 7))) {
    px = (int32_t)((((x + 1) & ~7) + 7) << 4); vx = 0;
  }
  if((px >> 4) < camX) {px = (int32_t)camX << 4; if(vx < 0) vx = 0;}
  if((px >> 4) > levelLen * 8 - 8) px = (int32_t)(levelLen * 8 - 8) << 4;

  // Jump (up button), higher while held
  uint8_t up = JOY_up_pressed();
  if(!up) jumpLatch = 0;
  if(up && !jumpLatch && (onGround || coyote)) {
    vy = -JUMP_SPEED; jumpLatch = 1; onGround = 0; coyote = 0;
    JOY_sound(180, 6);
  }
  // Standing on the ground: check if it is still there
  x = px >> 4;
  if(onGround) {
    y = (py >> 4) + 8;
    if(!GroundAt(x + 1, y) && !GroundAt(x + 6, y)) {onGround = 0; coyote = 4;}
  }

  // Falling or jumping: gravity, ground, bridges and blocks
  if(!onGround) {
    vy += (up && vy < 0) ? GRAVITY_HOLD : GRAVITY;
    if(vy > FALL_MAX) vy = FALL_MAX;
    if(coyote) coyote--;
    ny = (py + vy) >> 4;
    if(vy > 0) {
      uint8_t land = 0;
      for(i = x + 1; i <= x + 6; i += 5) {
        uint8_t t = TileAt(i, ny + 7);
        if(IS_SOLID(t)) land = 1;
        if(t == T_BRIDGE && ((py >> 4) + 7) < ((ny + 7) & ~7) + 1) land = 1;
      }
      if(land) {
        py = (((ny + 7) & ~7) - 8) << 4; vy = 0;
        onGround = 1;
      }
      else py += vy;
    }
    else {
      if(ny >= 0 && (SolidAt(x + 2, ny) || SolidAt(x + 5, ny))) {
        // Bump the block above the middle of the head (or the one that was hit)
        int16_t c = (x + 4) >> 3;
        if(!IS_SOLID(Tile(c, ny >> 3))) c = SolidAt(x + 2, ny) ? (x + 2) >> 3 : (x + 5) >> 3;
        HitBlock(c, ny >> 3);
        py = ((ny & ~7) + 8) << 4; vy = 0;
      }
      else py += vy;
    }
  }

  // Throw a dream bubble (fire button) with the dream orb
  if(JOY_act_released()) fireLatch = 0;
  if(JOY_act_pressed() && !fireLatch && power == 2) {
    fireLatch = 1;
    for(i = 0; i < MAX_BALLS; i++) {
      PARTICLE *b = &ball[i];
      if(b->on) continue;
      b->on = 1; b->x = (px >> 4) + (facing ? -2 : 6); b->y = py + 32;
      b->dx = facing ? -3 : 3; b->vy = 0;
      JOY_sound(200, 5);
      break;
    }
  }

  // Walking animation
  if(vx) walkAnim++;
  if(invuln) invuln--;

  // Tiles touched: diamonds, spikes, water, exit gate
  x = px >> 4; y = py >> 4;
  for(i = 0; i < 4; i++) {
    int16_t cx = x + ((i & 1) ? 5 : 2), cy = y + ((i & 2) ? 6 : 1);
    uint8_t t = TileAt(cx, cy);
    if(t == T_DIAMOND) {SetTile(cx >> 3, cy >> 3, T_EMPTY); AddDiamond();}
    else if(t == T_SPIKES || t == T_WATER) {PlayerDie(); return;}
    else if(t == T_GOAL && !cleared) {
      cleared = 1; walkAnim = 0; vx = 0;
      JOY_sound(150, 60); JOY_sound(200, 60); JOY_sound(250, 120);
      ShowMessage("WELL DONE", 90);
    }
  }

  // Fallen into a pit
  if(y > 56) PlayerDie();

  // Camera follows the sister (only to the right)
  x -= camX;
  if(x > CAM_POS) {
    camX += x - CAM_POS;
    if(camX > levelLen * 8 - 128) camX = levelLen * 8 - 128;
  }

  // Checkpoint half way through the level
  if(!cpReached && (px >> 7) >= LEVEL_CP[level]) {
    cpReached = 1;
    JOY_sound(200, 20); JOY_sound(240, 30);
  }
}

// Touched by an enemy: lose the power or die
void PlayerHurt(void) {
  if(invuln || dying || cleared) return;
  if(power) {
    power = 0; invuln = 60;
    JOY_sound(120, 30); JOY_sound(80, 40);
  }
  else PlayerDie();
}

void PlayerDie(void) {
  if(dying) return;
  dying = 1; vy = -56; power = 0;
  JOY_sound(200, 40); JOY_sound(150, 40); JOY_sound(100, 60); JOY_sound(60, 100);
}

// The sister bumps her head against a block
void HitBlock(int16_t col, int8_t row) {
  uint8_t t = Tile(col, row), i;
  switch(t) {
    case T_BONUS:                                 // diamond box
      SetTile(col, row, T_USED);
      popX = col << 3; popY = (row << 3) - 8; popT = 12;
      AddDiamond();
      break;
    case T_POWER:                                 // wheel or orb box
      SetTile(col, row, T_USED);
      if(itemType == I_NONE) {
        itemType = power ? I_ORB : I_WHEEL;
        itemX = col << 3; itemY = (row << 3) << 4; itemRise = 8;
        itemVY = 0; itemDir = 1;
      }
      JOY_sound(100, 20); JOY_sound(140, 20); JOY_sound(180, 20);
      break;
    case T_BRICK:                                 // the punk smashes bricks
      if(power) {
        SetTile(col, row, T_EMPTY);
        for(i = 0; i < MAX_DEBRIS; i++) {
          PARTICLE *d = &debris[i];
          d->on = 1; d->x = (col << 3) + ((i & 1) << 2); d->y = ((row << 3) + ((i & 2) << 1)) << 4;
          d->dx = (i & 1) ? 1 : -1; d->vy = (i & 2) ? -24 : -40;
        }
        AddScore(10);
        for(i = 0; i < 8; i++) JOY_sound((JOY_random() & 0x3F) + 30, 3);
        return;
      }
      break;
    default:
      JOY_sound(40, 6);
      return;
  }
  bumpCol = col; bumpRow = row; bumpT = 6;
  JOY_sound(60, 8);

  // Enemies standing on the block are knocked out
  for(i = 0; i < MAX_ENEMIES; i++) {
    ENEMY *e = &enemy[i];
    if(e->type && e->type != E_BOSS && e->state == S_ALIVE && ((e->y >> 4) + 8 == (row << 3)) && Collide(e->x, 0, 8, 1, col << 3, 0, 8, 1))
      EnemyKill(e);
  }
}

// ===================================================================================
// Enemies
// ===================================================================================
void EnemySpawn(uint8_t type, int16_t col) {
  for(uint8_t i = 0; i < MAX_ENEMIES; i++) {
    ENEMY *e = &enemy[i];
    if(e->type) continue;
    e->type = type; e->state = S_ALIVE; e->dir = -1;
    e->x = col << 3; e->vy = 0; e->t = JOY_random() & 31;
    if(type == E_BEE)       e->y = (12 + (JOY_random() & 15)) << 4;
    else if(type == E_FISH) e->y = 56 << 4;
    else                    e->y = SurfaceY(col) << 4;
    if(type == E_BOSS)      {e->y -= 8 << 4; bossHP = 6; bossHit = 0;}
    return;
  }
}

// Knocked out by a dream bubble or a bumped block: falls off the screen
void EnemyKill(ENEMY *e) {
  e->state = S_FALLING; e->vy = -40; e->t = 0;
  AddScore(POINTS[e->type]);
  JOY_sound(150, 8); JOY_sound(100, 8);
}

// Gravity and walking for enemies on the ground (8x8 or 16x16 pixels)
void EnemyMove(ENEMY *e, uint8_t step, uint8_t careful) {
  uint8_t sz = (e->type == E_BOSS) ? 16 : 8;
  int16_t y = e->y >> 4;
  uint8_t grounded = GroundAt(e->x + 1, y + sz) || GroundAt(e->x + sz - 2, y + sz);
  if(step) {
    int16_t fx = (e->dir > 0) ? e->x + sz : e->x - 1;
    if(SolidAt(fx, y + sz - 4) || (careful && grounded && !GroundAt(fx, y + sz))) e->dir = -e->dir;
    else e->x += e->dir;
  }
  if(grounded && e->vy >= 0) {e->vy = 0; e->y = y << 4; return;}
  e->vy += GRAVITY;
  if(e->vy > FALL_MAX) e->vy = FALL_MAX;
  int16_t ny = (e->y + e->vy) >> 4;
  if(e->vy > 0 && (GroundAt(e->x + 1, ny + sz - 1) || GroundAt(e->x + sz - 2, ny + sz - 1))) {
    e->y = (((ny + sz - 1) & ~7) - sz) << 4; e->vy = 0;
  }
  else if(e->vy < 0 && SolidAt(e->x + (sz >> 1), ny)) e->vy = 0;
  else e->y += e->vy;
}

// The owl king is hit: by a stomp (2) or a dream bubble (1)
void BossHit(ENEMY *e, uint8_t damage) {
  if(bossHit) return;
  bossHit = 30;
  JOY_sound(60, 20); JOY_sound(40, 20);
  if(bossHP > damage) bossHP -= damage;
  else {EnemyKill(e); ShowMessage("OWL KING DEFEATED", 60);}
}

void EnemiesUpdate(void) {
  uint8_t fast = loop ? 1 : 0;                   // enemies are faster in the second loop
  for(uint8_t i = 0; i < MAX_ENEMIES; i++) {
    ENEMY *e = &enemy[i];
    if(!e->type) continue;
    e->t++;

    // Squashed or knocked out
    if(e->state == S_SQUASHED) {
      if(e->t > 20) e->type = E_NONE;
      continue;
    }
    if(e->state == S_FALLING) {
      e->vy += 4; e->y += e->vy;
      if(e->y > (72 << 4)) e->type = E_NONE;
      continue;
    }

    // Movement
    switch(e->type) {
      case E_OWL:                                 // walks, falls off edges
        EnemyMove(e, (frame & 1) | fast, 0);
        break;
      case E_BEETLE:                              // walks, turns at edges
        EnemyMove(e, 1, 1);
        break;
      case E_SPIKY:                               // slow, turns at edges
        EnemyMove(e, (frame & 1) | fast, 1);
        break;
      case E_FROG:                                // hops towards the sister
        if(e->vy == 0 && GroundAt(e->x + 4, (e->y >> 4) + 8)) {
          if(e->t > (fast ? 30 : 50)) {
            e->t = 0; e->vy = -52;
            e->dir = ((px >> 4) < e->x) ? -1 : 1;
          }
          EnemyMove(e, 0, 0);
        }
        else EnemyMove(e, 1, 0);
        break;
      case E_BEE:                                 // flies in waves
        if((frame & 1) | fast) e->x--;
        e->y += SINE[(e->t >> 1) & 31] << 1;
        break;
      case E_BOSS:                                // walks and jumps, faster when hurt
        if(bossHit) bossHit--;
        if(e->vy == 0 && !(e->t & 31)) e->dir = ((px >> 4) < e->x) ? -1 : 1;
        if(e->vy == 0 && !(e->t & 63) && e->x < camX + 112) e->vy = -48;
        EnemyMove(e, (frame & 1) | (bossHP < 4) | fast, 1);
        break;
      case E_FISH:                                // jumps out of the water
        if(e->vy == 0 && e->y >= (56 << 4)) {
          if(e->t > 45 && e->x < camX + 120) {e->vy = -72 - (JOY_random() & 15); e->t = 0;}
        }
        else {
          e->vy += 4; e->y += e->vy;
          if(e->vy > 0 && e->y >= (56 << 4)) {e->y = 56 << 4; e->vy = 0;}
        }
        break;
    }

    // Out of sight or fallen into a pit
    if(e->x < camX - 16 || e->y > (64 << 4)) {e->type = E_NONE; continue;}

    // Touching the sister: stomp or get hurt
    int16_t ey = e->y >> 4, sy = py >> 4;
    uint8_t sz = (e->type == E_BOSS) ? 16 : 8;
    if(!dying && !cleared && Collide(px >> 4, sy, 8, 8, e->x + 1, ey + 1, sz - 2, sz - 1)) {
      if(e->type == E_BOSS) {
        if(vy > 0 && sy + 7 < ey + 6) {vy = -JUMP_SPEED; BossHit(e, 2);}
        else if(!bossHit) PlayerHurt();
      }
      else if(vy > 0 && sy + 7 < ey + 5 && e->type != E_SPIKY) {
        e->state = S_SQUASHED; e->t = 0;
        vy = JOY_up_pressed() ? -JUMP_SPEED : -STOMP_SPEED;
        AddScore(POINTS[e->type]);
        JOY_sound(100, 8); JOY_sound(160, 8);
      }
      else PlayerHurt();
    }
  }
}

// ===================================================================================
// Dream Bubbles, Items and Debris
// ===================================================================================
void BallsUpdate(void) {
  for(uint8_t i = 0; i < MAX_BALLS; i++) {
    PARTICLE *b = &ball[i];
    if(!b->on) continue;
    b->x += b->dx;
    if(SolidAt(b->x + 1, (b->y >> 4) + 1) || b->x < camX - 4 || b->x > camX + 128) {b->on = 0; continue;}
    b->vy += 5;
    int16_t ny = (b->y + b->vy) >> 4;
    if(b->vy > 0 && GroundAt(b->x + 1, ny + 3)) {
      b->y = (((ny + 3) & ~7) - 4) << 4; b->vy = -36;    // bounce
    }
    else b->y += b->vy;
    if(b->y > (64 << 4)) {b->on = 0; continue;}

    // Hit an enemy
    for(uint8_t j = 0; j < MAX_ENEMIES; j++) {
      ENEMY *e = &enemy[j];
      uint8_t sz = (e->type == E_BOSS) ? 16 : 8;
      if(e->type && e->state == S_ALIVE && Collide(b->x, b->y >> 4, 4, 4, e->x, e->y >> 4, sz, sz)) {
        if(e->type == E_BOSS) BossHit(e, 1);
        else EnemyKill(e);
        b->on = 0; break;
      }
    }
  }
}

void ItemUpdate(void) {
  if(itemType == I_NONE) return;
  if(itemRise) {                                  // comes out of the box
    itemRise--; itemY -= 16;
    return;
  }
  if(itemType == I_WHEEL) {                       // the wheel rolls away
    int16_t y = itemY >> 4;
    int16_t fx = itemDir > 0 ? itemX + 8 : itemX - 1;
    if(SolidAt(fx, y + 4)) itemDir = -itemDir;
    else itemX += itemDir;
    itemVY += GRAVITY;
    if(itemVY > FALL_MAX) itemVY = FALL_MAX;
    int16_t ny = (itemY + itemVY) >> 4;
    if(itemVY > 0 && (GroundAt(itemX + 1, ny + 7) || GroundAt(itemX + 6, ny + 7))) {
      itemY = (((ny + 7) & ~7) - 8) << 4; itemVY = 0;
    }
    else itemY += itemVY;
  }
  if(itemX < camX - 8 || itemY > (64 << 4)) {itemType = I_NONE; return;}
  if(!dying && Collide(px >> 4, py >> 4, 8, 8, itemX, itemY >> 4, 8, 8)) {
    if(itemType == I_WHEEL) {power = 1; ShowMessage("PUNK POWER", 50);}
    else                    {power = 2; ShowMessage("DREAM BUBBLES", 50);}
    itemType = I_NONE;
    AddScore(1000);
    JOY_sound(150, 30); JOY_sound(200, 30); JOY_sound(250, 40);
  }
}

void DebrisUpdate(void) {
  for(uint8_t i = 0; i < MAX_DEBRIS; i++) {
    PARTICLE *d = &debris[i];
    if(!d->on) continue;
    d->x += d->dx; d->vy += 4; d->y += d->vy;
    if(d->y > (64 << 4)) d->on = 0;
  }
}

// ===================================================================================
// Rendering
// ===================================================================================

// Reverse the bit order of a byte (vertical flip)
uint8_t Flip(uint8_t b) {
  b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
  b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
  return (b & 0xAA) >> 1 | (b & 0x55) << 1;
}

// Put one byte column into the page buffer at pixel row y (screen coordinates).
// With a mask, the pixels around it are cleared, so it stands out from the background.
void DrawColumn(uint8_t page, int16_t x, int16_t y, uint8_t b, uint8_t mask) {
  int16_t row = y - (page << 3);
  if(x < 0 || x > 127 || row <= -8 || row >= 8) return;
  uint16_t m = mask ? (b | (b << 1) | (b >> 1)) : 0;
  if(row >= 0) {buf[x] &= ~(m << row); buf[x] |= b << row;}
  else         {buf[x] &= ~(m >> -row); buf[x] |= b >> -row;}
}

// Sprite with mask (screen coordinates)
void DrawSprite(uint8_t page, int16_t x, int16_t y, uint8_t w, const uint8_t *bmp, uint8_t flags) {
  int16_t row = y - (page << 3);
  if(row <= -8 || row >= 8) return;
  for(uint8_t i = 0; i < w; i++) {
    uint8_t b = bmp[(flags & F_MIRROR) ? w - 1 - i : i];
    if(flags & F_VFLIP) b = Flip(b);
    DrawColumn(page, x + i, y, b, 1);
  }
}

// 5x7 font, scale 1 or 2
void DrawText(uint8_t page, int16_t x, int16_t y, const char *s, uint8_t scale) {
  for(; *s; s++) {
    uint8_t c = 0;
    while(FONT_CHARS[c] && FONT_CHARS[c] != *s) c++;
    const uint8_t *g = &FONT[c * 5];
    for(uint8_t i = 0; i < 5; i++) {
      if(scale == 1) DrawColumn(page, x + i, y, g[i], 1);
      else {
        uint16_t w = 0;                           // double every bit
        for(uint8_t b = 0; b < 8; b++) if(g[i] & (1 << b)) w |= 3 << (b << 1);
        for(uint8_t k = 0; k < 2; k++) {
          DrawColumn(page, x + (i << 1) + k, y, w, 1);
          DrawColumn(page, x + (i << 1) + k, y + 8, w >> 8, 1);
        }
      }
    }
    x += 6 * scale;
  }
}

// Text centered on the screen
void DrawTextC(uint8_t page, int16_t y, const char *s, uint8_t scale) {
  uint8_t len = 0;
  while(s[len]) len++;
  DrawText(page, 64 - len * 3 * scale, y, s, scale);
}

// Number with a fixed count of digits: 3x5 digits (big = 0) or 5x7 font (big = 1)
void DrawNumber(uint8_t page, int16_t x, int16_t y, uint32_t n, uint8_t digits, uint8_t big) {
  char s[8];
  s[digits] = 0;
  while(digits--) {s[digits] = '0' + n % 10; n /= 10;}
  if(big) {DrawText(page, x, y, s, 1); return;}
  for(char *p = s; *p; p++, x += 4) DrawSprite(page, x, y, 3, &DIGITS[(*p - '0') * 3], 0);
}

// Landscape of one page: distant hills and the tiles
void DrawMap(uint8_t page) {
  int8_t row = page - 1;
  int16_t col = -1;
  uint8_t t = 0, cave = 0;
  for(uint8_t x = 0; x < 128; x++) {
    int16_t wx = camX + x;
    uint8_t b, k = wx & 7;
    if((wx >> 3) != col) {                        // next column: get the tile
      col = wx >> 3;
      t = Tile(col, row);
      cave = (Tile(col, 0) == T_STONE);
    }

    // Distant hills (dotted line, half scrolling speed), not in caves
    if(!(x & 1) && !cave && t == T_EMPTY) {
      int8_t hy = 50 - HILLS[((camX >> 1) + x) >> 2 & 31];
      if((hy >> 3) == row) buf[x] |= 1 << (hy & 7);
    }

    // Tile (the bumped block is drawn as a sprite)
    if(bumpT && col == bumpCol && row == bumpRow) continue;
    if(t == T_WATER) k = (wx + (frame >> 2)) & 7;   // waves
    b = TILE_GFX[(t << 3) + k];
    if((t == T_BONUS || t == T_POWER) && (frame & 16)) b = TILE_GFX[(T_USED << 3) + k] | (b & 0x81);
    buf[x] |= b;
  }
}

// Status line: score, diamonds, lives, level and time
void DrawStatus(void) {
  uint8_t i;
  DrawNumber(0, 0, 1, score, 6, 0);
  DrawSprite(0, 29, 1, 5, SPR_ICON_DIA, 0);
  DrawNumber(0, 35, 1, diamonds, 2, 0);
  DrawSprite(0, 48, 1, 5, SPR_ICON_HEAD, 0);
  DrawNumber(0, 54, 1, lives, 1, 0);
  DrawSprite(0, 63, 1, 3, &DIGITS[10 * 3], 0);  // "L" for level
  DrawNumber(0, 67, 1, level + 1, 2, 0);
  if(power) DrawSprite(0, 84, 1, 5, power == 1 ? SPR_ICON_WHEEL : SPR_ICON_ORB, 0);
  DrawSprite(0, 104, 1, 5, SPR_ICON_CLOCK, 0);
  DrawNumber(0, 110, 1, timeLeft, 3, 0);
  for(i = 0; i < 128; i += 2) buf[i] |= 0x80;   // dotted line
}

// All the moving objects of one page
void DrawObjects(uint8_t page) {
  uint8_t i;
  const uint8_t *spr;

  // Bumped block
  if(bumpT) {
    static const uint8_t BUMP[7] = {0, 1, 2, 3, 3, 2, 1};
    DrawSprite(page, (bumpCol << 3) - camX, 8 + (bumpRow << 3) - BUMP[bumpT],
               8, &TILE_GFX[Tile(bumpCol, bumpRow) << 3], 0);
  }

  // Diamond popping out of a box
  if(popT) DrawSprite(page, popX - camX, 8 + popY, 8, &TILE_GFX[T_DIAMOND << 3], 0);

  // Item
  if(itemType) DrawSprite(page, itemX - camX, 8 + (itemY >> 4), 8,
                          (itemType == I_WHEEL ? SPR_WHEEL : SPR_ORB) + ((frame & 4) << 1), 0);

  // Enemies
  for(i = 0; i < MAX_ENEMIES; i++) {
    ENEMY *e = &enemy[i];
    if(!e->type) continue;
    static const uint8_t *const SPR[] = {0, SPR_OWL, SPR_BEETLE, SPR_FROG, SPR_BEE, SPR_SPIKY, SPR_FISH, SPR_BOSS};
    uint8_t f = (e->t & 8) ? 8 : 0, flags = (e->dir > 0) ? F_MIRROR : 0;
    spr = SPR[e->type] + f;
    if(e->type == E_FROG) spr = SPR_FROG + (e->vy ? 8 : 0);
    if(e->type == E_FISH) {spr = SPR_FISH + ((frame & 4) << 1); flags = (e->vy > 0) ? F_VFLIP : 0;}
    if(e->state == S_SQUASHED) {spr = SPR_SQUASH; flags = 0;}
    if(e->state == S_FALLING) flags |= F_VFLIP;
    if(e->type == E_BOSS) {                       // 16x16 pixels, blinks when hit
      if(bossHit & 2) continue;
      spr = SPR_BOSS + ((e->t & 8) << 2);
      uint8_t v = (flags & F_VFLIP) ? 16 : 0;
      DrawSprite(page, e->x - camX, 8 + (e->y >> 4), 16, spr + v, flags);
      DrawSprite(page, e->x - camX, 16 + (e->y >> 4), 16, spr + 16 - v, flags);
      continue;
    }
    DrawSprite(page, e->x - camX, 8 + (e->y >> 4), 8, spr, flags);
  }

  // Dream bubbles and debris
  for(i = 0; i < MAX_BALLS; i++)
    if(ball[i].on) DrawSprite(page, ball[i].x - camX, 8 + (ball[i].y >> 4), 4, SPR_BALL, 0);
  for(i = 0; i < MAX_DEBRIS; i++)
    if(debris[i].on) DrawSprite(page, debris[i].x - camX, 8 + (debris[i].y >> 4), 3, SPR_DEBRIS, 0);

  // The sister (blinks while invulnerable)
  if(!(invuln & 2) && !(cleared && walkAnim > 24)) {
    uint8_t f = 0;
    if(dying || !onGround) f = 2;
    else if(vx) f = (walkAnim >> 2) & 1;
    spr = (power ? SPR_PUNK : SPR_GIRL) + (f << 3);
    DrawSprite(page, (px >> 4) - camX, 8 + (py >> 4), 8, spr, facing | (dying ? F_VFLIP : 0));
  }
}

// Draw the whole screen page by page and send it to the OLED
void Tiny_Flip(uint8_t mode) {
  uint8_t page, i;
  for(page = 0; page < 8; page++) {
    for(i = 0; i < 128; i++) buf[i] = 0;

    switch(mode) {
      case R_TITLE:
        if(page > 3) DrawMap(page);
        if(page == 5 || page == 6)                // black band for the text
          for(i = 28; i < 100; i++) buf[i] &= (page == 5) ? 0x1F : 0x80;
        DrawText(page, 31, 0, "HI", 1);
        DrawNumber(page, 49, 0, hiscore, 7, 1);
        DrawTextC(page, 12, "TINY", 2);
        DrawTextC(page, 29, "SISTERS", 2);
        if(frame & 16) DrawTextC(page, 47, "PRESS FIRE", 1);
        break;

      case R_INTRO:
        if(loop) DrawTextC(page, 8, "ROUND 2", 1);
        DrawText(page, 37, 20, "LEVEL", 1);
        DrawNumber(page, 73, 20, level + 1, 2, 1);
        DrawSprite(page, 47, 37, 8, SPR_GIRL + ((frame & 8) ? 8 : 0), 0);
        DrawText(page, 61, 37, "X", 1);
        DrawNumber(page, 73, 37, lives, 1, 1);
        if(cpReached) DrawTextC(page, 54, "CHECKPOINT", 1);
        break;

      case R_GAMEOVER:
        DrawTextC(page, 16, "GAME OVER", 2);
        if(frame & 16) {
          DrawText(page, 25, 42, "SCORE", 1);
          DrawNumber(page, 61, 42, score, 7, 1);
        }
        break;

      case R_WIN:
        DrawTextC(page, 8, "WELL DONE!", 2);
        DrawTextC(page, 30, "ALL LEVELS CLEARED", 1);
        DrawSprite(page, 52, 46, 8, SPR_GIRL + ((frame & 8) ? 8 : 0), 0);
        DrawSprite(page, 68, 46, 8, SPR_PUNK + ((frame & 8) ? 8 : 0), F_MIRROR);
        break;

      default:
        if(page == 0) DrawStatus();
        else {
          DrawMap(page);
          DrawObjects(page);
          if(msgTimer && (msgTimer > 20 || (msgTimer & 4))) DrawTextC(page, 22, msgText, 1);
        }
        break;
    }

    // Send page to OLED
    JOY_OLED_data_start(page);
    for(i = 0; i < 128; i++) JOY_OLED_send(buf[i]);
    JOY_OLED_end();
  }
}
