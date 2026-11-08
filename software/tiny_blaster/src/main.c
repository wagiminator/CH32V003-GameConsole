// ===================================================================================
// Project:   Tiny Blaster - Vertical Scrolling Shoot'em Up for CH32V003
// Version:   v1.0
// Year:      2026
// Github:    https://github.com/wagiminator
// EasyEDA:   https://easyeda.com/wagiminator
// License:   http://creativecommons.org/licenses/by-sa/3.0/
// ===================================================================================
//
// Description:
// ------------
// A small Xenon II-like vertically scrolling shoot'em up for the CH32V003 Mini Game
// Console. Fly up through five stages full of alien drones, weavers, divers, rocks,
// wall turrets, spinners and chasers that attack from behind, and defeat the
// guardian at the end of each stage. Shot down enemies leave credit bubbles. Spend
// the credits in the weapon shop before each stage: gun power-ups, side shot, rear
// shot, homing missiles, laser, cannon, drones, electro ball, speed-up, autofire,
// energy, extra life and Super Nashwan Power (all weapons for a while). Capsules
// carried by the pods give a weapon, energy or Nashwan power right away. The walls
// of the canyons drain your energy.
//
// Controls:
// ---------
// Joypad:      move the ship (also diagonally); select an item in the shop
// Fire button: shoot (hold it, autofire shoots faster); buy in the shop, start game
//
// Scoring:
// --------
// Drone 50, weaver 60, diver 80, chaser 100, rock 70, turret 120, spinner 300,
// pod 150, guardian 5000 x stage. Credit bubbles: 10 or 50 credits.
//
// Compilation Settings:
// ---------------------
// - Make sure GCC toolchain (gcc-riscv64-unknown-elf, newlib) and Python3 with rvprog
//   are installed. In addition, Linux requires access rights to WCH-LinkE programmer.
// - Connect the WCH-LinkE programmer to the MCU board.
// - Run 'make flash'.


#include <driver.h>           // TinyJoypad conversion driver
#include <spritebank.h>       // grafix
#include <stages.h>           // walls and attack waves

// ===================================================================================
// Game Settings
// ===================================================================================
#define FRAME_TIME    (33 * DLY_MS_TIME)  // ~30 frames per second
#define PF_W          104     // width of the playfield (right of it: status panel)
#define MAX_ENEMIES   10      // enemies at the same time
#define MAX_SHOTS     20      // player shots
#define MAX_ESHOTS    12      // enemy shots
#define MAX_EXPL      6       // explosions
#define MAX_ITEMS     4       // credit bubbles and capsules
#define MAX_STARS     14      // background stars
#define LIVES         3       // ships at game start
#define ENERGY_MAX    8       // energy of the ship
#define START_CREDITS 1000    // credits for the first visit to the shop
#define NASHWAN_TIME  450     // duration of the Super Nashwan Power (frames)

// Weapons (bit mask)
#define W_SIDE        0x01
#define W_REAR        0x02
#define W_HOMING      0x04
#define W_LASER       0x08
#define W_CANNON      0x10
#define W_DRONES      0x20
#define W_ELECTRO     0x40
#define W_AUTO        0x80

// Enemy types, shot types, item types, guardian states, render modes
enum {E_NONE, E_DRONE, E_WEAVER, E_DIVER, E_CHASER, E_TURRET_L, E_TURRET_R, E_SPINNER, E_ROCK, E_POD};
enum {S_NONE, S_GUN, S_SIDE, S_REAR, S_HOMING, S_LASER, S_CANNON};
enum {I_NONE, I_COIN, I_BIGCOIN, I_WEAPON, I_ENERGY, I_NASHWAN};
enum {B_NONE, B_ENTER, B_FIGHT, B_DYING, B_DONE};
enum {R_TITLE, R_GAME, R_SHOP, R_GAMEOVER, R_WIN, R_STAGE};

// ===================================================================================
// Game Objects
// ===================================================================================
typedef struct {
  int16_t x, y;               // position in 1/16 pixels
  int16_t bx;                 // base position (weavers, turrets)
  int8_t  dx, dy;             // speed in 1/16 pixels per frame
  uint8_t type;               // E_NONE: free slot
  uint8_t hp;                 // hit points
  uint8_t t;                  // timer
  uint8_t flash;              // blinks when hit
} ENEMY;

typedef struct {
  int16_t x, y;               // position in pixels
  int8_t  dx, dy;             // speed in pixels per frame
  uint8_t type;               // S_NONE: free slot
  uint8_t hp;                 // hits left (cannon)
} SHOT;

typedef struct {
  int16_t x, y;               // position in 1/16 pixels
  int8_t  dx, dy;
} ESHOT;

typedef struct {
  int16_t x, y;               // position in pixels
  uint8_t t;                  // animation frame, 0: free
} EXPL;

typedef struct {
  int16_t x, y;               // position in 1/16 pixels
  uint8_t type;               // I_NONE: free slot
  uint8_t t;
} ITEM;

ENEMY    enemy[MAX_ENEMIES];
SHOT     shot[MAX_SHOTS];
ESHOT    eshot[MAX_ESHOTS];
uint8_t  eshotOn[MAX_ESHOTS];
EXPL     boom[MAX_EXPL];
ITEM     item[MAX_ITEMS];
uint8_t  starX[MAX_STARS], starY[MAX_STARS];
uint8_t  wallL[64], wallR[64];      // wall widths of the screen rows
uint8_t  buf[128];                  // one display page

// Player
int16_t  shipX, shipY;              // position in 1/16 pixels
int16_t  satX[2], satY[2];          // drones (pixels)
uint8_t  lives, energy, invuln, deadTimer, fireCool, volley;
uint8_t  weapons, gunLevel, speedLevel, wallHurt;
uint16_t nashwan, nashwanStock;
uint32_t score, hiscore;
uint16_t credits;

// Game progress
uint8_t  stage, loop, frame, gameOver, stageClear;
uint16_t scrollPos, waveIdx, waveEnd, wallStart, wallLen;
uint8_t  waveTimer;
uint8_t  spawnType[2], spawnLeft[2], spawnTimer[2];
int16_t  spawnX[2];
uint8_t  noise;

// Guardian
uint8_t  bossState, bossHP, bossMaxHP, bossT, bossAtk;
int16_t  bossX, bossY;

// Message in the middle of the playfield
const char *msgText;
uint8_t  msgNum, msgTimer;

// Shop
uint8_t  shopSel, shopTop;

// Clipping of sprites (playfield or whole screen)
uint8_t  clipW = PF_W;

// Sine table (movement of weavers and guardians)
const int8_t SINE[32] = {
  0, 3, 6, 9, 11, 13, 15, 16, 16, 16, 15, 13, 11, 9, 6, 3,
  0, -3, -6, -9, -11, -13, -15, -16, -16, -16, -15, -13, -11, -9, -6, -3
};

// Orbit of the electro ball (16 positions, radius 12 pixels)
const int8_t ORBIT_X[16] = {0, 5, 8, 11, 12, 11, 8, 5, 0, -5, -8, -11, -12, -11, -8, -5};
const int8_t ORBIT_Y[16] = {-12, -11, -8, -5, 0, 5, 8, 11, 12, 11, 8, 5, 0, -5, -8, -11};

