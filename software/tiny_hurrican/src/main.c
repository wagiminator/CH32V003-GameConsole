// ===================================================================================
// Project:   Tiny Hurrican - Run'n'Gun Game for CH32V003
// Version:   v1.0
// Year:      2026
// Github:    https://github.com/wagiminator
// EasyEDA:   https://easyeda.com/wagiminator
// License:   http://creativecommons.org/licenses/by-sa/3.0/
// ===================================================================================
//
// Description:
// ------------
// A small Turrican-like run'n'gun game for the CH32V003 Mini Game Console. Explore
// six large levels in three worlds - caves, a technical base and an alien hive - that
// scroll in all directions. Fight walkers, hoppers, soldiers, flyers, droppers, homing
// eyes, turrets, generators and giant walkers, and defeat the guardian at the end of
// each world. Shoot the power blocks to get weapons and extras: multiple shot, laser
// and rebound (each up to level 3), energy, shield, extra life, power lines and
// gyroscope wheels. Invisible blocks hide in the air, breakable walls and narrow
// tunnels lead to secret rooms full of diamonds. Hold the fire button to use the
// lightning beam, roll through tunnels as a gyroscope wheel and release the power
// lines to clear the screen. Reach the exit before the time runs out.
//
// Controls:
// ---------
// Left/Right:         walk (turn the lightning beam)
// Up:                 jump (hold longer to jump higher)
// Down:               gyroscope wheel (on solid ground), drop through a platform
// Fire (tap):         shoot the current weapon, drop a mine (as a wheel)
// Fire (hold):        lightning beam (on the ground, rotate with up/down),
//                     continuous fire (in the air)
// Down + Fire in air: release a power line
//
// Scoring:
// --------
// Diamond 50, enemies 100 per hit point, power block 200, breakable wall 50,
// item 500, guardian 5000 x world, time bonus 10 per second.
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
#define LIVES         3       // lives at game start
#define ENERGY_MAX    20      // energy of the hero
#define TIME_LIMIT    400     // seconds per level
#define WHEEL_TIME    240     // duration of the gyroscope wheel (frames)
#define SHIELD_TIME   250     // duration of the shield (frames)
#define BEAM_LEN      44      // length of the lightning beam (pixels)
#define MAX_ENEMIES   8       // enemies at the same time
#define MAX_SHOTS     10      // hero shots and mines
#define MAX_BULLETS   6       // enemy bullets
#define MAX_ITEMS     3       // items at the same time
#define MAX_FX        6       // explosions
#define NOINLINE      __attribute__((noinline))

// Physics (speeds in 1/16 pixels per frame)
#define WALK_ACC      3       // acceleration
#define WALK_MAX      22      // top speed walking
#define WHEEL_MAX     36      // top speed of the wheel
#define JUMP_SPEED    54      // initial jump speed
#define GRAVITY       6       // gravity
#define GRAVITY_HOLD  3       // gravity while the jump button is held
#define FALL_MAX      64      // terminal velocity

// Tiles
enum {T_EMPTY, T_ROCK, T_METAL, T_PLAT, T_SPIKE, T_DIAMOND, T_PBLOCK, T_BREAK,
      T_DECO, T_EXIT, T_HIDDEN, T_EGROUND, T_EAIR, T_ETURRET, T_EGEN, T_SPIKED};
#define SOLID_MASK    0x00C6  // rock, metal, power block, breakable wall
#define SPECIAL_MASK  0x7CE0  // tiles that can be removed (diamonds, blocks, enemies)
#define IS_SOLID(t)   ((SOLID_MASK >> (t)) & 1)

// Enemies, weapons, shots, items, render modes, sprite flags
enum {E_NONE, E_WALKER, E_HOPPER, E_SOLDIER, E_FLYER, E_DROPPER, E_HOMER, E_TURRET, E_GEN, E_BIG};
enum {W_MULTI, W_LASER, W_REBOUND};
enum {S_MULTI, S_LASER, S_REBOUND, S_MINE};
enum {I_NONE, I_MULTI, I_LASER, I_REBOUND, I_ENERGY, I_SHIELD, I_LIFE, I_LINE, I_WHEEL};
enum {R_TITLE, R_GAME, R_INTRO, R_GAMEOVER, R_WIN};
#define F_MIRROR      1

// ===================================================================================
// Game Objects
// ===================================================================================
typedef struct {
  int     x;                  // position in pixels
  int     y;                  // position in 1/16 pixels
  int     vy;                 // vertical speed
  int      dir;                // direction (-1, +1)
  int     type;               // E_NONE: free slot
  int      hp;                 // hit points
  int     t;                  // timer
  int     state;
  int     flash;              // hit cooldown
  int     hc, hr;             // tile of the marker in the map (0xFF: none)
} ENEMY;

typedef struct {
  int     x, y;               // position in 1/16 pixels
  int      dx, dy;             // speed
  int     type;
  int     life;               // 0: free slot
} SHOT;

typedef struct {
  int     x;                  // position in pixels
  int     y;                  // position in 1/16 pixels
  int     vy;
  int     type;               // I_NONE: free slot
  int     t;
} ITEM;

typedef struct {
  int     x, y;               // position in pixels
  int     t;                  // 0: free slot
} EFFECT;

ENEMY    enemy[MAX_ENEMIES];
SHOT     shot[MAX_SHOTS];
SHOT     bullet[MAX_BULLETS];
ITEM     item[MAX_ITEMS];
EFFECT   fx[MAX_FX];
uint8_t  buf[128];                  // one display page
uint8_t  gone[64];                  // removed tiles of the cells (one bit per special tile)

// Map
int      mapW, mapH, theme, gCell, gBit, exitLocked;
int      gridBase, foes;
int      camX, camY, mapPW, mapPH;
int      scanIdx, hitCnt, kills;
int      hitCol, hitRow;

// Hero
int      px, py, vx, vy;            // position and speed in 1/16 pixels
int      safeX, safeY;              // respawn position
int      onGround, coyote, dropT, jumpLatch, fireLatch, fireHeld, facing, walkAnim;
int      wheel, invuln, shield, dying, cleared, beamOn, beamAng, beamLen;
int      energy, weapon, wlevel, lines, wheels, diamonds, lives;
int      lineT, lastPad;
int      lineX1, lineX2;

// Game progress
int      level, loop, gameOver, frame, timeTick, endTimer;
int      timeLeft;
uint32_t score, hiscore;

// Guardian
int      bossType, bossOn, bossState, bossHP, bossT, bossFlash;
int      bossDir;
int      bossX, bossY, bossVY, arenaX, arenaY;   // bossY in 1/16 pixels

// Message in the middle of the screen
const char *msgText;
int      msgTimer;

// Tables
const int8_t SINE[32] = {
  0, 1, 2, 3, 4, 5, 5, 6, 6, 6, 5, 5, 4, 3, 2, 1,
  0, -1, -2, -3, -4, -5, -5, -6, -6, -6, -5, -5, -4, -3, -2, -1
};
const int8_t  BEAM_DX[7]  = {0, 4, 7, 8, 7, 4, 0};       // lightning beam directions
const int8_t  BEAM_DY[7]  = {-8, -7, -4, 0, 4, 7, 8};
const int8_t  MULTI_DY[5] = {0, -12, 12, -24, 24};       // spread of the multiple shot
const int8_t  REB_DX[6]   = {56, 48, 48, 56, 44, 44};    // rebound shots per level
const int8_t  REB_DY[6]   = {0, -32, 32, 0, -36, 36};
const uint8_t REB_FIRST[3] = {0, 1, 3};
const uint8_t REB_N[3]    = {1, 2, 3};
const int8_t  EHP[]       = {0, 2, 2, 3, 1, 2, 3, 5, 8, 14};
const uint8_t ITEM_CYCLE[16] = {
  I_MULTI, I_ENERGY, I_LASER, I_WHEEL, I_REBOUND, I_SHIELD, I_MULTI, I_LINE,
  I_LASER, I_ENERGY, I_REBOUND, I_WHEEL, I_MULTI, I_SHIELD, I_LASER, I_LINE
};
const uint8_t HIDDEN_ITEMS[4] = {I_LIFE, I_LINE, I_WHEEL, I_LIFE};
const char *const ITEM_NAMES[] = {"", "MULTIPLE", "LASER", "REBOUND", "ENERGY", "SHIELD",
                                  "EXTRA LIFE", "POWER LINE", "GYRO WHEEL"};
