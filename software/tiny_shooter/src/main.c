// ===================================================================================
// Project:   Tiny Shooter - Horizontal Scrolling Shoot'em Up for CH32V003
// Version:   v1.0
// Year:      2026
// Github:    https://github.com/wagiminator
// EasyEDA:   https://easyeda.com/wagiminator
// License:   http://creativecommons.org/licenses/by-sa/3.0/
// ===================================================================================
//
// Description:
// ------------
// A small horizontally scrolling shoot'em up for the CH32V003 Mini Game Console.
// Fly through open space, over hills and through caves, shoot down waves of
// enemies, collect power capsules for a stronger gun and defeat the guardian at
// the end of each stage. The stages get faster and more crowded.
//
// Controls:
// ---------
// Joypad:      move the ship (also diagonally)
// Fire button: shoot (hold for rapid fire), start game
//
// Scoring:
// --------
// Dart 10, Wave 20, Turret 40, Gunner 50, Mine 60, Capsule 100,
// Guardian 500 x stage, stage bonus 1000 x stage. Extra ship every 5000 points.
//
// Compilation Settings:
// ---------------------
// - Make sure GCC toolchain (gcc-riscv64-unknown-elf, newlib) and Python3 with rvprog
//   are installed. In addition, Linux requires access rights to WCH-LinkE programmer.
// - Connect the WCH-LinkE programmer to the MCU board.
// - Run 'make flash'.


#include <driver.h>           // TinyJoypad conversion driver
#include <spritebank.h>       // grafix

// ===================================================================================
// Game Settings
// ===================================================================================
#define FRAME_TIME    (33 * DLY_MS_TIME)  // ~30 frames per second
#define TOP           8       // first pixel row of the playfield (row 0..7: score line)
#define SHIP_W        8       // ship width
#define SHIP_H        7       // ship height
#define MAX_ENEMIES   8       // enemies on screen at the same time
#define MAX_PSHOTS    6       // player shots
#define MAX_ESHOTS    6       // enemy shots
#define MAX_EXPL      6       // explosions
#define MAX_STARS     16      // background stars
#define STAGE_LEN     1500    // frames until the guardian appears (~50 seconds)
#define LIVES         3       // ships at game start
#define EXTRA_LIFE    5000    // points for an extra ship

// Enemy types
enum {E_NONE, E_DART, E_WAVE, E_GUNNER, E_TURRET, E_MINE};

// Terrain sections
enum {T_OPEN, T_HILLS, T_CAVE};

// Guardian states
enum {B_NONE, B_WAIT, B_ENTER, B_FIGHT, B_DYING};

// Render modes
enum {R_TITLE, R_GAME, R_GAMEOVER};

// ===================================================================================
// Game Objects
// ===================================================================================
typedef struct {
  int16_t x;                  // position
  int8_t  y;
  uint8_t type;               // E_NONE: free slot
  uint8_t hp;                 // hit points
  uint8_t t;                  // timer for animation and movement
  int8_t  base;               // start row (wave enemies)
} ENEMY;

typedef struct {
  int16_t x;                  // position
  int8_t  y;
  int8_t  dy;                 // vertical speed (spread shot)
  uint8_t on;
} PSHOT;

typedef struct {
  int16_t x, y;               // position in 1/16 pixels
  int8_t  dx, dy;             // speed in 1/16 pixels per frame
  uint8_t on;
} ESHOT;

typedef struct {
  int16_t x;
  int8_t  y;
  uint8_t t;                  // animation frame, 0: free
} EXPL;

ENEMY    enemy[MAX_ENEMIES];
PSHOT    pshot[MAX_PSHOTS];
ESHOT    eshot[MAX_ESHOTS];
EXPL     boom[MAX_EXPL];
uint8_t  starX[MAX_STARS], starY[MAX_STARS];
uint8_t  floorH[128], ceilH[128];   // terrain heights (ring buffers)
uint8_t  buf[128];                  // one display page

// Player
int16_t  shipX;
int8_t   shipY;
uint8_t  lives, power, invuln, deadTimer, fireCool;
uint32_t score, hiscore, nextLife;

// Game progress
uint8_t  stage, frame, gameOver;
uint16_t progress;
uint8_t  spawnTimer, waveLeft, waveType, waveY;
uint8_t  noise;                     // remaining frames of explosion noise