// Enemies: hit points, points (x10), speed
const uint8_t E_HP[]     = {0, 1, 1, 2, 2, 3, 3, 8, 4, 3};
const uint8_t E_POINTS[] = {0, 5, 6, 8, 10, 12, 12, 30, 7, 15};

// Guardians: hit points (x stage factor), attacks
const uint8_t BOSS_HP[STAGES]    = {60, 80, 100, 120, 150};
const uint8_t BOSS_SPEED[STAGES] = {1, 2, 2, 3, 3};

// Shop items
#define SHOP_ITEMS 14
#define SHOP_BIT(n)   (1 << (((n) <= 7) ? (n) - 1 : 7))   // weapon bit of shop items 1..7, 9
const char *const SHOP_NAME[SHOP_ITEMS] = {
  "GUN POWER", "SIDE SHOT", "REAR SHOT", "HOMING", "LASER", "CANNON", "DRONES",
  "ELECTRO", "SPEED UP", "AUTOFIRE", "ENERGY", "EXTRA LIFE", "NASHWAN", "EXIT SHOP"
};
const uint16_t SHOP_PRICE[SHOP_ITEMS] = {
  300, 400, 300, 600, 700, 500, 800, 600, 200, 300, 200, 1000, 900, 0
};

// ===================================================================================
// Function Prototypes
// ===================================================================================
#define NOINLINE  __attribute__((noinline))
NOINLINE void Beep(uint8_t freq, uint8_t dur) {JOY_sound(freq, dur);}
void TitleScreen(void);
void ShopScreen(void);
void StageScreen(void);
void GameOverScreen(void);
void WinScreen(void);
void NewGame(void);
void StartStage(void);
void UpdateGame(void);
void Tiny_Flip(uint8_t mode);
void WaitFrame(uint32_t start);
void MoveStars(uint8_t speed);
void WallsUpdate(void);
void PlayerUpdate(void);
void PlayerFire(void);
NOINLINE void PlayerHit(uint8_t damage);
void ShotsUpdate(void);
void WavesUpdate(void);
NOINLINE void EnemySpawn(uint8_t type, int16_t x);
void EnemiesUpdate(void);
NOINLINE void EnemyDamage(ENEMY *e, uint8_t damage);
NOINLINE void EnemyFire(int16_t x, int16_t y, int8_t dx, int8_t dy);
NOINLINE void EnemyFireAimed(int16_t x, int16_t y, uint8_t speed);
void EnemyShotsUpdate(void);
void BossUpdate(void);
NOINLINE void BossDamage(uint8_t damage);
void ItemsUpdate(void);
NOINLINE void ItemDrop(int16_t x, int16_t y, uint8_t type);
void GiveWeapon(void);
NOINLINE void Explode(int16_t x, int16_t y);
NOINLINE void AddScore(uint16_t points);
NOINLINE void ShowMessage(const char *text, uint8_t num, uint8_t time);
NOINLINE uint8_t Collide(int16_t x1, int16_t y1, uint8_t w1, uint8_t h1, int16_t x2, int16_t y2, uint8_t w2, uint8_t h2);

// ===================================================================================
// Main Function
// ===================================================================================
int main(void) {
  // Setup
  JOY_init();
  for(uint8_t i = 0; i < MAX_STARS; i++) {starX[i] = JOY_random() % PF_W; starY[i] = JOY_random() & 63;}

  // Loop
  while(1) {
    TitleScreen();
    NewGame();
    while(!gameOver) {
      ShopScreen();
      StageScreen();
      StartStage();
      while(!gameOver && !stageClear) {
        uint32_t start = STK->CNT;
        UpdateGame();
        Tiny_Flip(R_GAME);
        WaitFrame(start);
      }
      if(stageClear && ++stage >= STAGES) {     // all stages done: next loop
        WinScreen();
        stage = 0; loop++;
      }
    }
    GameOverScreen();
  }
}

// ===================================================================================
// Screens
// ===================================================================================

// Title screen with high score; returns when the fire button is pressed
void TitleScreen(void) {
  frame = 0;
  while(JOY_act_pressed());                       // wait until fire button released
  while(1) {
    uint32_t start = STK->CNT;
    frame++;
    MoveStars(2);
    Tiny_Flip(R_TITLE);
    if(JOY_act_pressed()) break;
    JOY_random();                                 // stir the random generator
    WaitFrame(start);
  }
  Beep(100, 60); Beep(150, 60); Beep(200, 100);
}

// Weapon shop: select an item with the joypad, buy it with the fire button
void ShopScreen(void) {
  uint8_t repeat = 0;
  shopSel = SHOP_ITEMS - 1; shopTop = SHOP_ITEMS - 6;
  while(JOY_act_pressed());
  while(1) {
    uint32_t start = STK->CNT;
    frame++;
    if(repeat) repeat--;
    else if(JOY_up_pressed() && shopSel) {shopSel--; repeat = 5; Beep(200, 3);}
    else if(JOY_down_pressed() && shopSel < SHOP_ITEMS - 1) {shopSel++; repeat = 5; Beep(200, 3);}
    if(shopSel < shopTop) shopTop = shopSel;
    if(shopSel > shopTop + 5) shopTop = shopSel - 5;

    if(JOY_act_pressed()) {
      uint16_t price = SHOP_PRICE[shopSel];
      if(shopSel == SHOP_ITEMS - 1) break;        // exit shop
      uint8_t ok = (credits >= price);
      switch(shopSel) {                           // can it be bought?
        case 0:  if(gunLevel >= 3) ok = 0; break;
        case 8:  if(speedLevel >= 2) ok = 0; break;
        case 10: if(energy >= ENERGY_MAX) ok = 0; break;
        case 11: if(lives >= 9) ok = 0; break;
        case 12: if(nashwanStock) ok = 0; break;
        default: if(shopSel <= 9 && (weapons & SHOP_BIT(shopSel))) ok = 0; break;
      }
      if(ok) {
        credits -= price;
        switch(shopSel) {
          case 0:  gunLevel++; break;
          case 8:  speedLevel++; break;
          case 10: energy = ENERGY_MAX; break;
          case 11: lives++; break;
          case 12: nashwanStock = NASHWAN_TIME; break;
          default: weapons |= SHOP_BIT(shopSel); break;
        }
        Beep(150, 20); Beep(200, 20); Beep(250, 30);
      }
      else {Beep(50, 30);}
      while(JOY_act_pressed());
    }
    Tiny_Flip(R_SHOP);
    WaitFrame(start);
  }
  Beep(200, 30);
}

// Stage number
void StageScreen(void) {
  for(frame = 0; frame < 60; frame++) {
    uint32_t start = STK->CNT;
    MoveStars(1);
    Tiny_Flip(R_STAGE);
    WaitFrame(start);
  }
}

void GameOverScreen(void) {
  if(score > hiscore) hiscore = score;
  Beep(120, 100); Beep(90, 100); Beep(60, 200);
  frame = 0;
  while(JOY_act_released() || frame < 30) {
    uint32_t start = STK->CNT;
    frame++;
    MoveStars(1);
    Tiny_Flip(R_GAMEOVER);
    WaitFrame(start);
  }
}