const uint8_t *const ESPR[] = {0, SPR_WALKER, SPR_HOPPER, SPR_SOLDIER, SPR_FLYER,
                               SPR_DROPPER, SPR_HOMER, SPR_TURRET, SPR_GEN, SPR_BIG};

// ===================================================================================
// Function Prototypes
// ===================================================================================
void TitleScreen(void);
void IntroScreen(void);
void GameOverScreen(void);
void WinScreen(void);
void NewGame(void);
void LoadLevel(void);
void StartLevel(void);
void PlayLevel(void);
void UpdateGame(void);
void Tiny_Flip(int mode);
void PlayerUpdate(void);
void PlayerDie(void);
void Hurt(int n);
void EnemiesUpdate(void);
void ShotsUpdate(void);
void BulletsUpdate(void);
void ItemsUpdate(void);
void BossUpdate(void);
void SpawnScan(void);
void FxSpawn(int x, int y);
void ItemSpawn(int x, int y, int type);
void EnemyHit(ENEMY *e, int dmg, int pierce);
void BossHit(int dmg, int pierce);
ENEMY *EnemyAdd(int type, int x, int y);
void BlockHit(int col, int row, int t);
void ShowMessage(const char *text, int time);

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
        if(++level >= LEVELS) {                     // all levels done: next round
          WinScreen();
          level = 0; loop++;
        }
      }
    }
    GameOverScreen();
  }
}

// ===================================================================================
// Helpers
// ===================================================================================
NOINLINE void Beep(int freq, int dur) {
  JOY_sound(freq, dur);
}

// Keep a steady frame rate
void WaitFrame(uint32_t start) {
  while((int32_t)(STK->CNT - start) < FRAME_TIME);
}

// Bounding box collision
NOINLINE int Collide(int x1, int y1, int w1, int h1, int x2, int y2, int w2, int h2) {
  return (x1 < x2 + w2) && (x2 < x1 + w1) && (y1 < y2 + h2) && (y2 < y1 + h1);
}

void AddScore(uint16_t points) {
  score += points;
}

void ShowMessage(const char *text, int time) {
  msgText = text; msgTimer = time;
}

// ===================================================================================
// Screens
// ===================================================================================

// Play a melody (zero terminated)
NOINLINE void Melody(const uint8_t *m, int dur) {
  for(; *m; m++) Beep(*m, dur);
}

// Show a screen for a number of frames (0: until the fire button is pressed)
NOINLINE void Screen(int mode, int frames) {
  while(JOY_act_pressed());                       // wait until fire button released
  for(frame = 0; ; frame++) {
    uint32_t start = STK->CNT;
    if(mode == R_TITLE && (frame & 1) && ++camX > mapPW - 128) camX = 0;
    Tiny_Flip(mode);
    JOY_random();                                 // stir the random generator
    WaitFrame(start);
    if(frames ? frame >= frames : (frame > 30 && JOY_act_pressed())) return;
  }
}

// Title screen with the scrolling first level
const uint8_t START_TUNE[] = {120, 160, 200, 240, 0};
void TitleScreen(void) {
  level = 0;
  LoadLevel();
  camY = 72;
  Screen(R_TITLE, 0);
  Melody(START_TUNE, 50);
}

// World number, lives and score
void IntroScreen(void) {
  Screen(R_INTRO, 75);
}

const uint8_t OVER_TUNE[] = {120, 90, 60, 0};
void GameOverScreen(void) {
  if(score > hiscore) hiscore = score;
  Melody(OVER_TUNE, 120);
  Screen(R_GAMEOVER, 0);
}

const uint8_t WIN_TUNE[] = {150, 170, 190, 200, 190, 200, 230, 0};
void WinScreen(void) {
  Melody(WIN_TUNE, 120);
  Screen(R_WIN, 180);
}

// ===================================================================================
// Game Control
// ===================================================================================
void NewGame(void) {
  score = 0; lives = LIVES; diamonds = 0;
  weapon = W_MULTI; wlevel = 0; lines = 3; wheels = 3;
  level = 0; loop = 0; gameOver = 0;
}

// Prepare the map of the current level
void LoadLevel(void) {
  int i;
  mapW = LEVEL_W[level]; mapH = LEVEL_H[level];
  mapPW = mapW << 6; mapPH = mapH << 6;
  gridBase = LEVEL_GRID[level]; foes = LEVEL_FOES[level];
  theme = level >> 1;
  for(i = 0; i < 64; i++) gone[i] = 0;
  bossType = LEVEL_BOSS[level]; bossOn = 0;
  arenaX = LEVEL_AX[level] << 6; arenaY = LEVEL_AY[level] << 6;
  exitLocked = bossType ? 1 : 0;
  camX = 0;
}

// Set up the current level
void StartLevel(void) {
  int i;
  LoadLevel();
  for(i = 0; i < MAX_ENEMIES; i++) enemy[i].type = E_NONE;
  for(i = 0; i < MAX_SHOTS; i++)   shot[i].life = 0;
  for(i = 0; i < MAX_BULLETS; i++) bullet[i].life = 0;
  for(i = 0; i < MAX_ITEMS; i++)   item[i].type = I_NONE;
  for(i = 0; i < MAX_FX; i++)      fx[i].t = 0;
  msgTimer = 0; lineT = 0; hitCnt = 0;
  safeX = (LEVEL_SX[level] << 3) << 4;
  safeY = ((LEVEL_SY[level] << 3) - 8) << 4;
  energy = ENERGY_MAX; invuln = 60;
  timeLeft = TIME_LIMIT; timeTick = 0;
  cleared = 0; endTimer = 0;
  frame = 0;
  px = safeX; py = safeY;
  vx = vy = 0; facing = 0; onGround = 0;
  wheel = 0; shield = 0; dying = 0; beamOn = 0;
  jumpLatch = 1; fireLatch = 1; fireHeld = 0;
  camX = (px >> 4) - 60; camY = (py >> 4) - 24;
}

// Run the level until it is cleared or the game is over
void PlayLevel(void) {
  static const uint8_t TUNE[] = {100, 150, 200, 150, 200, 240, 0};
  Melody(TUNE, 50);
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
  int i;
  frame++;

  // Time
  if(!dying && !cleared && ++timeTick >= 30) {
    timeTick = 0;
    if(timeLeft) timeLeft--;
    if(timeLeft && timeLeft <= 30) Beep(220, 10);
    if(!timeLeft) {ShowMessage("TIME UP", 60); PlayerDie();}
  }

  // Level cleared: count the remaining time as bonus
  if(cleared) {
    if(timeLeft) {
      int n = timeLeft > 4 ? 5 : timeLeft;
      timeLeft -= n; AddScore(n * 10);
      Beep(200 + (frame & 31), 4);
      if(!timeLeft) endTimer = 45;
    }
  }

  PlayerUpdate();
  SpawnScan();
  EnemiesUpdate();
  BossUpdate();
  ShotsUpdate();
  BulletsUpdate();
  ItemsUpdate();
  for(i = 0; i < MAX_FX; i++) if(fx[i].t && ++fx[i].t > 9) fx[i].t = 0;
  if(msgTimer) msgTimer--;
}

// ===================================================================================
// Map
// ===================================================================================