// Terrain generator
uint8_t  tpos, terrMode, terrLen, terrFloor, terrCeil, terrFT, terrCT;

// Guardian
uint8_t  bossState, bossHP, bossMaxHP, bossT;
int16_t  bossX;
int8_t   bossY, bossDY;

// Power capsule
uint8_t  capOn;
int16_t  capX;
int8_t   capY;

// Message in the middle of the screen
const char *msgText;
uint8_t  msgNum, msgTimer;

// Movement table for wave enemies
const int8_t SINE[32] = {
  0, 2, 4, 6, 8, 9, 10, 11, 12, 11, 10, 9, 8, 6, 4, 2,
  0, -2, -4, -6, -8, -9, -10, -11, -12, -11, -10, -9, -8, -6, -4, -2
};

// ===================================================================================
// Function Prototypes
// ===================================================================================
void TitleScreen(void);
void GameOverScreen(void);
void NewGame(void);
void UpdateGame(void);
void Tiny_Flip(uint8_t mode);
void WaitFrame(uint32_t start);
void InitStars(void);
void MoveStars(uint8_t speed);
void TerrainInit(void);
void TerrainStep(void);
uint8_t FloorAt(int16_t x);
uint8_t CeilAt(int16_t x);
void PlayerUpdate(void);
void PlayerFire(void);
void PlayerHit(void);
void PlayerRespawn(void);
void ShotsUpdate(void);
void EnemySpawner(void);
void EnemySpawn(uint8_t type, int8_t y);
void EnemiesUpdate(void);
void EnemyFire(int16_t x, int8_t y, int8_t dx, int8_t dy);
void EnemyFireAimed(int16_t x, int8_t y);
void EnemyShotsUpdate(void);
void BossUpdate(void);
void Explode(int16_t x, int8_t y);
void AddScore(uint32_t points);
void ShowMessage(const char *text, uint8_t num, uint8_t time);
uint8_t Collide(int16_t x1, int8_t y1, uint8_t w1, uint8_t h1, int16_t x2, int8_t y2, uint8_t w2, uint8_t h2);