void WinScreen(void) {
  Beep(150, 100); Beep(200, 100); Beep(250, 200);
  for(frame = 0; frame < 150; frame++) {
    uint32_t start = STK->CNT;
    MoveStars(2);
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
  score = 0; lives = LIVES; energy = ENERGY_MAX; credits = START_CREDITS;
  weapons = 0; gunLevel = 0; speedLevel = 0; nashwanStock = 0;
  stage = 0; loop = 0; gameOver = 0;
}

void StartStage(void) {
  uint8_t i;
  for(i = 0; i < MAX_ENEMIES; i++) enemy[i].type = E_NONE;
  for(i = 0; i < MAX_SHOTS; i++)   shot[i].type = S_NONE;
  for(i = 0; i < MAX_ESHOTS; i++)  eshotOn[i] = 0;
  for(i = 0; i < MAX_EXPL; i++)    boom[i].t = 0;
  for(i = 0; i < MAX_ITEMS; i++)   item[i].type = I_NONE;
  spawnLeft[0] = spawnLeft[1] = 0;
  waveIdx = WAVE_START[stage]; waveEnd = waveIdx + WAVE_COUNT[stage];
  waveTimer = 30;
  wallStart = WALL_START[stage]; wallLen = WALL_LEN[stage];
  scrollPos = 0; stageClear = 0; bossState = B_NONE; noise = 0;
  shipX = 48 << 4; shipY = 50 << 4;
  satX[0] = satX[1] = 51; satY[0] = satY[1] = 53;
  invuln = 60; deadTimer = 0; fireCool = 0; wallHurt = 0;
  nashwan = nashwanStock; nashwanStock = 0;
  ShowMessage(0, 0, 0);
  WallsUpdate();
}

// One frame of the game
void UpdateGame(void) {
  frame++;

  // Scrolling (1 pixel every 2 frames), stops for the guardian fight
  if((frame & 1) && bossState < B_FIGHT && (scrollPos >> 3) < wallLen - 9) scrollPos++;
  MoveStars(1);
  WallsUpdate();

  PlayerUpdate();
  ShotsUpdate();
  WavesUpdate();
  EnemiesUpdate();
  EnemyShotsUpdate();
  BossUpdate();
  ItemsUpdate();

  // Explosions
  for(uint8_t i = 0; i < MAX_EXPL; i++)
    if(boom[i].t && ++boom[i].t > 9) boom[i].t = 0;

  // Explosion noise
  if(noise) {
    noise--;
    Beep((JOY_random() & 0x3F) + 20, 3);
  }

  if(nashwan) nashwan--;
  if(msgTimer) msgTimer--;
}

NOINLINE void AddScore(uint16_t points) {
  score += points;
}

NOINLINE void ShowMessage(const char *text, uint8_t num, uint8_t time) {
  msgText = text; msgNum = num; msgTimer = time;
}

// Bounding box collision
NOINLINE uint8_t Collide(int16_t x1, int16_t y1, uint8_t w1, uint8_t h1, int16_t x2, int16_t y2, uint8_t w2, uint8_t h2) {
  return (x1 < x2 + w2) && (x2 < x1 + w1) && (y1 < y2 + h2) && (y2 < y1 + h1);
}

// ===================================================================================
// Stars and Walls
// ===================================================================================
void MoveStars(uint8_t speed) {
  for(uint8_t i = 0; i < MAX_STARS; i++) {
    if((i & 1) && (frame & 1)) continue;          // odd stars are slower (parallax)
    starY[i] += speed;
    if(starY[i] > 63) {starY[i] = 0; starX[i] = JOY_random() % PF_W;}
  }
}

// Wall widths of all screen rows (interpolated between the 8 row steps)
void WallsUpdate(void) {
  for(uint8_t y = 0; y < 64; y++) {
    uint16_t w = scrollPos + 63 - y;              // world row
    uint16_t i = w >> 3;
    uint8_t  f = w & 7, a = 0, b = 0;
    if(i < wallLen)     a = WALLS[wallStart + i];
    if(i + 1 < wallLen) b = WALLS[wallStart + i + 1];
    wallL[y] = (((a >> 4) * (8 - f) + (b >> 4) * f) >> 1);
    wallR[y] = (((a & 15) * (8 - f) + (b & 15) * f) >> 1);
  }
}

// Does a box (pixels) touch a wall?
NOINLINE uint8_t HitsWall(int16_t x, int16_t y, uint8_t w, uint8_t h) {
  for(int16_t r = y; r < y + h; r++) {
    if(r < 0 || r > 63) continue;
    if(x < wallL[r] || x + w > PF_W - wallR[r]) return 1;
  }
  return 0;
}

// ===================================================================================
// Player
// ===================================================================================
void PlayerUpdate(void) {
  uint8_t i;

  // Ship destroyed: wait for the explosion, then the next ship
  if(deadTimer) {
    if(--deadTimer == 0) {
      if(--lives == 0) gameOver = 1;
      else {
        energy = ENERGY_MAX; invuln = 90;
        shipX = ((PF_W >> 1) - 3) << 4; shipY = 50 << 4;
        if(gunLevel) gunLevel--;
      }
    }
    return;
  }

  // Move
  uint8_t v = 24 + (speedLevel << 3);
  if(JOY_up_pressed())    shipY -= v;
  if(JOY_down_pressed())  shipY += v;
  if(JOY_left_pressed())  shipX -= v;
  if(JOY_right_pressed()) shipX += v;
  if(shipX < 0) shipX = 0;
  if(shipX > (PF_W - 7) << 4) shipX = (PF_W - 7) << 4;
  if(shipY < 0) shipY = 0;
  if(shipY > (64 - 8) << 4) shipY = (64 - 8) << 4;

  // Walls push the ship back and drain the energy
  int16_t x = shipX >> 4, y = shipY >> 4;
  if(HitsWall(x + 1, y + 2, 5, 5)) {
    if(x + 3 < wallL[y + 4] + 4) shipX += 32;
    else shipX -= 32;
    if(++wallHurt > 10) {wallHurt = 0; PlayerHit(1);}
  }

  // Drones follow the ship
  for(i = 0; i < 2; i++) {
    int16_t tx = x + (i ? 11 : -9), ty = y + 3;
    satX[i] += (tx - satX[i]) >> 1;
    satY[i] += (ty - satY[i]) >> 1;
  }

  // Fire (hold the button); autofire and Nashwan power shoot faster
  if(fireCool) fireCool--;
  if(JOY_act_pressed() && !fireCool) {
    PlayerFire();
    fireCool = ((weapons & W_AUTO) || nashwan) ? 4 : 8;
  }

  if(invuln) invuln--;
}

// Add a player shot
NOINLINE void ShotAdd(int16_t x, int16_t y, int8_t dx, int8_t dy, uint8_t type) {
  for(uint8_t i = 0; i < MAX_SHOTS; i++) {
    SHOT *s = &shot[i];
    if(s->type) continue;
    s->x = x; s->y = y; s->dx = dx; s->dy = dy; s->type = type;
    s->hp = (type == S_CANNON) ? 3 : 1;
    return;
  }
}

// One volley of all weapons of the ship
void PlayerFire(void) {
  int16_t x = (shipX >> 4) + 3, y = shipY >> 4;
  uint8_t w = nashwan ? 0xFF : weapons, g = nashwan ? 3 : gunLevel, i, n;
  volley++;

  // Main gun: 1, 2, 3 or 5 shots
  switch(g) {
    case 0: ShotAdd(x, y - 2, 0, -4, S_GUN); break;
    case 1: ShotAdd(x - 2, y, 0, -4, S_GUN); ShotAdd(x + 2, y, 0, -4, S_GUN); break;
    case 2: ShotAdd(x, y - 2, 0, -4, S_GUN); ShotAdd(x - 2, y, -1, -4, S_GUN); ShotAdd(x + 2, y, 1, -4, S_GUN); break;
    default:
      ShotAdd(x, y - 2, 0, -4, S_GUN);
      ShotAdd(x - 2, y, -1, -4, S_GUN); ShotAdd(x + 2, y, 1, -4, S_GUN);
      ShotAdd(x - 3, y + 2, -2, -3, S_GUN); ShotAdd(x + 3, y + 2, 2, -3, S_GUN);
      break;
  }

  // Additional weapons
  if((w & W_SIDE) && (volley & 1)) {ShotAdd(x - 4, y + 4, -4, 0, S_SIDE); ShotAdd(x + 3, y + 4, 4, 0, S_SIDE);}
  if(w & W_REAR) {
    ShotAdd(x, y + 8, 0, 3, S_REAR);
    if(g >= 2) {ShotAdd(x - 2, y + 7, -1, 3, S_REAR); ShotAdd(x + 2, y + 7, 1, 3, S_REAR);}
  }
  if((w & W_HOMING) && !(volley % 3)) {
    for(i = n = 0; i < MAX_SHOTS; i++) if(shot[i].type == S_HOMING) n++;
    if(n < 4) {ShotAdd(x - 4, y + 3, -1, -1, S_HOMING); ShotAdd(x + 3, y + 3, 1, -1, S_HOMING);}
  }
  if((w & W_LASER) && (volley & 1)) ShotAdd(x, y - 8, 0, -8, S_LASER);
  if((w & W_CANNON) && !(volley & 3)) ShotAdd(x - 1, y - 4, 0, -3, S_CANNON);
  if(w & W_DRONES) for(i = 0; i < 2; i++) ShotAdd(satX[i] + 2, satY[i] - 2, 0, -4, S_GUN);
  Beep(230, 2);
}

// The ship is hit
NOINLINE void PlayerHit(uint8_t damage) {
  if(invuln || deadTimer) return;
  invuln = 20;
  Beep(80, 10);
  if(energy > damage) {energy -= damage; return;}
  energy = 0; deadTimer = 50;
  int16_t x = shipX >> 4, y = shipY >> 4;
  Explode(x, y); Explode(x + 4, y - 4); Explode(x - 2, y + 4);
  noise = 15;
}

// ===================================================================================
// Player Shots
// ===================================================================================
void ShotsUpdate(void) {
  uint8_t i, j;
  for(i = 0; i < MAX_SHOTS; i++) {
    SHOT *s = &shot[i];
    if(!s->type) continue;

    // Homing missiles steer towards the nearest enemy (or the guardian)
    if(s->type == S_HOMING && (frame & 1)) {
      int16_t tx = -1, ty = 0, best = 999;
      for(j = 0; j < MAX_ENEMIES; j++) {
        ENEMY *e = &enemy[j];
        if(!e->type) continue;
        int16_t ex = (e->x >> 4) + 4, ey = (e->y >> 4) + 4;
        int16_t d = abs(ex - s->x) + abs(ey - s->y);
        if(d < best && ey >= 0) {best = d; tx = ex; ty = ey;}
      }
      if(bossState == B_FIGHT && tx < 0) {tx = (bossX >> 4) + 12; ty = (bossY >> 4) + 8;}
      if(tx >= 0) {
        if(tx > s->x && s->dx < 3) s->dx++;
        if(tx < s->x && s->dx > -3) s->dx--;
        if(ty > s->y && s->dy < 3) s->dy++;
        if(ty < s->y && s->dy > -3) s->dy--;
      }
      else if(s->dy > -3) s->dy--;
    }

    s->x += s->dx; s->y += s->dy;
    if(s->x < -4 || s->x > PF_W || s->y < -8 || s->y > 64) {s->type = S_NONE; continue;}
    if(s->type != S_LASER && HitsWall(s->x, s->y, 1, 1)) {s->type = S_NONE; continue;}

    // Size of the shot
    uint8_t w = 1, h = 3, dmg = 1;
    if(s->type == S_SIDE)   {w = 3; h = 1;}
    if(s->type == S_HOMING) {w = 3; dmg = 2;}
    if(s->type == S_LASER)  h = 8;
    if(s->type == S_CANNON) {w = 4; h = 4; dmg = 3;}

    // Enemies
    for(j = 0; j < MAX_ENEMIES; j++) {
      ENEMY *e = &enemy[j];
      if(!e->type) continue;
      if(Collide(s->x, s->y, w, h, e->x >> 4, e->y >> 4, 8, 8)) {
        EnemyDamage(e, dmg);
        if(s->type != S_LASER && --s->hp == 0) {s->type = S_NONE; break;}
      }
    }

    // Guardian
    if(s->type && bossState == B_FIGHT && Collide(s->x, s->y, w, h, (bossX >> 4) + 2, bossY >> 4, 20, 14)) {
      BossDamage(dmg);
      if(s->type != S_LASER || (frame & 1)) s->type = S_NONE;
    }
  }

  // Electro ball circles around the ship
  if(((weapons & W_ELECTRO) || nashwan) && !deadTimer && !(frame & 3)) {
    uint8_t k = (frame >> 1) & 15;
    int16_t ex = (shipX >> 4) + 1 + ORBIT_X[k], ey = (shipY >> 4) + 2 + ORBIT_Y[k];
    for(j = 0; j < MAX_ENEMIES; j++) {
      ENEMY *e = &enemy[j];
      if(e->type && Collide(ex, ey, 5, 5, e->x >> 4, e->y >> 4, 8, 8)) EnemyDamage(e, 1);
    }
  }
}

// ===================================================================================
// Enemies
// ===================================================================================

// Attack waves of the stage
void WavesUpdate(void) {
  uint8_t k;

  // Next wave
  if(waveIdx < waveEnd) {
    if(waveTimer) waveTimer--;
    else {
      const uint8_t *w = &WAVES[waveIdx * 3];
      k = spawnLeft[0] ? 1 : 0;
      spawnType[k] = w[1] >> 4; spawnLeft[k] = w[1] & 15; spawnX[k] = w[2]; spawnTimer[k] = 0;
      waveIdx++;
      waveTimer = (waveIdx < waveEnd) ? WAVES[waveIdx * 3] : 0;
      if(waveTimer > 6 && loop) waveTimer -= 6;    // faster in the next loop
      waveTimer <<= 2;
      if(waveTimer < 40 && waveIdx < waveEnd) waveTimer = 40;
    }
  }
  else if(!bossState && !spawnLeft[0] && !spawnLeft[1] && (scrollPos >> 3) >= wallLen - 9) {
    for(k = 0; k < MAX_ENEMIES; k++) if(enemy[k].type) return;
    bossState = B_ENTER;                          // all waves done: the guardian comes
    bossX = ((PF_W >> 1) - 12) << 4; bossY = -16 << 4;
    bossMaxHP = bossHP = BOSS_HP[stage] + (loop << 5);
    bossT = 0; bossAtk = 0;
    ShowMessage("WARNING!", 0, 70);
    Beep(60, 40); Beep(40, 40); Beep(60, 40); Beep(40, 40);
  }

  // Spawn the enemies of the running waves one after the other
  for(k = 0; k < 2; k++) {
    if(!spawnLeft[k]) continue;
    if(spawnTimer[k]) {spawnTimer[k]--; continue;}
    uint8_t t = spawnType[k];
    int16_t x = spawnX[k];
    if(t == E_ROCK || t == E_DIVER) x = 4 + JOY_random() % (PF_W - 16);
    EnemySpawn(t, x);
    spawnLeft[k]--;
    spawnTimer[k] = (t == E_DRONE || t == E_WEAVER) ? 8 : (t == E_TURRET_L || t == E_TURRET_R) ? 40 : 20;
  }
}

NOINLINE void EnemySpawn(uint8_t type, int16_t x) {
  for(uint8_t i = 0; i < MAX_ENEMIES; i++) {
    ENEMY *e = &enemy[i];
    if(e->type) continue;
    e->type = type; e->hp = E_HP[type] + (loop << 1); e->t = 0; e->flash = 0;
    e->x = x << 4; e->y = -8 << 4; e->dx = 0; e->dy = 20;
    switch(type) {
      case E_WEAVER:  e->dy = 12; break;
      case E_DIVER:   e->dy = 24; break;
      case E_CHASER:  e->y = 64 << 4; e->dy = -18; break;        // comes from behind
      case E_TURRET_L:
      case E_TURRET_R: e->dy = 8; break;                         // sits on the wall
      case E_SPINNER: e->dy = 10; break;
      case E_ROCK:    e->dy = 14; e->dx = (JOY_random() & 15) - 8; break;
      case E_POD:     e->dy = 10; break;
    }
    e->bx = e->x;
    return;
  }
}

NOINLINE void EnemyDamage(ENEMY *e, uint8_t damage) {
  e->flash = 4;
  if(e->hp > damage) {e->hp -= damage; Beep(180, 2); return;}
  int16_t x = e->x >> 4, y = e->y >> 4;
  AddScore(E_POINTS[e->type] * 10);
  Explode(x, y);
  noise = 5;
  if(e->type == E_POD) ItemDrop(x, y, I_WEAPON);
  else if(e->type == E_SPINNER || e->type == E_ROCK) ItemDrop(x, y, I_BIGCOIN);
  else if(!(JOY_random() % 3)) ItemDrop(x, y, I_COIN);
  e->type = E_NONE;
}

void EnemiesUpdate(void) {
  uint8_t fireRate = 90 - stage * 10 - (loop ? 20 : 0);
  int16_t sx = (shipX >> 4) + 3, sy = (shipY >> 4) + 4;
  for(uint8_t i = 0; i < MAX_ENEMIES; i++) {
    ENEMY *e = &enemy[i];
    if(!e->type) continue;
    e->t++;
    if(e->flash) e->flash--;
    int16_t x = e->x >> 4, y = e->y >> 4;

    switch(e->type) {
      case E_DRONE:                               // straight down, shoots now and then
        if(y > 4 && y < 40 && !(JOY_random() % fireRate)) EnemyFireAimed(x + 3, y + 6, 20);
        break;
      case E_WEAVER:                              // waves from side to side
        e->x = e->bx + (SINE[(e->t >> 1) & 31] << 4);
        break;
      case E_DIVER:                               // stops, aims and dives at the ship
        if(e->t == 20) {e->dy = 0; e->dx = 0;}
        if(e->t == 35) {
          int16_t dx = sx - x - 4, dy = sy - y - 4;
          int16_t m = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
          if(m) {e->dx = dx * 40 / m; e->dy = dy * 40 / m;}
          if(e->dy < 16) e->dy = 16;
          Beep(120, 4);
        }
        break;
      case E_CHASER:                              // from behind, follows the ship
        if(x + 4 < sx) e->dx = 8;
        else if(x + 4 > sx) e->dx = -8;
        break;
      case E_TURRET_L:                            // sits on the wall, shoots at the ship
      case E_TURRET_R:
        if(y >= 0 && y < 64) {
          e->x = (e->type == E_TURRET_L) ? (wallL[y + (y < 60 ? 4 : 0)] - 2) << 4 :
                 (PF_W - wallR[y + (y < 60 ? 4 : 0)] - 5) << 4;
          if(e->t > 20 && !(e->t % (fireRate >> 1)) && y < 48) EnemyFireAimed(x + 3, y + 4, 22);
        }
        break;
      case E_SPINNER:                             // stops, fires rotating volleys, leaves
        if(e->t == 40) e->dy = 0;
        if(e->t == 200) e->dy = 16;
        if(e->t > 40 && !(e->t % 16)) {
          static const int8_t DX[8] = {0, 14, 20, 14, 0, -14, -20, -14};
          uint8_t k = (e->t >> 4) & 1;
          for(uint8_t d = k; d < 8; d += 2) EnemyFire(x + 3, y + 3, DX[d], DX[(d + 6) & 7]);
          Beep(90, 4);
        }
        break;
      case E_ROCK:                                // bounces off the walls
        if(HitsWall(x, y + 2, 8, 4)) e->dx = -e->dx;
        break;
      case E_POD:                                 // carries a capsule
        e->x = e->bx + (SINE[e->t & 31] << 2);
        break;
    }

    e->x += e->dx; e->y += e->dy;
    x = e->x >> 4; y = e->y >> 4;
    if(y > 66 || y < -20 || x < -10 || x > PF_W + 2) {e->type = E_NONE; continue;}

    // Collision with the ship
    if(!deadTimer && Collide(sx - 2, sy - 2, 5, 5, x, y, 8, 8)) {
      PlayerHit(2);
      if(e->type != E_SPINNER) EnemyDamage(e, 9);
    }
  }
}

// ===================================================================================
// Enemy Shots
// ===================================================================================
NOINLINE void EnemyFire(int16_t x, int16_t y, int8_t dx, int8_t dy) {
  for(uint8_t i = 0; i < MAX_ESHOTS; i++) {
    if(eshotOn[i]) continue;
    eshotOn[i] = 1;
    eshot[i].x = x << 4; eshot[i].y = y << 4; eshot[i].dx = dx; eshot[i].dy = dy;
    return;
  }
}

// Shot towards the ship
NOINLINE void EnemyFireAimed(int16_t x, int16_t y, uint8_t speed) {
  int16_t dx = (shipX >> 4) + 3 - x, dy = (shipY >> 4) + 4 - y;
  int16_t m = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
  if(!m || deadTimer) return;
  speed += stage * 2;
  EnemyFire(x, y, dx * speed / m, dy * speed / m);
}

void EnemyShotsUpdate(void) {
  int16_t sx = (shipX >> 4) + 1, sy = (shipY >> 4) + 2;
  uint8_t el = ((weapons & W_ELECTRO) || nashwan) && !deadTimer, dr = ((weapons & W_DRONES) || nashwan);
  uint8_t k = (frame >> 1) & 15;
  for(uint8_t i = 0; i < MAX_ESHOTS; i++) {
    if(!eshotOn[i]) continue;
    ESHOT *s = &eshot[i];
    s->x += s->dx; s->y += s->dy;
    int16_t x = s->x >> 4, y = s->y >> 4;
    if(x < 0 || x >= PF_W || y < 0 || y > 63) {eshotOn[i] = 0; continue;}

    // Electro ball and drones block the shots
    if(el && Collide(x, y, 2, 2, sx + ORBIT_X[k], sy + ORBIT_Y[k], 5, 5)) {eshotOn[i] = 0; continue;}
    if(dr && (Collide(x, y, 2, 2, satX[0], satY[0], 5, 5) || Collide(x, y, 2, 2, satX[1], satY[1], 5, 5))) {
      eshotOn[i] = 0; continue;
    }

    // Ship
    if(!deadTimer && Collide(x, y, 2, 2, sx + 1, sy + 1, 3, 3)) {
      eshotOn[i] = 0;
      PlayerHit(1);
    }
  }
}

// ===================================================================================
// Guardian at the End of Each Stage
// ===================================================================================
void BossUpdate(void) {
  int16_t x = bossX >> 4, y = bossY >> 4;
  switch(bossState) {
    case B_ENTER:                                 // comes down from the top
      bossY += 8;
      if(bossY >= (4 << 4)) {bossState = B_FIGHT; bossT = 0;}
      break;

    case B_FIGHT:
      bossT++;
      // Movement: sideways, faster in later stages; dives down when hurt
      bossX = (((PF_W >> 1) - 12) << 4) + SINE[(bossT * BOSS_SPEED[stage] >> 2) & 31] * 40;
      bossY = (4 << 4) + ((bossHP < (bossMaxHP >> 1)) ? (SINE[(bossT >> 2) & 31] + 16) << 3 : 0);

      // Attacks: aimed burst, fan, ring, rain and escort drones
      if(!(bossT % (36 - stage * 3))) {
        bossAtk++;
        switch((bossAtk + stage) % 5) {
          case 0:
            EnemyFireAimed(x + 6, y + 14, 22); EnemyFireAimed(x + 17, y + 14, 22);
            break;
          case 1:
            for(int8_t d = -2; d <= 2; d++) EnemyFire(x + 11, y + 15, d * 8, 20);
            break;
          case 2:
            if(stage) {
              static const int8_t RX[8] = {0, 14, 20, 14, 0, -14, -20, -14};
              for(uint8_t d = 0; d < 8; d++) EnemyFire(x + 11, y + 8, RX[d], RX[(d + 6) & 7]);
              break;
            }
          case 3:
            for(uint8_t d = 0; d < 4; d++) EnemyFire(x + (JOY_random() % 22), y + 15, 0, 16 + stage * 2);
            break;
          default:
            if(stage >= 2) {EnemySpawn(E_DRONE, x - 6); EnemySpawn(E_DRONE, x + 22);}
            else EnemyFireAimed(x + 11, y + 15, 26);
            break;
        }
        Beep(70, 6);
      }
      if(!deadTimer && Collide((shipX >> 4) + 1, (shipY >> 4) + 2, 5, 5, x + 2, y, 20, 15)) PlayerHit(2);
      break;

    case B_DYING:                                 // chain of explosions
      bossT++;
      if(!(bossT & 3)) {
        Explode(x + (JOY_random() % 18), y + (JOY_random() & 7));
        noise = 4;
      }
      if(bossT >= 70) {
        bossState = B_DONE;
        AddScore(5000 * (stage + 1));
        credits += 500;
        ShowMessage("STAGE CLEAR", 0, 90);
        Beep(150, 80); Beep(200, 80); Beep(250, 120);
        bossT = 100;
      }
      break;

    default:                                      // stage clear: wait for the message
      if(bossT && --bossT == 0) stageClear = 1;
      break;
  }
}

NOINLINE void BossDamage(uint8_t damage) {
  Beep(160, 2);
  if(bossHP > damage) {bossHP -= damage; return;}
  bossHP = 0; bossState = B_DYING; bossT = 0;
  for(uint8_t i = 0; i < MAX_ESHOTS; i++) eshotOn[i] = 0;
}

// ===================================================================================
// Credit Bubbles and Capsules
// ===================================================================================
NOINLINE void ItemDrop(int16_t x, int16_t y, uint8_t type) {
  for(uint8_t i = 0; i < MAX_ITEMS; i++) {
    ITEM *it = &item[i];
    if(it->type) continue;
    if(type == I_WEAPON) {                        // pods carry weapons, energy or power
      uint8_t r = JOY_random() % 10;
      type = (r < 6) ? I_WEAPON : (r < 9) ? I_ENERGY : I_NASHWAN;
    }
    it->type = type; it->x = x << 4; it->y = y << 4; it->t = 0;
    return;
  }
}

// Capsule: the next weapon the ship does not have yet
void GiveWeapon(void) {
  static const char *const NAME[8] = {"SIDE SHOT", "REAR SHOT", "HOMING", "LASER", "CANNON", "DRONES", "ELECTRO", "AUTOFIRE"};
  uint8_t r = JOY_random() & 7;
  for(uint8_t k = 0; k < 8; k++, r = (r + 1) & 7) {
    if(!(weapons & (1 << r))) {weapons |= 1 << r; ShowMessage(NAME[r], 0, 50); return;}
  }
  if(gunLevel < 3) {gunLevel++; ShowMessage("GUN POWER", 0, 50); return;}
  credits += 200; ShowMessage("CREDITS", 200, 50);
}

void ItemsUpdate(void) {
  for(uint8_t i = 0; i < MAX_ITEMS; i++) {
    ITEM *it = &item[i];
    if(!it->type) continue;
    it->t++;
    it->y += 10;
    it->x += SINE[(it->t >> 1) & 31] >> 1;
    int16_t x = it->x >> 4, y = it->y >> 4;
    if(y > 64) {it->type = I_NONE; continue;}
    if(!deadTimer && Collide((shipX >> 4), (shipY >> 4), 7, 8, x, y, 7, 7)) {
      switch(it->type) {
        case I_COIN:    credits += 10; break;
        case I_BIGCOIN: credits += 50; break;
        case I_WEAPON:  GiveWeapon(); break;
        case I_ENERGY:  energy = ENERGY_MAX; ShowMessage("ENERGY", 0, 40); break;
        case I_NASHWAN: nashwan = NASHWAN_TIME; ShowMessage("NASHWAN POWER", 0, 50); break;
      }
      if(credits > 9999) credits = 9999;
      AddScore(50);
      it->type = I_NONE;
      Beep(220, 5); Beep(250, 5);
    }
  }
}

// ===================================================================================
// Explosions
// ===================================================================================
NOINLINE void Explode(int16_t x, int16_t y) {
  for(uint8_t i = 0; i < MAX_EXPL; i++) {
    if(!boom[i].t) {boom[i].x = x; boom[i].y = y; boom[i].t = 1; return;}
  }
}

// ===================================================================================
// Rendering
// ===================================================================================

// Put one byte column into the page buffer at pixel row y (with a mask around it)
NOINLINE void DrawColumn(uint8_t page, int16_t x, int16_t y, uint8_t b, uint8_t limit) {
  int16_t row = y - (page << 3);
  if(x < 0 || x >= limit || row <= -8 || row >= 8) return;
  uint16_t m = b | (b << 1) | (b >> 1);
  if(row >= 0) {buf[x] &= ~(m << row); buf[x] |= b << row;}
  else         {buf[x] &= ~(m >> -row); buf[x] |= b >> -row;}
}

// Sprite with w columns and h rows (h = 8 or 16), clipped to the playfield
NOINLINE void DrawSprite(uint8_t page, int16_t x, int16_t y, uint8_t w, const uint8_t *bmp) {
  if(y - (page << 3) <= -8 || y - (page << 3) >= 8) return;
  for(uint8_t i = 0; i < w; i++) DrawColumn(page, x + i, y, bmp[i], clipW);
}

void DrawSprite16(uint8_t page, int16_t x, int16_t y, uint8_t w, const uint8_t *bmp) {
  DrawSprite(page, x, y, w, bmp);
  DrawSprite(page, x, y + 8, w, bmp + w);
}

// Small rectangle (shots)
NOINLINE void DrawBox(uint8_t page, int16_t x, int16_t y, uint8_t w, uint8_t h) {
  int16_t row = y - (page << 3);
  if(row <= -8 || row >= 8) return;
  uint8_t b = (1 << h) - 1;
  for(uint8_t i = 0; i < w; i++) {
    int16_t cx = x + i;
    if(cx < 0 || cx >= clipW) continue;
    buf[cx] |= (row >= 0) ? (b << row) : (b >> -row);
  }
}

// 5x7 font, scale 1 or 2
NOINLINE void DrawText(uint8_t page, int16_t x, int16_t y, const char *s, uint8_t scale) {
  for(; *s; s++) {
    uint8_t c = 0;
    while(FONT_CHARS[c] && FONT_CHARS[c] != *s) c++;
    const uint8_t *g = &FONT[c * 5];
    for(uint8_t i = 0; i < 5; i++) {
      if(scale == 1) DrawColumn(page, x + i, y, g[i], 128);
      else {
        uint16_t w = 0;                           // double every bit
        for(uint8_t b = 0; b < 8; b++) if(g[i] & (1 << b)) w |= 3 << (b << 1);
        for(uint8_t k = 0; k < 2; k++) {
          DrawColumn(page, x + (i << 1) + k, y, w, 128);
          DrawColumn(page, x + (i << 1) + k, y + 8, w >> 8, 128);
        }
      }
    }
    x += 6 * scale;
  }
}

// Text centered at cx
void DrawTextC(uint8_t page, int16_t cx, int16_t y, const char *s, uint8_t scale) {
  uint8_t len = 0;
  while(s[len]) len++;
  DrawText(page, cx - len * 3 * scale, y, s, scale);
}

// Number with a fixed count of digits: 3x5 digits (big = 0) or 5x7 font (big = 1)
NOINLINE void DrawNumber(uint8_t page, int16_t x, int16_t y, uint32_t n, uint8_t digits, uint8_t big) {
  char s[8];
  s[digits] = 0;
  while(digits--) {s[digits] = '0' + n % 10; n /= 10;}
  if(big == 2) for(char *p = s; *p == '0' && p[1]; p++) *p = ' ';   // no leading zeros
  if(big) {DrawText(page, x, y, s, 1); return;}
  for(char *p = s; *p; p++, x += 4) {
    const uint8_t *g = &DIGITS[(*p - '0') * 3];
    for(uint8_t i = 0; i < 3; i++) DrawColumn(page, x + i, y, g[i], 128);
  }
}

// Small 5x5 icon in the status panel
NOINLINE void DrawIcon(uint8_t page, int16_t x, int16_t y, uint8_t n) {
  for(uint8_t i = 0; i < 5; i++) DrawColumn(page, x + i, y, ICONS[n * 5 + i], 128);
}

// Walls: dithered with ribs and a solid edge, scrolling with the background
void DrawWalls(uint8_t page) {
  for(uint8_t r = 0; r < 8; r++) {
    uint8_t y = (page << 3) + r, bit = 1 << r, x;
    uint8_t l = wallL[y], rw = wallR[y];
    uint16_t w = scrollPos + 63 - y;
    uint8_t rib = !(w & 7), dot = (w & 1) ? ((w >> 1) & 3) : 9;
    for(x = 0; x < l; x++)
      if(x + 2 >= l || rib || ((x & 3) == dot)) buf[x] |= bit;
    for(x = PF_W - rw; x < PF_W; x++)
      if(x <= PF_W - rw + 1 || rib || ((x & 3) == dot)) buf[x] |= bit;
  }
}

// Status panel right of the playfield
void DrawPanel(uint8_t page) {
  uint8_t i;
  buf[PF_W] = 0xFF;                               // separator line
  switch(page) {
    case 0: DrawNumber(0, 105, 1, score, 6, 0); break;
    case 1:
      for(i = 0; i < 5; i++) DrawColumn(1, 105 + i, 9, SPR_COIN[i], 128);
      DrawNumber(1, 112, 9, credits, 4, 0);
      break;
    case 2:
      for(i = 0; i < lives && i < 3; i++)
        for(uint8_t k = 0; k < 5; k++) DrawColumn(2, 106 + i * 6 + k, 18, SPR_LIFE[k], 128);
      DrawNumber(2, 124, 17, stage + 1, 1, 0);
      break;
    case 3:                                       // energy bar
      for(i = 0; i < 22; i++) {
        uint8_t b = (i == 0 || i == 21) ? 0x3E : 0x22;
        if(i && i < 21 && i <= energy * 20 / ENERGY_MAX) b = 0x3E;
        buf[106 + i] |= b;
      }
      break;
    default: {                                    // weapons
      uint8_t blink = nashwan && (frame & 4);
      uint8_t w = (nashwan && !blink) ? 0xFF : weapons;
      uint8_t row = page - 4;                     // icons 0..3 in row 0, ...
      for(i = 0; i < 4; i++) {
        uint8_t n = row * 4 + i;
        if(n > 11) break;
        uint8_t on = 0;
        if(n == 0) on = 1;                                    // gun
        else if(n <= 7) on = w & (1 << (n - 1));              // side .. electro
        else if(n == 8) on = speedLevel;                      // speed
        else if(n == 9) on = w & W_AUTO;                      // autofire
        else if(n == 10) on = nashwan && !blink;              // nashwan
        else {DrawNumber(page, 107 + i * 6, (page << 3) + 1, (nashwan ? 3 : gunLevel) + 1, 1, 0); continue;}
        if(on) DrawIcon(page, 106 + i * 6, (page << 3) + 1, n);
      }
    }
  }
}

// Shop list
void DrawShop(uint8_t page) {
  if(page == 0) {
    DrawText(0, 0, 0, "SHOP", 1);
    DrawText(0, 78, 0, "$", 1);
    DrawNumber(0, 98, 0, credits, 4, 2);
    for(uint8_t i = 0; i < 128; i += 2) buf[i] |= 0x80;
    return;
  }
  if(page == 7) {
    if(shopSel < 10 || shopSel == 12) DrawIcon(7, 0, 57, shopSel < 10 ? shopSel : 10);
    DrawText(7, 10, 57, "FIRE - BUY", 1);
    return;
  }
  uint8_t n = shopTop + page - 1;
  int16_t y = page << 3;
  if(n == shopSel && (frame & 8)) DrawText(page, 0, y, ">", 1);
  DrawText(page, 8, y, SHOP_NAME[n], 1);
  uint8_t owned = 0;
  if(n == 0) owned = (gunLevel >= 3);
  else if(n <= 7 || n == 9) owned = (weapons & SHOP_BIT(n)) != 0;
  else if(n == 8) owned = (speedLevel >= 2);
  else if(n == 12) owned = (nashwanStock != 0);
  if(n == 13) return;
  if(owned) DrawText(page, 98, y, "OK", 1);
  else DrawNumber(page, 98, y, SHOP_PRICE[n], 4, 2);
}

// Draw the whole screen page by page and send it to the OLED
void Tiny_Flip(uint8_t mode) {
  uint8_t page, i;
  clipW = (mode == R_GAME) ? PF_W : 128;
  for(page = 0; page < 8; page++) {
    for(i = 0; i < 128; i++) buf[i] = 0;

    if(mode == R_SHOP) DrawShop(page);
    else {
      // Stars
      for(i = 0; i < MAX_STARS; i++)
        if((starY[i] >> 3) == page) buf[starX[i]] |= 1 << (starY[i] & 7);

      if(mode == R_GAME) {
        DrawWalls(page);

        // Items
        for(i = 0; i < MAX_ITEMS; i++) {
          ITEM *it = &item[i];
          if(!it->type) continue;
          int16_t x = it->x >> 4, y = it->y >> 4;
          if(it->type <= I_BIGCOIN) DrawSprite(page, x + 1, y + 1, 5, SPR_COIN);
          else DrawSprite(page, x, y, 7, it->type == I_WEAPON ? SPR_CAPSULE :
                          it->type == I_ENERGY ? SPR_ENERGY : SPR_NASHWAN);
        }

        // Enemies (blink when hit)
        for(i = 0; i < MAX_ENEMIES; i++) {
          ENEMY *e = &enemy[i];
          if(!e->type || (e->flash & 2)) continue;
          const uint8_t *spr = 0; uint8_t w = 7;
          uint8_t f = (e->t & 8) ? 1 : 0;
          switch(e->type) {
            case E_DRONE:   spr = SPR_DRONE; break;
            case E_WEAVER:  spr = SPR_WEAVER + f * 7; break;
            case E_DIVER:   spr = SPR_DIVER; w = 8; break;
            case E_CHASER:  spr = SPR_CHASER; w = 8; break;
            case E_TURRET_L:
            case E_TURRET_R: spr = SPR_TURRET + f * 7; break;
            case E_SPINNER: spr = SPR_SPINNER + f * 8; w = 8; break;
            case E_ROCK:    spr = SPR_ROCK; w = 8; break;
            case E_POD:     spr = SPR_POD; break;
          }
          DrawSprite(page, e->x >> 4, e->y >> 4, w, spr);
        }

        // Guardian
        if(bossState >= B_ENTER && bossState <= B_DYING && !(bossState == B_DYING && (bossT & 2)))
          DrawSprite16(page, bossX >> 4, bossY >> 4, 24, &SPR_BOSS[stage * 48]);

        // Player shots
        for(i = 0; i < MAX_SHOTS; i++) {
          SHOT *s = &shot[i];
          switch(s->type) {
            case S_GUN:    DrawBox(page, s->x, s->y, 1, 3); break;
            case S_SIDE:   DrawBox(page, s->x, s->y, 3, 1); break;
            case S_REAR:   DrawBox(page, s->x, s->y, 1, 2); break;
            case S_HOMING: DrawBox(page, s->x, s->y, 2, 2); DrawBox(page, s->x + 1, s->y + 2, 1, 1); break;
            case S_LASER:  DrawBox(page, s->x, s->y, 1, 8); break;
            case S_CANNON: DrawBox(page, s->x + 1, s->y, 2, 4); DrawBox(page, s->x, s->y + 1, 4, 2); break;
          }
        }

        // Enemy shots: small diamonds
        for(i = 0; i < MAX_ESHOTS; i++) {
          if(!eshotOn[i]) continue;
          int16_t x = eshot[i].x >> 4, y = eshot[i].y >> 4;
          DrawBox(page, x, y, 2, 2);
        }

        // Ship, drones and electro ball (blink while invulnerable)
        if(!deadTimer && !(invuln & 2)) {
          int16_t x = shipX >> 4, y = shipY >> 4;
          DrawSprite(page, x, y, 7, SPR_SHIP);
          if((weapons & W_DRONES) || nashwan)
            for(i = 0; i < 2; i++) DrawSprite(page, satX[i], satY[i], 5, SPR_SAT);
          if((weapons & W_ELECTRO) || nashwan) {
            uint8_t k = (frame >> 1) & 15;
            DrawSprite(page, x + 1 + ORBIT_X[k], y + 2 + ORBIT_Y[k], 5, SPR_ELECTRO + ((frame & 2) ? 5 : 0));
          }
        }

        // Explosions
        for(i = 0; i < MAX_EXPL; i++) {
          if(!boom[i].t) continue;
          DrawSprite(page, boom[i].x, boom[i].y, 8, SPR_EXPL + ((boom[i].t - 1) / 3) * 8);
        }

        // Guardian energy bar
        if(page == 0 && bossState == B_FIGHT) {
          uint8_t w = (uint16_t)bossHP * 60 / bossMaxHP;
          for(i = 0; i < 62; i++) buf[21 + i] = (buf[21 + i] & 0xF0) | ((i == 0 || i == 61 || i <= w) ? 0x07 : 0x05);
        }

        // Message
        if(msgTimer && (msgTimer > 20 || (msgTimer & 4))) {
          DrawTextC(page, PF_W >> 1, 28, msgText, 1);
          if(msgNum) DrawNumber(page, (PF_W >> 1) - 12, 37, msgNum, 4, 1);
        }
        DrawPanel(page);
      }

      // Title and other screens
      else if(mode == R_TITLE) {
        DrawTextC(page, 64, 2, "TINY", 2);
        DrawTextC(page, 64, 20, "BLASTER", 2);
        DrawSprite(page, 6, 42 - ((frame >> 2) & 7), 7, SPR_SHIP);
        DrawSprite(page, 114, 30 + ((frame >> 2) & 7), 7, SPR_SHIP);
        if(frame & 16) DrawTextC(page, 64, 41, "PRESS FIRE", 1);
        DrawText(page, 31, 55, "HI", 1);
        DrawNumber(page, 49, 55, hiscore, 7, 1);
      }
      else if(mode == R_STAGE) {
        if(loop) DrawTextC(page, 64, 10, "LOOP 2", 1);
        DrawText(page, 37, 24, "STAGE", 1);
        DrawNumber(page, 79, 24, stage + 1, 1, 1);
        DrawTextC(page, 64, 38, "GET READY", 1);
      }
      else if(mode == R_GAMEOVER) {
        DrawTextC(page, 64, 16, "GAME OVER", 2);
        if(frame & 16) {
          DrawText(page, 25, 42, "SCORE", 1);
          DrawNumber(page, 61, 42, score, 7, 1);
        }
      }
      else {
        DrawTextC(page, 64, 10, "WELL DONE!", 2);
        DrawTextC(page, 64, 32, "ALL GUARDIANS", 1);
        DrawTextC(page, 64, 42, "DESTROYED", 1);
      }
    }

    // Send page to OLED
    JOY_OLED_data_start(page);
    for(i = 0; i < 128; i++) JOY_OLED_send(buf[i]);
    JOY_OLED_end();
  }
}