// Tile at map position. raw = 0: as seen and touched by the hero (markers are empty).
// For special tiles gCell and gBit are set (the bit in the gone[] list).
NOINLINE int TileGet(int col, int row, int raw) {
  if(col < 0 || row < 0 || col >= (mapW << 3) || row >= (mapH << 3)) return T_ROCK;
  int cell = (row >> 3) * mapW + (col >> 3);
  int g = GRID[gridBase + cell];
  const uint8_t *blk = &BLOCKS[(g & 0x7F) << 5];
  int c = col & 7;
  if(g & 0x80) c = 7 - c;
  int pos = ((row & 7) << 3) | c;
  int t = (pos & 1) ? blk[pos >> 1] >> 4 : blk[pos >> 1] & 15;
  if(!raw && t >= T_HIDDEN && t <= T_EGEN) return T_EMPTY;
  if((SPECIAL_MASK >> t) & 1) {
    int n = 0;
    for(int i = 0; i < pos; i++) {
      int s = (i & 1) ? blk[i >> 1] >> 4 : blk[i >> 1] & 15;
      if((SPECIAL_MASK >> s) & 1) n++;
    }
    gCell = cell; gBit = 1 << n;
    if(gone[cell] & gBit) return T_EMPTY;
  }
  if(t == T_EXIT && exitLocked) return T_EMPTY;
  return t;
}

#define Tile(c, r)    TileGet(c, r, 0)

// Remove a special tile (collected, destroyed or spawned)
NOINLINE void Remove(int col, int row) {
  TileGet(col, row, 1);
  gone[gCell] |= gBit;
}

// Tile at pixel position
NOINLINE int TileAt(int x, int y) {
  return Tile(x >> 3, y >> 3);
}

NOINLINE int SolidAt(int x, int y) {
  return IS_SOLID(TileAt(x, y));
}

// Ground for enemies and items: solid tile or the upper half of a platform
NOINLINE int GroundAt(int x, int y) {
  int t = TileAt(x, y);
  return IS_SOLID(t) || (t == T_PLAT && !(y & 4));
}

// Scan two columns around the screen per frame for enemy markers
void SpawnScan(void) {
  for(int k = 0; k < 2; k++) {
    int c = (camX >> 3) - 2 + scanIdx;
    if(++scanIdx > 20) scanIdx = 0;
    int r0 = (camY >> 3) - 1;
    for(int r = r0; r < r0 + 10; r++) {
      int t = TileGet(c, r, 1);
      if(t < T_EGROUND || t > T_EGEN) continue;
      int sel = ((c ^ r) & 1) << 2, type;
      if(t == T_EGROUND)     type = (foes >> sel) & 15;
      else if(t == T_EAIR)   type = (foes >> (8 + sel)) & 15;
      else if(t == T_ETURRET) type = E_TURRET;
      else                   type = E_GEN;
      ENEMY *e = EnemyAdd(type, c << 3, (r << 3) - ((type == E_BIG) ? 8 : 0));
      if(e) {gone[gCell] |= gBit; e->hc = c; e->hr = r;}
    }
  }
}

// ===================================================================================
// Hero
// ===================================================================================

// Is there room for the hero standing upright at pixel x, top y?
NOINLINE int Headroom(int x, int y) {
  for(int i = 0; i < 16; i += 7)
    if(SolidAt(x + 1, y + i) || SolidAt(x + 6, y + i)) return 0;
  return 1;
}

// Hit the enemies and the guardian in a box. mode: 1 = only every few frames (beam,
// laser), 2 = only the first one, 4 = only test. Returns 1 if something was hit.
NOINLINE int HitArea(int x, int y, int w, int h, int dmg, int mode) {
  int hit = 0;
  for(int i = 0; i < MAX_ENEMIES; i++) {
    ENEMY *e = &enemy[i];
    int sz = (e->type == E_BIG) ? 16 : 8;
    if(e->type && Collide(x, y, w, h, e->x, e->y >> 4, sz, sz)) {
      if(mode & 4) return 1;
      hit = 1;
      EnemyHit(e, dmg, mode & 1);
      if(mode & 2) return 1;
    }
  }
  if(bossOn == 1 && Collide(x, y, w, h, bossX, bossY >> 4, 24, 24)) {
    if(!(mode & 4)) BossHit(dmg, mode & 1);
    hit = 1;
  }
  return hit;
}

// Shot patterns: multiple shot (levels 1-3), laser, rebound (levels 1-3)
const int8_t  PAT_DX[] = {72, 72, 72, 72, 72, 96, 56, 48, 48, 56, 44, 44};
const int8_t  PAT_DY[] = {0, -12, 12, -24, 24, 0, 0, -32, 32, 0, -36, 36};
const uint8_t PAT_FIRST[9] = {0, 0, 0, 5, 5, 5, 6, 7, 9};
const uint8_t PAT_N[9]     = {1, 3, 5, 1, 1, 1, 1, 2, 3};
const uint8_t PAT_LIFE[4]  = {20, 24, 45, 90};
#define LASER_W       (6 + (wlevel << 2))

// Spawn a shot or a mine (pixel position)
NOINLINE void ShotSpawn(int x, int y, int dx, int dy, int type) {
  for(int i = 0; i < MAX_SHOTS; i++) {
    SHOT *p = &shot[i];
    if(p->life) continue;
    p->x = x << 4; p->y = y << 4; p->dx = dx; p->dy = dy;
    p->type = type; p->life = PAT_LIFE[type];
    return;
  }
}

// Read the direction buttons once: bit 0 up, 1 down, 2 left, 3 right
const uint16_t PAD_VAL[8]  = {JOY_N, JOY_NE, JOY_E, JOY_SE, JOY_S, JOY_SW, JOY_W, JOY_NW};
const uint8_t  PAD_BITS[8] = {1, 9, 8, 10, 2, 6, 4, 5};
NOINLINE int ReadPad(void) {
  int v = ADC_read();
  for(int i = 0; i < 8; i++)
    if(v > PAD_VAL[i] - JOY_DEV && v < PAD_VAL[i] + JOY_DEV) return PAD_BITS[i];
  return 0;
}

// Shoot the current weapon
void Fire(void) {
  int k = weapon * 3 + wlevel, n = PAT_N[k], f = PAT_FIRST[k];
  int gx = (px >> 4) + (facing ? -2 : 8);
  if(weapon == W_LASER && facing) gx -= LASER_W;
  while(n--) {
    ShotSpawn(gx, (py >> 4) + 6, facing ? -PAT_DX[f] : PAT_DX[f], PAT_DY[f], weapon);
    f++;
  }
  Beep(weapon == W_LASER ? 240 : 200, 3);
}