// ===================================================================================
// Main Function
// ===================================================================================
int main(void) {
  // Setup
  JOY_init();
  InitStars();

  // Loop
  while(1) {
    TitleScreen();
    NewGame();
    while(!gameOver) {
      uint32_t start = STK->CNT;
      UpdateGame();
      Tiny_Flip(R_GAME);
      WaitFrame(start);
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
    MoveStars(1);
    Tiny_Flip(R_TITLE);
    frame++;
    if(JOY_act_pressed()) break;
    JOY_random();                                 // stir the random generator
    WaitFrame(start);
  }
  JOY_sound(100, 60); JOY_sound(150, 60); JOY_sound(200, 100);
}

// Game over: show the score until the fire button is pressed
void GameOverScreen(void) {
  if(score > hiscore) hiscore = score;
  for(frame = 0; frame < 90; frame++) {           // let the explosion fade
    uint32_t start = STK->CNT;
    MoveStars(1);
    for(uint8_t i = 0; i < MAX_EXPL; i++) if(boom[i].t && ++boom[i].t > 9) boom[i].t = 0;
    Tiny_Flip(R_GAMEOVER);
    WaitFrame(start);
  }
  JOY_sound(120, 100); JOY_sound(90, 100); JOY_sound(60, 200);
  while(JOY_act_released()) {
    uint32_t start = STK->CNT;
    MoveStars(1);
    frame++;
    Tiny_Flip(R_GAMEOVER);
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
  uint8_t i;
  for(i = 0; i < MAX_ENEMIES; i++) enemy[i].type = E_NONE;
  for(i = 0; i < MAX_PSHOTS; i++)  pshot[i].on = 0;
  for(i = 0; i < MAX_ESHOTS; i++)  eshot[i].on = 0;
  for(i = 0; i < MAX_EXPL; i++)    boom[i].t = 0;
  score = 0; nextLife = EXTRA_LIFE;
  lives = LIVES; power = 0; deadTimer = 0; fireCool = 0;
  stage = 1; progress = 0; gameOver = 0; noise = 0;
  spawnTimer = 60; waveLeft = 0;
  bossState = B_NONE; capOn = 0;
  TerrainInit();
  PlayerRespawn();
  ShowMessage("STAGE", stage, 60);
}

// One frame of the game
void UpdateGame(void) {
  uint8_t i;
  frame++;

  // Scrolling background
  MoveStars(2);
  TerrainStep();

  // Objects
  PlayerUpdate();
  ShotsUpdate();
  EnemySpawner();
  EnemiesUpdate();
  EnemyShotsUpdate();
  BossUpdate();

  // Power capsule
  if(capOn) {
    capX--;
    if(capX < -8) capOn = 0;
    else if(!deadTimer && Collide(shipX, shipY, SHIP_W, SHIP_H, capX, capY, 7, 7)) {
      capOn = 0;
      if(power < 2) power++;
      AddScore(100);
      ShowMessage(power == 1 ? "DOUBLE" : "SPREAD", 0, 40);
      JOY_sound(150, 30); JOY_sound(200, 30); JOY_sound(250, 40);
    }
  }

  // Explosions
  for(i = 0; i < MAX_EXPL; i++) {
    if(boom[i].t) {
      boom[i].x--;                                // drift with the scrolling
      if(++boom[i].t > 9) boom[i].t = 0;
    }
  }

  // Explosion noise
  if(noise) {
    noise--;
    JOY_sound((JOY_random() & 0x3F) + 20, 3);
  }

  // Message timer
  if(msgTimer) msgTimer--;
}

void AddScore(uint32_t points) {
  score += points;
  if(score >= nextLife) {
    nextLife += EXTRA_LIFE;
    if(lives < 9) lives++;
    ShowMessage("EXTRA SHIP", 0, 50);
    JOY_sound(200, 40); JOY_sound(220, 40); JOY_sound(240, 60);
  }
}

void ShowMessage(const char *text, uint8_t num, uint8_t time) {
  msgText = text; msgNum = num; msgTimer = time;
}

// Bounding box collision
uint8_t Collide(int16_t x1, int8_t y1, uint8_t w1, uint8_t h1, int16_t x2, int8_t y2, uint8_t w2, uint8_t h2) {
  return (x1 < x2 + w2) && (x2 < x1 + w1) && (y1 < y2 + h2) && (y2 < y1 + h1);
}

// ===================================================================================
// Stars and Terrain
// ===================================================================================
void InitStars(void) {
  for(uint8_t i = 0; i < MAX_STARS; i++) {
    starX[i] = JOY_random() & 127;
    starY[i] = TOP + (JOY_random() % (64 - TOP));
  }
}

// Even stars move every frame, odd stars every other frame (parallax)
void MoveStars(uint8_t speed) {
  for(uint8_t i = 0; i < MAX_STARS; i++) {
    if((i & 1) && (frame & 1)) continue;
    if(starX[i] < speed) {
      starX[i] = 127;
      starY[i] = TOP + (JOY_random() % (64 - TOP));
    }
    else starX[i] -= speed;
  }
}

void TerrainInit(void) {
  for(uint8_t i = 0; i < 128; i++) floorH[i] = ceilH[i] = 0;
  tpos = 0; terrMode = T_OPEN; terrLen = 100;
  terrFloor = terrCeil = terrFT = terrCT = 0;
}

// Scroll the terrain one column and create the new rightmost column
void TerrainStep(void) {
  uint8_t r;
  tpos = (tpos + 1) & 127;

  // Choose the next section
  if(!terrLen) {
    r = JOY_random() % 8;
    if(bossState || r < 2) terrMode = T_OPEN;
    else if(r < 5)         terrMode = T_HILLS;
    else                   terrMode = T_CAVE;
    terrLen = 96 + (JOY_random() & 127);
  }
  if(!bossState) terrLen--;
  else           terrMode = T_OPEN;               // flat space for the guardian fight

  // New target heights now and then
  if(terrMode == T_OPEN) terrFT = terrCT = 0;
  else if(!(JOY_random() & 15)) {
    terrFT = 2 + JOY_random() % 13;
    terrCT = (terrMode == T_CAVE) ? 2 + JOY_random() % 12 : 0;
    if(terrFT + terrCT > 24) terrCT = 24 - terrFT;  // keep a gap of at least 32 rows
  }

  // Heights follow the targets with gentle slopes
  if(frame & 1) {
    if(terrFloor < terrFT) terrFloor++;
    else if(terrFloor > terrFT) terrFloor--;
    if(terrCeil < terrCT) terrCeil++;
    else if(terrCeil > terrCT) terrCeil--;
  }
  floorH[(tpos + 127) & 127] = terrFloor;
  ceilH[(tpos + 127) & 127]  = terrCeil;
}

// Terrain heights at screen column x
uint8_t FloorAt(int16_t x) {
  if(x < 0 || x > 127) return 0;
  return floorH[(tpos + x) & 127];
}

uint8_t CeilAt(int16_t x) {
  if(x < 0 || x > 127) return 0;
  return ceilH[(tpos + x) & 127];
}

// Does a box touch the terrain?
uint8_t HitsTerrain(int16_t x, int8_t y, uint8_t w, uint8_t h) {
  for(int16_t cx = x; cx < x + w; cx++) {
    if(y + h > 64 - FloorAt(cx)) return 1;
    if(y < TOP + CeilAt(cx)) return 1;
  }
  return 0;
}

// ===================================================================================
// Player
// ===================================================================================
void PlayerRespawn(void) {
  shipX = 8;
  shipY = ((TOP + CeilAt(12) + 64 - FloorAt(12)) >> 1) - 3;  // middle of the gap
  invuln = 90;
}

void PlayerUpdate(void) {
  // Ship destroyed: wait for the explosion, then the next ship
  if(deadTimer) {
    if(--deadTimer == 0) {
      if(--lives == 0) gameOver = 1;
      else PlayerRespawn();
    }
    return;
  }

  // Move
  if(JOY_up_pressed()    && shipY > TOP)             shipY -= 2;
  if(JOY_down_pressed()  && shipY < 64 - SHIP_H)     shipY += 2;
  if(JOY_left_pressed()  && shipX > 0)               shipX -= 2;
  if(JOY_right_pressed() && shipX < 104)             shipX += 2;
  if(shipY < TOP) shipY = TOP;
  if(shipY > 64 - SHIP_H) shipY = 64 - SHIP_H;

  // Fire (hold for rapid fire)
  if(fireCool) fireCool--;
  if(JOY_act_pressed() && !fireCool) {
    PlayerFire();
    fireCool = 5;
  }

  // Invulnerability after a new ship
  if(invuln) invuln--;
  else if(HitsTerrain(shipX + 1, shipY + 1, SHIP_W - 2, SHIP_H - 2)) PlayerHit();
}

void PlayerFire(void) {
  // Shots per volley: single, double or spread
  static const int8_t ROW[3][3] = {{3, 0, 0}, {1, 5, 0}, {3, 3, 3}};
  static const int8_t DY[3][3]  = {{0, 0, 0}, {0, 0, 0}, {0, -1, 1}};
  uint8_t n = power + 1, k = 0;
  for(uint8_t i = 0; i < MAX_PSHOTS && k < n; i++) {
    if(!pshot[i].on) {
      pshot[i].on = 1;
      pshot[i].x  = shipX + 6;
      pshot[i].y  = shipY + ROW[power][k];
      pshot[i].dy = DY[power][k];
      k++;
    }
  }
  if(k) JOY_sound(230, 3);
}

void PlayerHit(void) {
  if(invuln || deadTimer) return;
  deadTimer = 45;
  Explode(shipX, shipY);
  Explode(shipX + 4, shipY - 3);
  Explode(shipX + 2, shipY + 4);
  noise = 12;
  if(power) power--;
}

// ===================================================================================
// Player Shots
// ===================================================================================
void ShotsUpdate(void) {
  uint8_t i, j;
  for(i = 0; i < MAX_PSHOTS; i++) {
    PSHOT *s = &pshot[i];
    if(!s->on) continue;
    s->x += 4;
    s->y += s->dy;
    if(s->x > 127 || s->y < TOP || s->y > 63) {s->on = 0; continue;}
    if(HitsTerrain(s->x, s->y, 4, 1)) {s->on = 0; continue;}

    // Enemies
    for(j = 0; j < MAX_ENEMIES; j++) {
      ENEMY *e = &enemy[j];
      if(e->type == E_NONE) continue;
      if(Collide(s->x, s->y, 4, 1, e->x, e->y, 8, 7)) {
        s->on = 0;
        if(--e->hp == 0) {
          static const uint8_t POINTS[] = {0, 10, 20, 50, 40, 60};
          AddScore(POINTS[e->type]);
          Explode(e->x, e->y);
          noise = 5;
          // Gunners and every tenth enemy leave a power capsule
          if(!capOn && (e->type == E_GUNNER || (JOY_random() % 10) == 0)) {
            capOn = 1; capX = e->x; capY = e->y;
          }
          e->type = E_NONE;
        }
        else JOY_sound(180, 4);
        break;
      }
    }

    // Guardian
    if(s->on && bossState == B_FIGHT && Collide(s->x, s->y, 4, 1, bossX + 2, bossY, 14, 16)) {
      s->on = 0;
      JOY_sound(160, 3);
      if(--bossHP == 0) {bossState = B_DYING; bossT = 0;}
    }
  }
}

// ===================================================================================
// Enemies
// ===================================================================================
void EnemySpawner(void) {
  if(bossState) return;
  if(++progress >= STAGE_LEN) {                   // end of stage: guardian comes
    bossState = B_WAIT;
    return;
  }
  if(spawnTimer) {spawnTimer--; return;}

  // Rest of a running wave
  if(waveLeft) {
    waveLeft--;
    EnemySpawn(waveType, waveType == E_DART ? TOP + 4 + JOY_random() % 44 : waveY);
    spawnTimer = (waveType == E_DART) ? 14 : 9;
    return;
  }

  // New wave
  uint8_t r = JOY_random() % 8;
  waveY = TOP + 14 + JOY_random() % 26;
  switch(r) {
    case 0: case 1: case 2:
      waveType = E_WAVE;  waveLeft = 3 + (stage > 2); break;
    case 3:
      waveType = E_DART;  waveLeft = 2 + (stage > 1); break;
    case 4:
      waveType = E_GUNNER; waveLeft = (stage > 2); break;
    case 5:
      if(FloorAt(127) >= 4) {waveType = E_TURRET; waveLeft = 0; EnemySpawn(E_TURRET, 0);}
      else {waveType = E_DART; waveLeft = 2;}
      break;
    case 6:
      waveType = (stage > 1) ? E_MINE : E_GUNNER; waveLeft = 0; break;
    default:
      waveType = E_GUNNER; waveLeft = 1; break;
  }
  if(r != 5 || waveType != E_TURRET) EnemySpawn(waveType, waveType == E_DART ? TOP + 4 + JOY_random() % 44 : waveY);
  spawnTimer = (stage < 6) ? 70 - stage * 8 : 22;
}

void EnemySpawn(uint8_t type, int8_t y) {
  static const uint8_t HP[] = {0, 1, 1, 2, 2, 3};
  for(uint8_t i = 0; i < MAX_ENEMIES; i++) {
    ENEMY *e = &enemy[i];
    if(e->type != E_NONE) continue;
    if(type == E_TURRET) y = 64 - FloorAt(127) - 7;
    else {                                        // stay in the gap of caves
      int8_t lo = TOP + CeilAt(127) + 1, hi = 64 - FloorAt(127) - 8;
      if(y < lo) y = lo;
      if(y > hi) y = hi;
    }
    e->type = type; e->x = 128; e->y = y; e->base = y;
    e->hp = HP[type]; e->t = JOY_random() & 31;
    return;
  }
}

void EnemiesUpdate(void) {
  uint8_t fireRate = (stage < 8) ? 64 - stage * 5 : 24;
  for(uint8_t i = 0; i < MAX_ENEMIES; i++) {
    ENEMY *e = &enemy[i];
    if(e->type == E_NONE) continue;
    e->t++;
    switch(e->type) {
      case E_DART:                                // fast and straight
        e->x -= 3; break;
      case E_WAVE:                                // sine wave
        e->x -= 2;
        e->y = e->base + SINE[(e->t << 1) & 31];
        break;
      case E_GUNNER:                              // slow, shoots at the ship
        e->x -= 1;
        if(e->x < 120 && e->x > shipX + 16 && !(e->t % fireRate)) EnemyFireAimed(e->x, e->y + 3);
        break;
      case E_TURRET:                              // sits on the ground
        e->x -= 1;
        if(e->x < 124 && e->x > shipX + 8 && !(e->t % (fireRate + 16))) EnemyFireAimed(e->x + 1, e->y);
        break;
      case E_MINE:                                // drifts towards the ship
        e->x -= 1;
        if(!(e->t & 3)) {
          if(e->y < shipY) e->y++;
          else if(e->y > shipY) e->y--;
        }
        break;
    }
    if(e->x < -8) {e->type = E_NONE; continue;}
    if(!deadTimer && !invuln && Collide(shipX + 1, shipY + 1, SHIP_W - 2, SHIP_H - 2, e->x + 1, e->y + 1, 6, 5)) {
      Explode(e->x, e->y);
      e->type = E_NONE;
      PlayerHit();
    }
  }
}

// ===================================================================================
// Enemy Shots
// ===================================================================================
void EnemyFire(int16_t x, int8_t y, int8_t dx, int8_t dy) {
  for(uint8_t i = 0; i < MAX_ESHOTS; i++) {
    ESHOT *s = &eshot[i];
    if(s->on) continue;
    s->on = 1; s->x = x << 4; s->y = y << 4; s->dx = dx; s->dy = dy;
    return;
  }
}

// Shot towards the ship, faster in later stages
void EnemyFireAimed(int16_t x, int8_t y) {
  int16_t dx = (shipX + 4) - x, dy = (shipY + 3) - y;
  int16_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
  int16_t m = ax > ay ? ax : ay;
  int16_t v = 18 + stage * 2;
  if(v > 32) v = 32;
  if(m == 0) return;
  EnemyFire(x, y, dx * v / m, dy * v / m);
  JOY_sound(90, 3);
}

void EnemyShotsUpdate(void) {
  for(uint8_t i = 0; i < MAX_ESHOTS; i++) {
    ESHOT *s = &eshot[i];
    if(!s->on) continue;
    s->x += s->dx; s->y += s->dy;
    int16_t x = s->x >> 4, y = s->y >> 4;
    if(x < 0 || x > 127 || y < TOP || y > 63) {s->on = 0; continue;}
    if(!deadTimer && !invuln && Collide(x, y, 2, 2, shipX + 1, shipY + 1, SHIP_W - 2, SHIP_H - 2)) {
      s->on = 0;
      PlayerHit();
    }
  }
}

// ===================================================================================
// Guardian at the End of Each Stage
// ===================================================================================
void BossUpdate(void) {
  uint8_t i;
  switch(bossState) {
    case B_WAIT:                                  // wait until the screen is clear
      for(i = 0; i < MAX_ENEMIES; i++) if(enemy[i].type != E_NONE) return;
      bossState = B_ENTER; bossX = 128; bossY = 24; bossDY = 1;
      bossMaxHP = bossHP = 20 + stage * 8;
      ShowMessage("WARNING!", 0, 70);
      JOY_sound(60, 40); JOY_sound(40, 40);
      break;

    case B_ENTER:                                 // fly in
      if(--bossX <= 106) {bossState = B_FIGHT; bossT = 0;}
      break;

    case B_FIGHT:
      // Move up and down
      if(frame & 1) {
        bossY += bossDY;
        if(bossY <= TOP + 2 || bossY >= 64 - 18) bossDY = -bossDY;
      }
      // Attack: spread volleys and aimed shots, faster in later stages
      bossT++;
      if(bossT >= ((stage < 6) ? 48 - stage * 4 : 24)) {
        bossT = 0;
        if(JOY_random() & 1) {
          EnemyFire(bossX, bossY + 7, -24, 0);
          EnemyFire(bossX, bossY + 7, -22, -8);
          EnemyFire(bossX, bossY + 7, -22, 8);
          JOY_sound(70, 6);
        }
        else {
          EnemyFireAimed(bossX, bossY + 4);
          EnemyFireAimed(bossX, bossY + 11);
        }
      }
      if(!deadTimer && !invuln && Collide(shipX + 1, shipY + 1, SHIP_W - 2, SHIP_H - 2, bossX + 2, bossY + 1, 14, 14))
        PlayerHit();
      break;

    case B_DYING:                                 // chain of explosions
      bossT++;
      if(!(bossT & 3)) {
        Explode(bossX + (JOY_random() & 7), bossY + (JOY_random() & 7));
        noise = 4;
      }
      if(bossT >= 64) {
        AddScore(500 * stage);
        AddScore(1000 * stage);
        if(stage < 99) stage++;
        bossState = B_NONE; progress = 0; spawnTimer = 90;
        ShowMessage("STAGE", stage, 70);
        JOY_sound(150, 80); JOY_sound(200, 80); JOY_sound(250, 120);
      }
      break;
  }
}

// ===================================================================================
// Explosions
// ===================================================================================
void Explode(int16_t x, int8_t y) {
  for(uint8_t i = 0; i < MAX_EXPL; i++) {
    if(!boom[i].t) {boom[i].x = x; boom[i].y = y; boom[i].t = 1; return;}
  }
}

// ===================================================================================
// Rendering
// ===================================================================================

// OR one byte column into the page buffer at pixel row y
void DrawColumn(uint8_t page, int16_t x, int8_t y, uint8_t b) {
  int8_t row = y - (page << 3);
  if(x < 0 || x > 127 || row <= -8 || row >= 8) return;
  buf[x] |= (row >= 0) ? (b << row) : (b >> -row);
}

void DrawSprite(uint8_t page, int16_t x, int8_t y, uint8_t w, const uint8_t *bmp) {
  int8_t row = y - (page << 3);
  if(row <= -8 || row >= 8) return;
  for(uint8_t i = 0; i < w; i++) DrawColumn(page, x + i, y, bmp[i]);
}

// 5x7 font, scale 1 or 2
void DrawText(uint8_t page, int16_t x, int8_t y, const char *s, uint8_t scale) {
  for(; *s; s++) {
    uint8_t c = 0;
    while(FONT_CHARS[c] && FONT_CHARS[c] != *s) c++;
    const uint8_t *g = &FONT[c * 5];
    for(uint8_t i = 0; i < 5; i++) {
      if(scale == 1) DrawColumn(page, x + i, y, g[i]);
      else {
        uint16_t w = 0;                           // double every bit
        for(uint8_t b = 0; b < 8; b++) if(g[i] & (1 << b)) w |= 3 << (b << 1);
        for(uint8_t k = 0; k < 2; k++) {
          DrawColumn(page, x + (i << 1) + k, y, w);
          DrawColumn(page, x + (i << 1) + k, y + 8, w >> 8);
        }
      }
    }
    x += 6 * scale;
  }
}

// Number with a fixed count of digits: 3x5 digits (big = 0) or 5x7 font (big = 1)
void DrawNumber(uint8_t page, int16_t x, int8_t y, uint32_t n, uint8_t digits, uint8_t big) {
  char s[8];
  s[digits] = 0;
  while(digits--) {s[digits] = '0' + n % 10; n /= 10;}
  if(big) {DrawText(page, x, y, s, 1); return;}
  for(char *p = s; *p; p++, x += 4) DrawSprite(page, x, y, 3, &DIGITS[(*p - '0') * 3]);
}

// Terrain bits of one page: dithered rock with a solid edge
void DrawTerrain(uint8_t page) {
  uint8_t top = page << 3;
  for(uint8_t x = 0; x < 128; x++) {
    uint8_t k = (tpos + x) & 127, f = floorH[k], c = ceilH[k], bits = 0;
    if(!(f | c)) continue;
    uint8_t pattern = (x & 1) ? 0xAA : 0x55;
    if(f) {
      int8_t yf = 64 - f - top;                   // first floor row in this page
      if(yf <= 0) bits = pattern;
      else if(yf < 8) bits = (pattern & (0xFF << yf)) | (1 << yf);
    }
    if(c) {
      int8_t yc = TOP + c - top;                  // first free row below the ceiling
      if(yc >= 8) bits |= pattern;
      else if(yc > 0) bits |= (pattern & (0xFF >> (8 - yc))) | (1 << (yc - 1));
    }
    buf[x] |= bits;
  }
}

// Draw the whole screen page by page and send it to the OLED
void Tiny_Flip(uint8_t mode) {
  uint8_t page, i;
  for(page = 0; page < 8; page++) {
    for(i = 0; i < 128; i++) buf[i] = 0;

    // Stars
    for(i = 0; i < MAX_STARS; i++)
      if((starY[i] >> 3) == page) buf[starX[i]] |= 1 << (starY[i] & 7);

    if(mode == R_TITLE) {
      DrawText(page, 40, 3, "TINY", 2);
      DrawText(page, 22, 21, "SHOOTER", 2);
      DrawSprite(page, 4 + ((frame >> 1) & 7), 25, 8, SPR_SHIP);
      if(frame & 16) DrawText(page, 34, 43, "PRESS FIRE", 1);
      DrawText(page, 31, 55, "HI", 1);
      DrawNumber(page, 49, 55, hiscore, 7, 1);
    }
    else {
      if(page == 0) {
        // Score line: score, ships, stage or guardian energy
        DrawNumber(0, 0, 1, score, 6, 0);
        for(i = 0; i < lives && i < 6; i++) DrawSprite(0, 30 + i * 7, 1, 5, SPR_LIFE);
        if(bossState == B_FIGHT) {
          uint8_t w = (uint16_t)bossHP * 40 / bossMaxHP;
          for(i = 0; i < 42; i++) buf[86 + i] |= (i == 0 || i == 41) ? 0x3E : (i <= w ? 0x3E : 0x22);
        }
        else {
          DrawSprite(0, 112, 1, 3, &DIGITS[10 * 3]);  // "S" for stage
          DrawNumber(0, 120, 1, stage, 2, 0);
        }
        for(i = 0; i < 128; i += 2) buf[i] |= 0x80;   // dotted line
      }
      else {
        // Terrain
        DrawTerrain(page);

        // Ship (blinks while invulnerable)
        if(mode == R_GAME && !deadTimer && !(invuln & 2)) DrawSprite(page, shipX, shipY, 8, SPR_SHIP);

        // Player shots
        for(i = 0; i < MAX_PSHOTS; i++)
          if(pshot[i].on && (pshot[i].y >> 3) == page)
            for(uint8_t k = 0; k < 4; k++) DrawColumn(page, pshot[i].x + k, pshot[i].y, 1);

        // Enemies
        for(i = 0; i < MAX_ENEMIES; i++) {
          ENEMY *e = &enemy[i];
          const uint8_t *spr;
          switch(e->type) {
            case E_DART:   spr = SPR_DART;   break;
            case E_WAVE:   spr = (e->t & 4) ? SPR_WAVE0 : SPR_WAVE1; break;
            case E_GUNNER: spr = SPR_GUNNER; break;
            case E_TURRET: spr = SPR_TURRET; break;
            case E_MINE:   spr = SPR_MINE;   break;
            default: continue;
          }
          DrawSprite(page, e->x, e->y, 8, spr);
        }

        // Guardian (flickers when hit hard)
        if(bossState >= B_ENTER && !(bossState == B_DYING && (bossT & 2))) {
          DrawSprite(page, bossX, bossY, 16, SPR_BOSS);
          DrawSprite(page, bossX, bossY + 8, 16, SPR_BOSS + 16);
        }

        // Enemy shots: small diamonds
        for(i = 0; i < MAX_ESHOTS; i++) {
          if(!eshot[i].on) continue;
          int16_t x = eshot[i].x >> 4;
          int8_t  y = eshot[i].y >> 4;
          DrawColumn(page, x, y, 0x02);
          DrawColumn(page, x + 1, y, 0x07);
          DrawColumn(page, x + 2, y, 0x02);
        }

        // Power capsule
        if(capOn && (frame & 4)) DrawSprite(page, capX, capY, 7, SPR_CAPSULE);

        // Explosions
        for(i = 0; i < MAX_EXPL; i++) {
          if(!boom[i].t) continue;
          static const uint8_t *const EXP[3] = {SPR_EXPL0, SPR_EXPL1, SPR_EXPL2};
          DrawSprite(page, boom[i].x, boom[i].y, 8, EXP[(boom[i].t - 1) / 3]);
        }
      }

      // Game over text or message
      if(mode == R_GAMEOVER) {
        DrawText(page, 10, 18, "GAME OVER", 2);
        if(frame & 16) {
          DrawText(page, 25, 42, "SCORE", 1);
          DrawNumber(page, 61, 42, score, 7, 1);
        }
      }
      else if(msgTimer && (msgTimer > 20 || (msgTimer & 4))) {
        uint8_t len = 0;
        while(msgText[len]) len++;
        if(msgNum) len += 2 + (msgNum > 9);
        int16_t x = 64 - len * 3;
        DrawText(page, x, 28, msgText, 1);
        if(msgNum) DrawNumber(page, x + (len - 1 - (msgNum > 9)) * 6, 28, msgNum, 1 + (msgNum > 9), 1);
      }
    }

    // Send page to OLED
    JOY_OLED_data_start(page);
    for(i = 0; i < 128; i++) JOY_OLED_send(buf[i]);
    JOY_OLED_end();
  }
}