void PlayerUpdate(void) {
  int x, y, i, t, t2;

  // Dying: wait, then respawn at the last safe position
  if(dying) {
    if(++dying == 50) {
      if(--lives == 0) {gameOver = 1; endTimer = 1; return;}
      px = safeX; py = safeY; vx = vy = 0;
      energy = ENERGY_MAX; invuln = 90; dying = 0; wlevel = 0;
      if(!timeLeft) timeLeft = TIME_LIMIT;
      for(i = 0; i < MAX_BULLETS; i++) bullet[i].life = 0;
    }
    return;
  }
  if(cleared) return;

  // DOWN only counts if it is read in two frames in a row: while another button is
  // pressed, the voltage of the button ladder passes the DOWN value for a moment
  int pad = ReadPad(), fire = JOY_act_pressed();
  int up = pad & 1, down = pad & lastPad & 2, left = pad & 4, right = pad & 8, hold = up;
  lastPad = pad;
  int press = fire && !fireLatch;
  fireLatch = fire;
  if(!fire) fireHeld = 0;
  else if(fireHeld < 250) fireHeld++;
  x = px >> 4; y = py >> 4;

  // Lightning beam: hold the fire button on the ground, rotate it with up/down
  beamOn = 0;
  if(fireHeld > 8 && onGround && !wheel) {
    beamOn = 1;
    if(fireHeld == 9) beamAng = 3;
    if(left) facing = F_MIRROR;
    if(right) facing = 0;
    if(!(frame & 3)) {
      if(up && beamAng) beamAng--;
      if(down && beamAng < 6) beamAng++;
      Beep(30 + (frame & 12), 2);
    }
    left = right = up = down = hold = 0; vx = 0;
    int gx = facing ? x - 1 : x + 8, gy = y + 6;
    int dx = facing ? -BEAM_DX[beamAng] : BEAM_DX[beamAng], dy = BEAM_DY[beamAng];
    for(beamLen = 0; beamLen < BEAM_LEN; beamLen += 2) {
      int bx = gx + ((dx * beamLen) >> 3), by = gy + ((dy * beamLen) >> 3);
      t = TileAt(bx, by);
      if(IS_SOLID(t)) {
        if(!(frame & 7) && (t == T_PBLOCK || t == T_BREAK)) BlockHit(bx >> 3, by >> 3, t);
        break;
      }
      if(!(frame & 3) && !(beamLen & 3)) HitArea(bx - 1, by - 1, 3, 3, 1, 1);
    }
  }

  // Walk (the wheel is faster)
  int maxv = wheel ? WHEEL_MAX : WALK_MAX;
  if(left || right) {
    facing = left ? F_MIRROR : 0;
    vx += left ? -WALK_ACC : WALK_ACC;
    if(vx > maxv) vx = maxv;
    if(vx < -maxv) vx = -maxv;
  }
  else if(vx > 2) vx -= 2;
  else if(vx < -2) vx += 2;
  else vx = 0;

  // Horizontal movement and walls (the wheel is only 8 pixels high)
  int top = wheel ? 8 : 0;
  px += vx;
  x = px >> 4;
  if(vx) {
    int sx = (vx > 0) ? x + 6 : x + 1;
    for(i = top; i < 16; i += 7) if(SolidAt(sx, y + i)) {
      px = ((sx & ~7) + ((vx > 0) ? -7 : 7)) << 4; vx = 0; break;
    }
    x = px >> 4;
  }

  // Standing on a platform?
  t = TileAt(x + 1, y + 16); t2 = TileAt(x + 6, y + 16);
  int onPlat = onGround && !IS_SOLID(t) && !IS_SOLID(t2);

  // Power line: down + fire in the air
  if(press && down && !onGround && lines && !lineT && !wheel) {
    lines--; lineT = 1; lineX1 = lineX2 = x + 4; press = 0;
    Beep(250, 20); Beep(200, 20); Beep(150, 20);
  }

  // Gyroscope wheel and dropping through platforms
  if(down && onGround && !wheel) {
    if(onPlat) {dropT = 8; onGround = 0;}
    else if(wheels && !fire) {wheel = WHEEL_TIME; wheels--; Beep(100, 10); Beep(160, 10);}
  }
  if(wheel) {
    if(--wheel == 0 || up) {
      if(Headroom(x, y)) {wheel = 0; Beep(160, 10);}
      else if(!wheel) wheel = 1;
    }
    up = hold = 0;
    if(press) ShotSpawn(x + 2, y + 12, 0, 0, S_MINE);
  }
  else if(press || (fireHeld > 8 && !onGround && !(fireHeld & 7))) Fire();

  // Jump (up button), higher while held
  if(!up) jumpLatch = 0;
  if(up && !jumpLatch && (onGround || coyote)) {
    vy = -JUMP_SPEED; jumpLatch = 1; onGround = 0; coyote = 0;
    Beep(180, 4);
  }
  if(dropT) dropT--;
  if(onPlat && t != T_PLAT && t2 != T_PLAT) {onGround = 0; coyote = 4;}

  // Falling or jumping: gravity, ground, platforms and ceilings
  if(!onGround) {
    vy += (hold && vy < 0) ? GRAVITY_HOLD : GRAVITY;
    if(vy > FALL_MAX) vy = FALL_MAX;
    if(coyote) coyote--;
    int ny = (py + vy) >> 4;
    if(vy > 0) {
      int fy = ny + 15, land = 0;
      for(i = x + 1; i <= x + 6; i += 5) {
        t = TileAt(i, fy);
        if(IS_SOLID(t) || (t == T_PLAT && !dropT && y + 15 < (fy & ~7))) land = 1;
      }
      if(land) {py = ((fy & ~7) - 16) << 4; vy = 0; onGround = 1;}
      else py += vy;
    }
    else if(SolidAt(x + 1, ny + top) || SolidAt(x + 6, ny + top)) {
      py = (((ny + top) & ~7) + 8 - top) << 4; vy = 0;
    }
    else py += vy;
  }
  if(py < 0) py = 0;

  // Animation and timers
  if(vx) walkAnim++;
  if(invuln) invuln--;
  if(shield) shield--;

  // Tiles touched: diamonds, spikes, exit
  x = px >> 4; y = py >> 4;
  for(i = 0; i < 4; i++) {
    int cx = x + ((i & 1) ? 5 : 2), cy = y + ((i & 2) ? 13 : top + 2);
    t = TileAt(cx, cy);
    if(t == T_DIAMOND) {
      Remove(cx >> 3, cy >> 3);
      AddScore(50);
      Beep(230, 4); Beep(250, 6);
      if(++diamonds >= 100) {
        diamonds = 0;
        if(lives < 9) lives++;
        ShowMessage("EXTRA LIFE", 60);
      }
    }
    else if(t == T_SPIKE || t == T_SPIKED) {
      if(!invuln && !shield && !wheel) {Hurt(5); vy = -40; onGround = 0;}
    }
    else if(t == T_EXIT && !cleared) {
      cleared = 1; beamOn = 0;
      Beep(150, 60); Beep(200, 60); Beep(250, 120);
      ShowMessage("STAGE CLEAR", 120);
    }
  }

  // Remember a safe position to respawn
  if(onGround && !onPlat && !wheel && !invuln && !(frame & 15) && Headroom(x, y)) {safeX = px; safeY = py;}

  // Camera follows the hero
  x -= camX;
  if(x > 72) camX += x - 72;
  if(x < 48) camX -= 48 - x;
  if(camX > mapPW - 128) camX = mapPW - 128;
  if(camX < 0) camX = 0;
  y = ((bossOn == 1) ? arenaY + 26 : y) - camY - 22;   // the hero (or the arena) in the middle
  if(y > 2)  camY += (y > 14) ? 4 : 1;
  if(y < -2) camY -= (y < -14) ? 4 : 1;
  if(camY > mapPH - 56) camY = mapPH - 56;
  if(camY < 0) camY = 0;

  // Power lines: two walls of energy move to the left and to the right
  if(lineT) {
    lineT++; lineX1 -= 5; lineX2 += 5;
    HitArea(lineX1 - 2, camY - 16, 5, 96, 8, 1);
    HitArea(lineX2 - 2, camY - 16, 5, 96, 8, 1);
    for(i = 0; i < MAX_BULLETS; i++) bullet[i].life = 0;
    if(lineT > 30) lineT = 0;
  }
}

// Damage to the hero
void Hurt(int n) {
  if(invuln || shield || wheel || dying || cleared) return;
  if(energy > n) {
    energy -= n; invuln = 40;
    Beep(120, 15); Beep(80, 20);
  }
  else {energy = 0; PlayerDie();}
}

void PlayerDie(void) {
  if(dying || cleared) return;
  dying = 1; beamOn = 0; wheel = 0; vx = vy = 0;
  FxSpawn((px >> 4), (py >> 4)); FxSpawn((px >> 4), (py >> 4) + 8);
  Beep(200, 40); Beep(150, 40); Beep(100, 60); Beep(60, 100);
}

// ===================================================================================
// Effects and Items
// ===================================================================================
void FxSpawn(int x, int y) {
  static int next;
  for(int i = 0; i < MAX_FX; i++) if(!fx[i].t) {next = i; break;}
  fx[next].x = x; fx[next].y = y; fx[next].t = 1;
  if(++next >= MAX_FX) next = 0;
}

void ItemSpawn(int x, int y, int type) {
  int i;
  for(i = 0; i < MAX_ITEMS - 1; i++) if(!item[i].type) break;
  item[i].x = x; item[i].y = y << 4; item[i].vy = -40; item[i].type = type; item[i].t = 0;
}

void ItemsUpdate(void) {
  for(int i = 0; i < MAX_ITEMS; i++) {
    ITEM *p = &item[i];
    if(!p->type) continue;
    if(++p->t > 250) {p->type = I_NONE; continue;}
    p->vy += 4;
    if(p->vy > FALL_MAX) p->vy = FALL_MAX;
    int ny = (p->y + p->vy) >> 4;
    if(p->vy > 0 && (GroundAt(p->x + 1, ny + 7) || GroundAt(p->x + 6, ny + 7))) {
      p->y = (((ny + 7) & ~7) - 8) << 4; p->vy = 0;
    }
    else if(p->vy < 0 && SolidAt(p->x + 4, ny)) p->vy = 0;
    else p->y += p->vy;
    if(!dying && Collide((px >> 4) + 1, py >> 4, 6, 16, p->x, p->y >> 4, 8, 8)) {
      int k = p->type;
      if(k <= I_REBOUND) {
        if(weapon == k - I_MULTI) {if(wlevel < 2) wlevel++;}
        else weapon = k - I_MULTI;
      }
      else if(k == I_ENERGY) energy = ENERGY_MAX;
      else if(k == I_SHIELD) shield = SHIELD_TIME;
      else if(k == I_LIFE)   {if(lives < 9) lives++;}
      else if(k == I_LINE)   {if(lines < 9) lines++;}
      else                   {if(wheels < 9) wheels++;}
      ShowMessage(ITEM_NAMES[k], 45);
      AddScore(500);
      p->type = I_NONE;
      Beep(150, 20); Beep(200, 20); Beep(250, 30);
    }
  }
}

// A shot hits a power block or a breakable wall
void BlockHit(int col, int row, int t) {
  if(col != hitCol || row != hitRow) {hitCol = col; hitRow = row; hitCnt = 0;}
  Beep(60, 3);
  if(++hitCnt < (t == T_PBLOCK ? 4 : 3)) return;
  Remove(col, row);
  hitCnt = 0;
  FxSpawn(col << 3, row << 3);
  if(t == T_PBLOCK) {
    ItemSpawn(col << 3, row << 3, ITEM_CYCLE[(col * 3 + row * 5 + level) & 15]);
    AddScore(200);
  }
  else AddScore(50);
  for(int i = 0; i < 6; i++) Beep((JOY_random() & 0x3F) + 30, 3);
}

// ===================================================================================
// Enemies
// ===================================================================================

// Add an enemy (pixel position)
NOINLINE ENEMY *EnemyAdd(int type, int x, int y) {
  for(int i = 0; i < MAX_ENEMIES; i++) {
    ENEMY *e = &enemy[i];
    if(e->type) continue;
    e->type = type; e->hp = EHP[type] + (loop ? 1 : 0);
    e->x = x; e->y = y << 4; e->vy = 0;
    e->dir = ((px >> 4) < x) ? -1 : 1;
    e->t = x; e->state = 0; e->flash = 0; e->hc = 0xFF;
    return e;
  }
  return 0;
}

// Enemy bullet (pixel position, speed in 1/16 pixels)
NOINLINE void BulletSpawn(int x, int y, int dx, int dy) {
  for(int i = 0; i < MAX_BULLETS; i++) {
    SHOT *b = &bullet[i];
    if(b->life) continue;
    b->x = x << 4; b->y = y << 4; b->dx = dx; b->dy = dy; b->life = 90;
    return;
  }
}

// Enemy shoots at the hero (aimed = 0: horizontally, else in one of 8 directions)
NOINLINE void EnemyShoot(int x, int y, int aimed, int speed) {
  int dx = (px >> 4) + 4 - x, dy = (py >> 4) + 8 - y, ax = abs(dx), ay = abs(dy);
  int sx = (dx < 0) ? -speed : speed, sy = (dy < 0) ? -speed : speed;
  if(!aimed || ax > (ay << 1)) sy = 0;
  else if(ay > (ax << 1)) sx = 0;
  else {sx = (sx * 3) >> 2; sy = (sy * 3) >> 2;}
  BulletSpawn(x, y, sx, sy);
  Beep(100, 3);
}

void EnemyKill(ENEMY *e) {
  int sz = (e->type == E_BIG) ? 16 : 8;
  FxSpawn(e->x + (sz >> 1) - 4, (e->y >> 4) + (sz >> 1) - 4);
  AddScore(EHP[e->type] * 100);
  if(e->type >= E_TURRET || !(++kills & 7)) ItemSpawn(e->x, e->y >> 4, (kills & 16) ? I_ENERGY : ITEM_CYCLE[kills & 15]);
  e->type = E_NONE;
  Beep(150, 6); Beep(90, 8);
}

// Enemy is hit (pierce: laser, beam, wheel - only every few frames)
void EnemyHit(ENEMY *e, int dmg, int pierce) {
  if(pierce && e->flash) return;
  e->flash = 4;
  e->hp -= dmg;
  if(e->hp <= 0) EnemyKill(e);
  else Beep(70, 2);
}

// Gravity and walking for enemies on the ground
void EnemyMove(ENEMY *e, int step, int careful) {
  int sz = (e->type == E_BIG) ? 16 : 8;
  int y = e->y >> 4;
  int grounded = GroundAt(e->x + 1, y + sz) || GroundAt(e->x + sz - 2, y + sz);
  if(step) {
    int fx = (e->dir > 0) ? e->x + sz : e->x - 1;
    if(SolidAt(fx, y + sz - 4) || SolidAt(fx, y + 1) || (careful && grounded && !GroundAt(fx, y + sz))) e->dir = -e->dir;
    else e->x += e->dir;
  }
  if(grounded && e->vy >= 0) {e->vy = 0; e->y = y << 4; return;}
  e->vy += GRAVITY;
  if(e->vy > FALL_MAX) e->vy = FALL_MAX;
  int ny = (e->y + e->vy) >> 4;
  if(e->vy > 0 && (GroundAt(e->x + 1, ny + sz - 1) || GroundAt(e->x + sz - 2, ny + sz - 1))) {
    e->y = (((ny + sz - 1) & ~7) - sz) << 4; e->vy = 0;
  }
  else if(e->vy < 0 && SolidAt(e->x + (sz >> 1), ny)) e->vy = 0;
  else e->y += e->vy;
}

void EnemiesUpdate(void) {
  int fast = loop ? 1 : 0;                       // enemies are faster in the second round
  int hx = (px >> 4) + 4, hy = (py >> 4) + 8;    // center of the hero
  for(int i = 0; i < MAX_ENEMIES; i++) {
    ENEMY *e = &enemy[i];
    if(!e->type) continue;
    e->t++;
    if(e->flash) e->flash--;
    int sz = (e->type == E_BIG) ? 16 : 8;
    int ey = e->y >> 4, ecx = e->x + (sz >> 1);
    int side = (hx < ecx) ? -1 : 1;
    int close = abs(hx - ecx) < 100 && abs(hy - ey) < 48;
    int step = (frame & 1) | fast;

    switch(e->type) {
      case E_WALKER:                              // walks, turns at edges
        EnemyMove(e, step, 1);
        break;
      case E_HOPPER:                              // hops towards the hero
        if(e->vy == 0 && GroundAt(e->x + 4, ey + 8)) {
          if(e->t > (fast ? 30 : 45)) {e->t = 0; e->vy = -52; e->dir = side;}
          EnemyMove(e, 0, 0);
        }
        else EnemyMove(e, 1, 0);
        break;
      case E_SOLDIER:                             // walks and shoots when the hero is in line
        if(close && abs(hy - ey - 4) < 10) {
          e->dir = side;
          if(!(e->t & 31)) EnemyShoot(ecx, ey + 3, 0, 40);
          EnemyMove(e, 0, 1);
        }
        else EnemyMove(e, frame & 1, 1);
        break;
      case E_FLYER:                               // flies in waves towards the hero
        if(!(e->t & 63)) e->dir = side;
        if(step) e->x += e->dir;
        e->y += SINE[(e->t >> 1) & 31] << 1;
        break;
      case E_DROPPER:                             // waits at the ceiling, drops on the hero
        if(!e->state) {
          if(abs(hx - ecx) < 14 && hy > ey) e->state = 1;
        }
        else {
          if(!(e->t & 15)) e->dir = side;
          EnemyMove(e, step, 1);
        }
        break;
      case E_HOMER:                               // slowly follows the hero
        if(close && step) {
          e->x += side; e->dir = side;
          e->y += (hy > ey + 4) ? 16 : -16;
        }
        break;
      case E_TURRET:                              // shoots aimed bullets
        e->dir = side;
        if(close && !(e->t & 63)) EnemyShoot(ecx, ey + 3, 1, 32);
        break;
      case E_GEN:                                 // generates flyers
        if(close && !(e->t & 127)) EnemyAdd(E_FLYER, e->x, ey);
        break;
      case E_BIG:                                 // giant walker: walks, jumps and shoots
        if(close && !(e->t & 63)) {e->dir = side; EnemyShoot(ecx + side * 8, ey + 4, 0, 48);}
        if(close && (e->t & 127) == 100 && e->vy == 0) e->vy = -48;
        EnemyMove(e, frame & 1, 1);
        break;
    }

    // Out of sight: remove it and put it back into the map
    ey = e->y >> 4;
    if(e->x < camX - 48 || e->x > camX + 176 || ey < camY - 40 || ey > camY + 96) {
      if(e->hc != 0xFF) {TileGet(e->hc, e->hr, 1); gone[gCell] &= ~gBit;}
      e->type = E_NONE;
      continue;
    }

    // Touching the hero: the wheel destroys enemies
    if(!dying && Collide((px >> 4) + 1, (py >> 4) + (wheel ? 8 : 0), 6, wheel ? 8 : 16, e->x + 1, ey + 1, sz - 2, sz - 2)) {
      if(wheel) EnemyHit(e, 1, 1);
      else Hurt(4);
    }
  }
}

// ===================================================================================
// Shots and Bullets
// ===================================================================================
void ShotsUpdate(void) {
  for(int i = 0; i < MAX_SHOTS; i++) {
    SHOT *p = &shot[i];
    if(!p->life) continue;
    int sx = p->x >> 4, sy = p->y >> 4;

    // Mine: explodes when an enemy touches it or after a while
    if(p->type == S_MINE) {
      if(--p->life == 0 || HitArea(sx - 1, sy - 1, 6, 6, 0, 4)) {
        p->life = 0;
        FxSpawn(sx - 4, sy - 6); FxSpawn(sx - 8, sy - 10); FxSpawn(sx, sy - 10);
        HitArea(sx - 14, sy - 14, 30, 30, 4, 0);
        Beep(50, 20); Beep(30, 30);
      }
      continue;
    }

    p->life--;
    int ox = sx, oy = sy;
    p->x += p->dx; p->y += p->dy;
    sx = p->x >> 4; sy = p->y >> 4;
    int laser = (p->type == S_LASER), w = laser ? LASER_W : 2;
    int hx = (laser && p->dx > 0) ? sx + w - 1 : sx;
    int t = TileGet(hx >> 3, sy >> 3, 1);
    if(t == T_HIDDEN) {                           // invisible block: releases an extra
      gone[gCell] |= gBit;
      ItemSpawn(hx & ~7, sy & ~7, HIDDEN_ITEMS[((hx ^ sy) >> 3) & 3]);
      FxSpawn(hx & ~7, sy & ~7);
      p->life = 0;
      Beep(250, 10); Beep(200, 10);
      continue;
    }
    if(IS_SOLID(t)) {
      if(t == T_PBLOCK || t == T_BREAK) {BlockHit(hx >> 3, sy >> 3, t); p->life = 0;}
      else if(p->type == S_REBOUND) {             // rebound shots bounce off the walls
        int bx = SolidAt(sx, oy), by = SolidAt(ox, sy);
        p->x -= p->dx; p->y -= p->dy;
        if(bx || !by) p->dx = -p->dx;
        if(by || !bx) p->dy = -p->dy;
        Beep(150, 1);
      }
      else p->life = 0;
      continue;
    }
    if(sx < camX - 16 || sx > camX + 136 || sy < camY - 8 || sy > camY + 64) {p->life = 0; continue;}

    // Hit enemies (the laser goes through)
    if(HitArea(sx, sy - 1, w, 3, laser ? 2 : 1, laser ? 1 : 2) && !laser) p->life = 0;
  }
}

void BulletsUpdate(void) {
  for(int i = 0; i < MAX_BULLETS; i++) {
    SHOT *b = &bullet[i];
    if(!b->life) continue;
    b->life--;
    b->x += b->dx; b->y += b->dy;
    int x = b->x >> 4, y = b->y >> 4;
    if(SolidAt(x, y) || x < camX - 8 || x > camX + 136 || y < camY - 8 || y > camY + 64) {b->life = 0; continue;}
    if(!dying && !wheel && !invuln && !shield && Collide((px >> 4) + 1, (py >> 4) + 1, 6, 14, x - 1, y - 1, 3, 3)) {
      b->life = 0; Hurt(3);
    }
  }
}

// ===================================================================================
// Guardians
// ===================================================================================
void BossHit(int dmg, int pierce) {
  if(bossOn != 1 || (pierce && bossFlash)) return;
  if(dmg > 4) dmg = 4;
  bossFlash = 4;
  Beep(50, 4);
  if(bossHP > dmg) {bossHP -= dmg; return;}
  bossHP = 0; bossOn = 2; bossT = 0;
  for(int i = 0; i < MAX_BULLETS; i++) bullet[i].life = 0;
  AddScore(5000 * bossType);
}

void BossUpdate(void) {
  if(!bossType) return;
  int hx = (px >> 4) + 4, by = bossY >> 4, floorY = arenaY + 56 - 24;

  // Appear when the hero enters the arena
  if(!bossOn) {
    if((px >> 4) < arenaX + 24 || (py >> 4) < arenaY || (py >> 4) > arenaY + 64) return;
    bossOn = 1; bossState = 0; bossT = 0; bossFlash = 0; bossVY = 0; bossDir = -1;
    bossX = arenaX + 88; bossY = ((bossType == 2) ? floorY : arenaY + 9) << 4;
    bossHP = 30 + bossType * 10 + (loop ? 20 : 0);
    ShowMessage("GUARDIAN", 60);
    Beep(60, 60); Beep(40, 60);
    return;
  }

  // Exploding
  if(bossOn == 2) {
    if(!(++bossT & 3)) {
      FxSpawn(bossX + (JOY_random() & 15), by + (JOY_random() & 15));
      Beep((JOY_random() & 0x3F) + 20, 6);
    }
    if(bossT > 70) {bossOn = 3; exitLocked = 0; ShowMessage("GUARDIAN DESTROYED", 90);}
    return;
  }
  if(bossOn != 1) return;
  bossT++;
  if(bossFlash) bossFlash--;
  int cx = bossX + 12;

  switch(bossType) {
    case 1:                                       // the crusher: hovers and slams down
      if(bossState == 0) {                        // follow the hero
        if(cx < hx - 2) bossX++;
        else if(cx > hx + 2) bossX--;
        if(bossT > 50 && abs(cx - hx) < 8) {bossState = 1; bossVY = 0;}
      }
      else if(bossState == 1) {                   // fall down, shock waves on impact
        bossVY += 6; bossY += bossVY;
        if((bossY >> 4) >= floorY) {
          bossY = floorY << 4; bossState = 2; bossT = 0;
          Beep(30, 30);
          BulletSpawn(cx - 12, arenaY + 52, -36, 0);
          BulletSpawn(cx + 12, arenaY + 52, 36, 0);
        }
      }
      else if(bossState == 2) {                   // rest
        if(bossT > (bossHP < 20 ? 12 : 25)) bossState = 3;
      }
      else {                                      // rise
        bossY -= 16;
        if((bossY >> 4) <= arenaY + 9) {bossState = 0; bossT = 0;}
      }
      break;

    case 2:                                       // the walker: walks, jumps and shoots
      bossVY += GRAVITY; bossY += bossVY;
      if((bossY >> 4) >= floorY) {bossY = floorY << 4; bossVY = 0;}
      if((frame & 1) || bossHP < 25) bossX += bossDir;
      if(bossX < arenaX + 8)  bossDir = 1;
      if(bossX > arenaX + 96) bossDir = -1;
      if(!(bossT & 31)) EnemyShoot(cx + ((hx < cx) ? -12 : 12), by + 7, bossT & 64, 40);
      if(!(bossT & 127) && !bossVY) bossVY = -64;
      break;

    default:                                      // the machine: floats and fires spreads
      bossX = arenaX + 52 + SINE[(bossT >> 1) & 31] * 5;
      bossY = (arenaY + 16 + SINE[(bossT + 8) & 31]) << 4;
      if(!(bossT & 31)) {
        EnemyShoot(cx, by + 20, 1, 36);
        EnemyShoot(cx - 8, by + 18, 1, 36);
        EnemyShoot(cx + 8, by + 18, 1, 36);
      }
      if(!(bossT & 127)) EnemyAdd(E_HOMER, cx - 4, by + 16);
      break;
  }

  // Touching the hero
  if(!dying && Collide((px >> 4) + 1, (py >> 4) + 1, 6, 14, bossX + 2, (bossY >> 4) + 2, 20, 21)) Hurt(4);
}

// ===================================================================================
// Rendering
// ===================================================================================

// Put one byte column into the page buffer at pixel row y (screen coordinates).
// With a mask, the pixels around it are cleared, so it stands out from the background.
NOINLINE void DrawColumn(int page, int x, int y, int b, int mask) {
  int row = y - (page << 3);
  if(x < 0 || x > 127 || row <= -8 || row >= 8) return;
  uint16_t m = mask ? (b | (b << 1) | (b >> 1)) : 0;
  if(row >= 0) {buf[x] &= ~(m << row); buf[x] |= b << row;}
  else         {buf[x] &= ~(m >> -row); buf[x] |= b >> -row;}
}

// Sprite with mask (screen coordinates), h: height in pages
NOINLINE void DrawSprite(int page, int x, int y, int w, int h, const uint8_t *bmp, int flags) {
  for(int p = 0; p < h; p++, y += 8, bmp += w) {
    int row = y - (page << 3);
    if(row <= -8 || row >= 8) continue;
    for(int i = 0; i < w; i++)
      DrawColumn(page, x + i, y, bmp[(flags & F_MIRROR) ? w - 1 - i : i], 1);
  }
}

// 5x7 font, scale 1 or 2
void DrawText(int page, int x, int y, const char *s, int scale) {
  for(; *s; s++) {
    int c = 0;
    while(FONT_CHARS[c] && FONT_CHARS[c] != *s) c++;
    const uint8_t *g = &FONT[c * 5];
    for(int i = 0; i < 5; i++) {
      if(scale == 1) DrawColumn(page, x + i, y, g[i], 1);
      else {
        uint16_t w = 0;                           // double every bit
        for(int b = 0; b < 8; b++) if(g[i] & (1 << b)) w |= 3 << (b << 1);
        for(int k = 0; k < 2; k++) {
          DrawColumn(page, x + (i << 1) + k, y, w, 1);
          DrawColumn(page, x + (i << 1) + k, y + 8, w >> 8, 1);
        }
      }
    }
    x += 6 * scale;
  }
}

// Text centered on the screen
void DrawTextC(int page, int y, const char *s, int scale) {
  int len = 0;
  while(s[len]) len++;
  DrawText(page, 64 - len * 3 * scale, y, s, scale);
}

// Number with a fixed count of digits: 3x5 digits (big = 0) or 5x7 font (big = 1)
void DrawNumber(int page, int x, int y, uint32_t n, int digits, int big) {
  char s[8];
  s[digits] = 0;
  while(digits--) {s[digits] = '0' + n % 10; n /= 10;}
  if(big) {DrawText(page, x, y, s, 1); return;}
  for(char *p = s; *p; p++, x += 4) DrawSprite(page, x, y, 3, 1, &DIGITS[(*p - '0') * 3], 0);
}

// One column (byte) of a tile with the edges of rock tiles
int TileByte(uint8_t (*tc)[4], int ci, int j, int k, int c, int r) {
  int t = tc[ci][j], b;
  if(t == T_ROCK) {
    b = ROCK_GFX[(theme << 4) + (((c * 3 + r) & 1) << 3) + k];
    if(!IS_SOLID(tc[ci][j - 1])) b |= 0x03;
    if(!IS_SOLID(tc[ci][j + 1])) b |= 0x80;
    if((k == 0 && !IS_SOLID(tc[ci - 1][j])) || (k == 7 && !IS_SOLID(tc[ci + 1][j]))) b = 0xFF;
    return b;
  }
  b = TILE_GFX[(t << 3) + k];
  if(t == T_EXIT) b = (b << (frame & 7)) | (b >> (8 - (frame & 7)));
  if(t == T_PBLOCK && (frame & 16)) b = (k == 0 || k == 7) ? 0xFF : 0x81;
  return b & 0xFF;
}

// Map of one page: distant stars and the tiles
void DrawMap(int page) {
  uint8_t tc[19][4];
  int wy = camY + ((page - 1) << 3);
  int r0 = wy >> 3, c0 = camX >> 3;
  int s = wy & 7, i, j;
  for(i = 0; i < 19; i++)
    for(j = 0; j < 4; j++) tc[i][j] = Tile(c0 - 1 + i, r0 - 1 + j);
  for(int x = 0; x < 128; x++) {
    int wx = camX + x;
    int ci = (wx >> 3) - c0 + 1, k = wx & 7;
    uint16_t col = TileByte(tc, ci, 1, k, c0 - 1 + ci, r0) | (TileByte(tc, ci, 2, k, c0 - 1 + ci, r0 + 1) << 8);
    // Distant stars (half scrolling speed)
    int sx = (camX >> 1) + x;
    if((sx & 7) == 3) {
      int sy = (((sx >> 3) * 37) - (camY >> 1)) & 63;
      int d = sy - ((page - 1) << 3);
      if(d >= 0 && d < 8 && !IS_SOLID(tc[ci][1 + ((s + d) >> 3)])) col |= 1 << (s + d);
    }
    buf[x] = col >> s;
  }
}

// Status line: energy, lives, weapon, power lines, wheels, diamonds, time
void DrawStatus(void) {
  int i;
  for(i = 0; i < 22; i++) buf[i] = (i == 0 || i == 21) ? 0x3E : (i <= energy && (!shield || (frame & 4))) ? 0x3E : 0x22;
  DrawSprite(0, 25, 1, 5, 1, ICONS, 0);
  DrawNumber(0, 31, 1, lives, 1, 0);
  char w[2] = {"MLR"[weapon], 0};
  DrawText(0, 37, 0, w, 1);
  DrawNumber(0, 43, 1, wlevel + 1, 1, 0);
  DrawSprite(0, 50, 1, 5, 1, ICONS + 5, 0);
  DrawNumber(0, 56, 1, lines, 1, 0);
  DrawSprite(0, 63, 1, 5, 1, ICONS + 10, 0);
  DrawNumber(0, 69, 1, wheels, 1, 0);
  if(bossOn == 1) {                               // energy of the guardian
    for(i = 80; i < 128; i++) buf[i] = (i == 80 || i == 127) ? 0x3E : (i - 80 <= (bossHP >> 1)) ? 0x3E : 0x22;
  }
  else {
    DrawSprite(0, 77, 1, 5, 1, ICONS + 15, 0);
    DrawNumber(0, 83, 1, diamonds, 2, 0);
    DrawSprite(0, 110, 1, 5, 1, ICONS + 20, 0);
    DrawNumber(0, 116, 1, timeLeft, 3, 0);
  }
  for(i = 0; i < 128; i += 2) buf[i] |= 0x80;   // dotted line
}

// All the moving objects of one page
void DrawObjects(int page) {
  int i;
  int ox = -camX, oy = 8 - camY;

  // Items (blink before they vanish)
  for(i = 0; i < MAX_ITEMS; i++) {
    ITEM *p = &item[i];
    if(p->type && (p->t < 200 || (frame & 4)))
      DrawSprite(page, p->x + ox, (p->y >> 4) + oy, 8, 1, SPR_ITEM + ((p->type - 1) << 3), 0);
  }

  // Enemies
  for(i = 0; i < MAX_ENEMIES; i++) {
    ENEMY *e = &enemy[i];
    if(!e->type) continue;
    int f = (e->t & 8) ? 1 : 0, flags = (e->dir > 0) ? F_MIRROR : 0;
    if(e->type == E_BIG) DrawSprite(page, e->x + ox, (e->y >> 4) + oy, 16, 2, SPR_BIG + (f << 5), flags);
    else DrawSprite(page, e->x + ox, (e->y >> 4) + oy, 8, 1, ESPR[e->type] + (f << 3), flags);
  }

  // Guardian
  if(bossOn == 1 || (bossOn == 2 && (bossT & 2)))
    DrawSprite(page, bossX + ox, (bossY >> 4) + oy, 24, 3, SPR_BOSS + (bossType - 1) * 72,
               (bossType == 2 && bossDir > 0) ? F_MIRROR : 0);

  // Shots and mines
  for(i = 0; i < MAX_SHOTS; i++) {
    SHOT *p = &shot[i];
    if(!p->life) continue;
    int x = (p->x >> 4) + ox, y = (p->y >> 4) + oy;
    if(p->type == S_LASER) {
      for(int k = 0; k < 6 + (wlevel << 2); k++) DrawColumn(page, x + k, y - 1, 0x03, 0);
    }
    else if(p->type == S_MINE) {
      if(p->life > 20 || (frame & 2)) DrawSprite(page, x, y, 4, 1, SPR_MINE, 0);
    }
    else DrawSprite(page, x, y - 1, 2, 1, SPR_PELLET, 0);
  }

  // Enemy bullets
  for(i = 0; i < MAX_BULLETS; i++)
    if(bullet[i].life) DrawSprite(page, (bullet[i].x >> 4) + ox - 1, (bullet[i].y >> 4) + oy - 1, 3, 1, SPR_BULLET, 0);

  // Explosions
  for(i = 0; i < MAX_FX; i++)
    if(fx[i].t) DrawSprite(page, fx[i].x + ox, fx[i].y + oy, 8, 1, SPR_EXPL + ((((fx[i].t - 1) * 11) >> 5) << 3), 0);

  // Hero (blinks while invulnerable)
  int hx = (px >> 4) + ox, hy = (py >> 4) + oy;
  if(!dying && !((invuln | shield) & 2)) {
    if(wheel) DrawSprite(page, hx, hy + 8, 8, 1, SPR_WHEEL + (((walkAnim >> 1) & 1) << 3), 0);
    else {
      int f = 0;
      if(!onGround) f = 3;
      else if(vx) f = 1 + ((walkAnim >> 2) & 1);
      DrawSprite(page, hx, hy, 8, 2, SPR_HERO + (f << 4), facing);
    }
  }

  // Lightning beam
  if(beamOn) {
    int gx = (facing ? hx - 1 : hx + 8), gy = hy + 6;
    int dx = facing ? -BEAM_DX[beamAng] : BEAM_DX[beamAng], dy = BEAM_DY[beamAng];
    int steep = abs(dy) > abs(dx);
    for(i = 0; i < beamLen; i++) {
      int bx = gx + ((dx * i) >> 3), by = gy + ((dy * i) >> 3);
      int o = ((i * 3 + frame) >> 1) & 1;
      DrawColumn(page, bx, by, 1, 0);
      if(steep) DrawColumn(page, bx + o, by, 1, 0);
      else      DrawColumn(page, bx, by + o, 1, 0);
    }
  }

  // Power lines
  if(lineT) {
    int z = (frame & 1) ? 0xAA : 0x55;
    for(i = 0; i < 2; i++) {
      int x = (i ? lineX2 : lineX1) + ox;
      if(x >= 0 && x < 127) {buf[x] |= z; buf[x + 1] |= z ^ 0xFF;}
    }
  }
}

// Draw the whole screen page by page and send it to the OLED
void Tiny_Flip(int mode) {
  int page, i;
  for(page = 0; page < 8; page++) {
    for(i = 0; i < 128; i++) buf[i] = 0;

    switch(mode) {
      case R_TITLE:
        if(page > 5) DrawMap(page);
        DrawSprite(page, 10, 40, 8, 2, SPR_HERO + ((((frame >> 2) & 1) + 1) << 4), 0);
        DrawTextC(page, 1, "TINY", 2);
        DrawTextC(page, 17, "HURRICAN", 2);
        DrawText(page, 31, 32, "HI", 1);
        DrawNumber(page, 49, 32, hiscore, 7, 1);
        if(frame & 16) DrawTextC(page, 40, "PRESS FIRE", 1);
        break;

      case R_INTRO: {
        static char s[] = "WORLD 1-1";
        s[6] = '1' + (level >> 1); s[8] = '1' + (level & 1);
        if(loop) DrawTextC(page, 0, "ROUND 2", 1);
        DrawTextC(page, 10, s, 2);
        DrawSprite(page, 47, 30, 8, 2, SPR_HERO + (((frame >> 3) & 1) << 4), 0);
        DrawText(page, 61, 34, "X", 1);
        DrawNumber(page, 73, 34, lives, 1, 1);
        DrawText(page, 22, 54, "SCORE", 1);
        DrawNumber(page, 58, 54, score, 8, 1);
        break;
      }

      case R_GAMEOVER:
        DrawTextC(page, 16, "GAME OVER", 2);
        if(frame & 16) {
          DrawText(page, 22, 42, "SCORE", 1);
          DrawNumber(page, 58, 42, score, 8, 1);
        }
        break;

      case R_WIN:
        DrawTextC(page, 6, "CONGRATULATIONS", 1);
        DrawTextC(page, 20, "THE PLANET IS SAVED", 1);
        DrawSprite(page, 60, 36, 8, 2, SPR_HERO + (((frame >> 3) & 1) << 4), 0);
        break;

      default:
        if(page == 0) DrawStatus();
        else {
          DrawMap(page);
          DrawObjects(page);
          if(msgTimer && (msgTimer > 20 || (msgTimer & 4))) DrawTextC(page, 24, msgText, 1);
        }
        break;
    }

    // Send page to OLED
    JOY_OLED_data_start(page);
    for(i = 0; i < 128; i++) JOY_OLED_send(buf[i]);
    JOY_OLED_end();
  }
}
