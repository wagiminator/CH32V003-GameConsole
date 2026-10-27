// ===================================================================================
// Project:   Tiny Run - Pseudo 3D Racing Game for CH32V003
// Version:   v1.0
// Year:      2026
// Github:    https://github.com/wagiminator
// EasyEDA:   https://easyeda.com/wagiminator
// License:   http://creativecommons.org/licenses/by-sa/3.0/
// ===================================================================================
//
// Description:
// ------------
// A small Out Run-like racing game for the CH32V003 Mini Game Console. Race your
// convertible along a winding coast road, through the desert, the mountains and
// the city. Reach the checkpoint at the end of each stage before the time runs out;
// at each checkpoint the road forks and you choose the next stage. Five stages in a
// row bring you to the goal.
//
// Controls:
// ---------
// Fire button:  accelerate, start game
// Left/Right:   steer
// Down:         brake
//
// Compilation Settings:
// ---------------------
// - Make sure GCC toolchain (gcc-riscv64-unknown-elf, newlib) and Python3 with rvprog
//   are installed. In addition, Linux requires access rights to WCH-LinkE programmer.
// - Connect the WCH-LinkE programmer to the MCU board.
// - Run 'make flash'.


#include <driver.h>           // TinyJoypad conversion driver
#include <spritebank.h>       // sprites, backgrounds and fonts

// ===================================================================================
// Game Settings
// ===================================================================================
#define FRAME_TIME    (33 * DLY_MS_TIME)  // ~30 frames per second
#define HORIZON       23      // screen row above the first road row
#define ROWS          40      // road rows (screen rows 24..63)
#define ZK            3200    // depth of road row r: ZK / r
#define SEG_SHIFT     8       // 256 position units per track segment
#define STAGE_LEN     260     // segments per stage
#define FORK_LEN      48      // the road forks in the last segments of a stage
#define FORK_MAX      72      // final distance of the two roads' centers
#define ROAD_W        44      // half road width (in pixels at row 32)
#define CURVE_SHIFT   11      // strength of the curves on the screen
#define MAX_SPEED     384     // in 1/8 position units per frame (288 km/h)
#define PLAYER_Z      90      // depth of the player's car
#define CARS          6       // traffic
#define MAX_OBJ       16      // sprites per frame
#define LEVELS        5       // stages to the goal
#define START_TIME    75
#define CRASH_TIME    45

// ===================================================================================
// Game Variables
// ===================================================================================
typedef struct {
  uint32_t z;                 // position along the stage
  int8_t   x;                 // lane position
  uint8_t  speed, type;
} CAR;

typedef struct {
  int16_t  x, y;              // screen center, bottom row
  uint8_t  w, h, spr;         // size on the screen, sprite
  uint16_t dz;                // distance (for sorting)
  uint16_t sx, sy;            // scaling steps (8.8 fixed point)
} OBJ;

uint8_t  buf[128];            // one display page
int8_t   curve[STAGE_LEN];    // the stage
uint16_t zrow[ROWS + 1];      // depth of each road row
uint16_t inv[64];             // 8192 / r: scaling steps without division
int16_t  rowC[ROWS + 1], rowHW[ROWS + 1], rowF[ROWS + 1];
uint8_t  rowPh[ROWS + 1];

CAR      car[CARS];
OBJ      obj[MAX_OBJ];
uint8_t  order[MAX_OBJ], objCount;

uint32_t pos;                 // player position along the stage
int16_t  px;                  // player position across the road
uint16_t speed;               // 1/8 position units per frame
uint8_t  posFrac;
int16_t  bgX;                 // background scroll position
uint8_t  level, branch, theme, cmax, traffic;
uint8_t  timeLeft, secTimer, frame, crash, offroad, goal, attract;
int8_t   steer;
uint32_t score, hiscore;

const char *msgText;
uint8_t  msgNum, msgTimer;

// ===================================================================================
// Function Prototypes
// ===================================================================================
void TitleScreen(void);
void Race(void);
void NewStage(void);
void Drive(uint8_t accel, uint8_t brake, int8_t dir);
void MoveTraffic(void);
void Crash(void);
void ShowMessage(const char *text, uint8_t num, uint8_t time);
void Tiny_Flip(uint8_t title);
void WaitFrame(uint32_t start);

// ===================================================================================
// Main Function
// ===================================================================================
int main(void) {
  // Setup
  JOY_init();
  for(uint8_t r = 1; r <= ROWS; r++) zrow[r] = ZK / r;
  for(uint8_t r = 1; r < 64; r++) inv[r] = 8192 / r;

  // Loop
  while(1) {
    TitleScreen();
    Race();
  }
}

void WaitFrame(uint32_t start) {
  while((int32_t)(STK->CNT - start) < FRAME_TIME);
}

void ShowMessage(const char *text, uint8_t num, uint8_t time) {
  msgText = text; msgNum = num; msgTimer = time;
}

// ===================================================================================
// Stages
// ===================================================================================
uint16_t seed;
uint8_t Rnd(void) {
  seed ^= seed << 7; seed ^= seed >> 9; seed ^= seed << 8;
  return seed;
}

// Build the curves of a stage from its number: random pieces of road with smooth
// transitions; the start and the fork at the end are straight
void NewStage(void) {
  uint16_t s = 0;
  int8_t c = 0;
  seed = 0x2F00 + (level << 5) + (branch << 2) + 1;
  theme = (level + branch) & 3;
  if(level == 0) theme = 0;                       // the classic start: the beach
  cmax = 1 + ((level + branch) % 3);
  traffic = 3 + level;
  if(traffic > CARS) traffic = CARS;
  while(s < STAGE_LEN) {
    uint8_t len = 10 + (Rnd() & 31);
    int8_t target = 0;
    if(s > 12 && s < STAGE_LEN - FORK_LEN - 10 && (Rnd() % 3)) {
      target = 1 + Rnd() % cmax;
      if(Rnd() & 1) target = -target;
    }
    for(uint8_t i = 0; i < len && s < STAGE_LEN; i++, s++) {
      if(!(i & 1)) {if(c < target) c++; else if(c > target) c--;}  // ease in and out
      if(s >= STAGE_LEN - FORK_LEN - 4) c = 0;
      curve[s] = c;
    }
  }
  // traffic ahead
  for(uint8_t i = 0; i < CARS; i++) {
    car[i].z = 2000 + i * 1900 + (Rnd() << 2);
    car[i].x = ((int8_t)(Rnd() % 3) - 1) * 29;
    car[i].speed = 14 + (Rnd() & 15);
    car[i].type = (Rnd() & 3) == 0;
  }
  timeLeft += level ? 60 - level * 2 : 0;
  secTimer = 0;
}

static inline int8_t CurveAt(uint16_t seg) {
  return seg < STAGE_LEN ? curve[seg] : 0;
}

// Distance of the two roads' centers from the middle at a segment
static inline int16_t ForkAt(uint16_t seg) {
  if(level >= LEVELS - 1) return 0;               // the last stage has no fork
  if(seg < STAGE_LEN - FORK_LEN) return 0;
  seg -= STAGE_LEN - FORK_LEN;
  return seg >= FORK_LEN ? FORK_MAX : (seg * 3) >> 1;
}

// ===================================================================================
// Race
// ===================================================================================
void Race(void) {
  level = 0; branch = 0; score = 0; goal = 0;
  timeLeft = START_TIME;
  pos = 0; px = 0; speed = 0; crash = 0; bgX = 0;
  NewStage();
  timeLeft = START_TIME;

  // countdown
  for(uint8_t i = 3; i; i--) {
    ShowMessage("", i, 30);
    JOY_sound(200, 40);
    for(uint8_t f = 0; f < 30; f++) {
      uint32_t start = STK->CNT;
      Tiny_Flip(0);
      if(msgTimer) msgTimer--;
      WaitFrame(start);
    }
  }
  ShowMessage("GO!", 0, 40);
  JOY_sound(250, 80);

  uint8_t over = 0;
  while(1) {
    uint32_t start = STK->CNT;
    frame++;
    int8_t dir = JOY_left_pressed() ? -1 : JOY_right_pressed() ? 1 : 0;
    if(timeLeft && !goal) Drive(JOY_act_pressed(), JOY_down_pressed(), dir);
    else {
      Drive(0, 1, 0);                             // time up or goal: stop
      if(!speed && !over) over = 1;
    }
    MoveTraffic();

    // time
    if(timeLeft && !goal && ++secTimer >= 30) {
      secTimer = 0; timeLeft--;
      if(timeLeft <= 5 && timeLeft) JOY_sound(230, 10);
      if(!timeLeft) {ShowMessage("TIME UP", 0, 255); JOY_sound(100, 60); JOY_sound(60, 100);}
    }

    // checkpoint: choose the next stage by the road you're on
    if(pos >= ((uint32_t)STAGE_LEN << SEG_SHIFT)) {
      pos -= (uint32_t)STAGE_LEN << SEG_SHIFT;
      if(level >= LEVELS - 1) {
        goal = 1; pos = 0;
        ShowMessage("GOAL!", 0, 255);
        JOY_sound(150, 40); JOY_sound(200, 40); JOY_sound(250, 80);
      }
      else {
        if(px >= 0) {branch++; px -= FORK_MAX;}
        else px += FORK_MAX;
        level++;
        NewStage();
        ShowMessage("CHECKPOINT", 0, 70);
        JOY_sound(200, 30); JOY_sound(250, 30); JOY_sound(200, 30); JOY_sound(250, 50);
      }
    }
    else if(msgTimer == 1 && msgText[0] == 'C') ShowMessage("STAGE", level + 1, 50);

    Tiny_Flip(0);
    if(msgTimer && msgTimer < 255) msgTimer--;
    WaitFrame(start);

    if(over) {
      // time bonus at the goal, then wait for the fire button
      if(goal) {
        while(timeLeft) {
          uint32_t s2 = STK->CNT;
          timeLeft--; score += 1000;
          JOY_sound(220, 10);
          Tiny_Flip(0);
          WaitFrame(s2);
        }
      }
      if(score > hiscore) hiscore = score;
      ShowMessage(goal ? "WELL DONE" : "GAME OVER", 0, 255);
      for(frame = 0; ; frame++) {
        uint32_t s2 = STK->CNT;
        Tiny_Flip(0);
        if(frame > 60 && JOY_act_pressed()) return;
        if(frame > 250) frame = 61;
        WaitFrame(s2);
      }
    }
  }
}

// Speed, steering, curves pushing the car out, grass and crashes
void Drive(uint8_t accel, uint8_t brake, int8_t dir) {
  uint16_t seg = pos >> SEG_SHIFT;
  int16_t f = ForkAt(seg);
  int16_t d = px < 0 ? -px : px;                  // distance from the (nearest) road center
  if(f) {d = px < 0 ? px + f : px - f; if(d < 0) d = -d;}
  offroad = d > ROAD_W;

  if(crash) {
    crash--;
    speed = 0;
    if(!crash) {if(f) px = px < 0 ? -f : f; else if(offroad) px = px < 0 ? -20 : 20;}
    return;
  }
  if(accel) {
    if(speed < MAX_SPEED) speed += speed < 192 ? 5 : speed < 320 ? 3 : 1;
  }
  else if(speed) speed -= speed > 2 ? 2 : speed;
  if(brake) speed = speed > 12 ? speed - 12 : 0;
  if(offroad && speed > 150) speed -= 8;
  if(speed > MAX_SPEED) speed = MAX_SPEED;

  // move along the road
  uint16_t adv = (speed >> 3);
  posFrac += speed & 7;
  if(posFrac >= 8) {posFrac -= 8; adv++;}
  pos += adv;
  score += adv >> 2;

  // steer; the curve pushes outwards; the background scrolls
  steer = 0;
  if(speed > 8 && dir) {
    px += dir * (2 + (speed >> 7));
    steer = dir;
  }
  int8_t c = CurveAt(seg);
  px -= (c * (int16_t)speed) >> 8;
  bgX += (c * (int16_t)speed) >> 7;
  int16_t lim = ROAD_W + 60 + f;
  if(px < -lim) px = -lim;
  if(px > lim)  px = lim;

  // roadside objects and the sign at the fork
  uint16_t s = (pos + PLAYER_Z) >> SEG_SHIFT;
  if(offroad && s < STAGE_LEN && ((pos + PLAYER_Z) & 255) < 96) {
    int16_t ox = 0x7FFF;
    ox = f + ROAD_W + 32 + ((s >> 1) & 3) * 4;
    if(!(s & 1)) ox = -ox;
    if(s == STAGE_LEN - FORK_LEN + 40 && f) ox = 0;
    if(ox != 0x7FFF && px - ox > -16 && px - ox < 16 && speed > 40) Crash();
  }

  // the grass rumbles
  if(offroad && speed > 40 && (frame & 1)) JOY_sound(10, 2);
}

void Crash(void) {
  crash = CRASH_TIME;
  JOY_sound(30, 30); JOY_sound(60, 20); JOY_sound(20, 40);
}

void MoveTraffic(void) {
  uint32_t end = (uint32_t)STAGE_LEN << SEG_SHIFT;
  for(uint8_t i = 0; i < traffic; i++) {
    CAR *c = &car[i];
    c->z += c->speed;
    // cars far behind or far ahead come back ahead
    if(c->z + 400 < pos || c->z > pos + ZK + 6000) {
      c->z = pos + ZK + 200 + (JOY_random() & 2047);
      c->x = ((int8_t)(JOY_random() % 3) - 1) * 29;
      c->speed = 12 + (JOY_random() & 15) + level;
      c->type = (JOY_random() & 3) == 0;
    }
    if(c->z >= end - 512) continue;               // no cars at the checkpoint
    // collision with the player's car
    int32_t dz = (int32_t)(c->z - pos);
    if(!crash && !attract && dz > PLAYER_Z - 30 && dz < PLAYER_Z + 40) {
      int16_t cx = c->x;
      int16_t f = ForkAt(c->z >> SEG_SHIFT);
      cx += (cx > 0) ? f : -f;
      int16_t dx = cx - px;
      if(dx > -24 && dx < 24 && speed > (c->speed << 3)) {
        Crash();
        c->z = pos + PLAYER_Z + 120;              // the other car drives on
      }
    }
  }
}

// ===================================================================================
// Rendering
// ===================================================================================

// Road geometry of every row: center, half width, fork and stripe phase. The
// curves are integrated from the near to the far rows.
void ComputeRoad(void) {
  int32_t slope = 0, X = 0;
  uint16_t prevz = 0;
  for(uint8_t r = ROWS; r; r--) {
    uint16_t z = zrow[r];
    uint32_t a = pos + z;
    uint16_t seg = a >> SEG_SHIFT;
    int16_t dz = z - prevz;
    prevz = z;
    slope += CurveAt(seg) * dz;
    X += slope * dz;
    int16_t xs = X >> CURVE_SHIFT;
    rowC[r]  = 64 + (((int32_t)(xs - px) * r) >> 5);
    rowHW[r] = (ROAD_W * r) >> 5;
    rowF[r]  = (ForkAt(seg) * r) >> 5;
    rowPh[r] = (a >> 7) & 1;
  }
}

// Set pixels in a row from x0 to x1: solid (step 1) or dotted (step 2 or 4)
static void Span(int16_t x0, int16_t x1, uint8_t bit, uint8_t step, uint8_t phase) {
  if(x0 < 0) x0 = 0;
  if(x1 > 127) x1 = 127;
  if(step > 1) x0 += (phase - x0) & (step - 1);
  for(; x0 <= x1; x0 += step) buf[x0] |= bit;
}

// One road row: grass, curbs, road edges, lane markings (for one or two roads)
void DrawRow(uint8_t r) {
  uint8_t bit = 1 << ((HORIZON + r) & 7);
  int16_t c = rowC[r], hw = rowHW[r], f = rowF[r];
  uint8_t ph = rowPh[r];
  int16_t rw = (hw >> 3) + 1;                     // curb width
  int16_t L = c - f - hw, R = c + f + hw;
  uint8_t gstep = ph ? 4 : 8, gph = ph ? ((r << 1) & 3) : ((r * 3) & 7);
  Span(0, L - rw - 1, bit, gstep, gph);           // grass
  Span(R + rw + 1, 127, bit, gstep, gph);
  if(ph) {Span(L - rw, L - 1, bit, 1, 0); Span(R + 1, R + rw, bit, 1, 0);}
  Span(L, L, bit, 1, 0); Span(R, R, bit, 1, 0);   // road edges
  if(f > hw) {                                    // grass between the forked roads
    int16_t gl = c - f + hw, gr = c + f - hw;
    Span(gl, gl, bit, 1, 0); Span(gr, gr, bit, 1, 0);
    Span(gl + 2, gr - 2, bit, gstep, gph);
  }
  if(ph) {                                        // lane markings
    int16_t l3 = (hw >> 2) + (hw >> 4) + (hw >> 6), lw = (hw >> 4);
    for(int8_t k = -1; k <= 1; k += 2) {
      int16_t rc = c + k * f;
      if(!f && k > 0) break;
      Span(rc - l3, rc - l3 + lw, bit, 1, 0);
      Span(rc + l3, rc + l3 + lw, bit, 1, 0);
    }
  }
}

// Add a sprite at depth dz and lateral position xo (road coordinates)
void AddObject(uint8_t spr, int32_t dz, int16_t xo) {
  if(dz < 50 || dz >= ZK || objCount >= MAX_OBJ - 1) return;
  uint8_t r;
  if(dz >= zrow[ROWS]) {                          // find the row by binary search
    uint8_t lo = 1, hi = ROWS;
    while(lo < hi) {uint8_t mid = (lo + hi + 1) >> 1; if(zrow[mid] >= dz) lo = mid; else hi = mid - 1;}
    r = lo;
  }
  else r = (dz < 51) ? 63 : ZK / dz;
  OBJ *o = &obj[objCount];
  int16_t cx = (r <= ROWS) ? rowC[r] : 64 - ((px * r) >> 5);
  o->x = cx + ((xo * r) >> 5);
  o->y = HORIZON + r;
  uint8_t sh = spr < S_PALM ? 5 : 4;             // scenery is drawn twice as big
  o->w = (SPRITE_W[spr] * r) >> sh;
  o->h = (SPRITE_H[spr] * r) >> sh;
  if(o->w < 2 || o->h < 2) return;
  if(o->x + o->w < 0 || o->x - o->w > 127) return;
  o->spr = spr; o->dz = dz;
  o->sx = o->sy = inv[r] >> (5 - sh);
  objCount++;
}

// Collect everything to draw this frame: roadside objects, traffic, checkpoint
void CollectObjects(void) {
  static const uint8_t MAIN[4] = {S_PALM, S_CACTUS, S_PINE, S_LAMP};
  static const uint8_t SIDE[4] = {S_SIGN, S_ROCK, S_ROCK, S_SIGN};
  objCount = 0;
  uint16_t s0 = pos >> SEG_SHIFT;
  for(uint16_t s = s0; s <= s0 + (ZK >> SEG_SHIFT) + 1; s++) {
    int32_t dz = ((int32_t)s << SEG_SHIFT) - (int32_t)pos;
    int16_t f = ForkAt(s);
    if(s >= STAGE_LEN) {
      if(s == STAGE_LEN) {                        // checkpoint posts
        AddObject(S_POST, dz, -(f + ROAD_W + 8));
        AddObject(S_POST, dz, f + ROAD_W + 8);
        if(f) AddObject(S_POST, dz, 0);
      }
      continue;
    }
    if(s == STAGE_LEN - FORK_LEN + 40 && f) {AddObject(S_SIGN, dz, 0); continue;}
    if((s & 3) == 1) {                            // curve warnings
      int8_t c = CurveAt(s + 6);
      if((c >= 2 || c <= -2) && !CurveAt(s)) {
        AddObject(c > 0 ? S_ARROWR : S_ARROW, dz, c > 0 ? f + ROAD_W + 26 : -(f + ROAD_W + 26));
        continue;
      }
    }
    uint8_t spr = (s & 15) == 8 ? SIDE[theme] : MAIN[theme];
    int16_t x = f + ROAD_W + 32 + ((s >> 1) & 3) * 4;
    AddObject(spr, dz, (s & 1) ? x : -x);
  }
  uint32_t end = (uint32_t)STAGE_LEN << SEG_SHIFT;
  for(uint8_t i = 0; i < traffic; i++) {
    if(car[i].z >= end - 512) continue;
    int16_t f = ForkAt(car[i].z >> SEG_SHIFT);
    int16_t x = car[i].x;
    x += (x > 0) ? f : -f;
    AddObject(car[i].type ? S_TRUCK : S_TCAR, (int32_t)(car[i].z - pos), x);
  }
  // draw far objects first
  for(uint8_t i = 0; i < objCount; i++) order[i] = i;
  for(uint8_t i = 1; i < objCount; i++) {
    uint8_t k = order[i], j = i;
    while(j && obj[order[j - 1]].dz < obj[k].dz) {order[j] = order[j - 1]; j--;}
    order[j] = k;
  }
}

// Draw the part of a scaled sprite that lies in the current page
void DrawObject(uint8_t page, OBJ *o) {
  int16_t top = o->y - o->h + 1;
  int16_t r0 = page << 3;
  if(top > r0 + 7 || o->y < r0) return;
  uint8_t n = 0, rb[8], rs[8];
  uint8_t hmax = SPRITE_H[o->spr] - 1, wmax = SPRITE_W[o->spr] - 1;
  int16_t dy = r0 - top;
  uint32_t sya = (dy > 0) ? (uint32_t)dy * o->sy : 0;
  for(uint8_t b = 0; b < 8; b++, dy++) {          // source row of every pixel row
    if(dy >= 0 && dy < o->h) {
      uint8_t s = sya >> 8;
      rb[n] = 1 << b; rs[n++] = s > hmax ? hmax : s;
      sya += o->sy;
    }
  }
  const uint32_t *s = &SPRITES[SPRITE_OFFSET[o->spr]];
  int16_t x = o->x - (o->w >> 1);
  uint32_t sxa = 0;
  uint8_t last = 0xFF, ib = 0, mb = 0;
  for(uint8_t dx = 0; dx < o->w; dx++, x++, sxa += o->sx) {
    if(x < 0) continue;
    if(x > 127) break;
    uint8_t sc = sxa >> 8;
    if(sc > wmax) sc = wmax;
    if(sc != last) {                              // a new source column
      last = sc;
      uint32_t ink = s[sc], mask = ink;
      if(sc) mask |= s[sc - 1];
      if(sc < wmax) mask |= s[sc + 1];
      mask |= (mask << 1) | (mask >> 1);          // outline
      ib = 0; mb = 0;
      for(uint8_t i = 0; i < n; i++) {
        if((mask >> rs[i]) & 1) mb |= rb[i];
        if((ink  >> rs[i]) & 1) ib |= rb[i];
      }
    }
    buf[x] = (buf[x] & ~mb) | ib;
  }
}

// Checkered banner between the outer checkpoint posts
void DrawBanner(uint8_t page) {
  OBJ *l = 0, *r = 0;
  for(uint8_t i = 0; i < objCount; i++) {
    OBJ *o = &obj[i];
    if(o->spr != S_POST) continue;
    if(!l || o->x < l->x) l = o;
    if(!r || o->x > r->x) r = o;
  }
  if(!l || l == r) return;
  int16_t top = l->y - l->h + 1, bh = (l->h >> 2) + 1, r0 = page << 3;
  uint8_t sq = (bh >> 1) + 1;
  for(uint8_t b = 0; b < 8; b++) {
    int16_t dy = r0 + b - top;
    if(dy < 0 || dy >= bh) continue;
    uint8_t rowOdd = (dy / sq) & 1, cnt = 0, on = rowOdd;
    for(int16_t x = l->x; x <= r->x; x++) {
      if(++cnt > sq) {cnt = 1; on ^= 1;}
      if(x < 0 || x > 127) continue;
      if(on) buf[x] |= 1 << b; else buf[x] &= ~(1 << b);
    }
  }
}

// 5x7 font, scale 1 or 2, with a black box behind the letters
void DrawColumn(uint8_t page, int16_t x, int16_t y, uint8_t b) {
  int16_t row = y - (page << 3);
  if(x < 0 || x > 127 || row <= -8 || row >= 8) return;
  buf[x] |= (row >= 0) ? (b << row) : (b >> -row);
}

void DrawText(uint8_t page, int16_t x, int8_t y, const char *s, uint8_t scale) {
  int8_t r0 = page << 3;
  if(y - 1 >= r0 + 8 || y + 7 * scale < r0) return;
  uint8_t box = 0;
  for(int8_t r = -1; r <= 7 * scale; r++) {
    int8_t row = y + r - r0;
    if(row >= 0 && row < 8) box |= 1 << row;
  }
  for(; *s; s++) {
    uint8_t c = 0;
    while(FONT_CHARS[c] && FONT_CHARS[c] != *s) c++;
    const uint8_t *g = &FONT[c * 5];
    for(uint8_t i = 0; i < 6 * scale + 1; i++) {
      int16_t cx = x + i - 1;
      if(cx >= 0 && cx <= 127) buf[cx] &= ~box;
    }
    for(uint8_t i = 0; i < 5; i++) {
      if(scale == 1) DrawColumn(page, x + i, y, g[i]);
      else {
        uint16_t w = 0;
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

void DrawTextC(uint8_t page, int8_t y, const char *s, uint8_t scale) {
  uint8_t len = 0;
  while(s[len]) len++;
  DrawText(page, 64 - len * 3 * scale + scale, y, s, scale);
}

// Number: 3x5 digits (big = 0) or the 5x7 font (big = 1), fixed count of digits
void DrawNumber(uint8_t page, int16_t x, int8_t y, uint32_t n, uint8_t digits, uint8_t big) {
  static const uint32_t POW10[7] = {1, 10, 100, 1000, 10000, 100000, 1000000};
  char s[8];
  for(uint8_t i = 0; i < digits; i++) {            // digits without division
    uint32_t p = POW10[digits - 1 - i];
    char d = '0';
    while(n >= p && d < '9') {n -= p; d++;}
    s[i] = d;
  }
  s[digits] = 0;
  if(big) {DrawText(page, x, y, s, 1); return;}
  for(char *p = s; *p; p++, x += 4)
    for(uint8_t i = 0; i < 3; i++) buf[x + i] |= DIGITS[(*p - '0') * 3 + i] << 1;
}

// Draw the whole screen page by page and send it to the OLED
void Tiny_Flip(uint8_t title) {
  ComputeRoad();
  CollectObjects();
  // the player's car
  OBJ *pc = &obj[MAX_OBJ - 1];
  uint8_t pspr = S_CAR;
  if(crash) pspr = (crash & 4) ? S_CARL : S_CARR;
  else if(steer) pspr = steer < 0 ? S_CARL : S_CARR;
  pc->spr = pspr; pc->w = SPRITE_W[pspr]; pc->h = SPRITE_H[pspr];
  pc->x = 64; pc->y = 63 - ((offroad && speed > 40) ? (frame & 1) : 0);
  pc->sx = pc->sy = 256;

  for(uint8_t page = 0; page < 8; page++) {
    for(uint8_t x = 0; x < 128; x++) buf[x] = 0;
    if(page == 0) {
      if(title) {
        DrawText(0, 37, 0, "HI", 1);
        DrawNumber(0, 55, 0, hiscore, 7, 1);
      }
      else {
        DrawNumber(0, 0, 0, score, 7, 0);
        if(timeLeft > 10 || (frame & 8)) DrawNumber(0, 58, 0, timeLeft, 2, 1);
        DrawNumber(0, 104, 0, ((uint32_t)speed * 3) >> 2, 3, 1);
      }
    }
    else if(page < 3) {                           // sky with background
      const uint8_t *bg = &BACKGROUNDS[(theme << 9) + ((page - 1) << 8)];
      for(uint8_t x = 0; x < 128; x++) buf[x] = bg[(uint8_t)(x + (bgX >> 3))];
    }
    else {                                        // road and objects
      for(uint8_t b = 0; b < 8; b++) DrawRow((page << 3) + b - HORIZON);
    }
    if(page >= 1) {
      for(uint8_t i = 0; i < objCount; i++) DrawObject(page, &obj[order[i]]);
      DrawBanner(page);
      if(!title) DrawObject(page, pc);
    }

    if(title) {
      DrawTextC(page, 12, "TINY", 2);
      DrawTextC(page, 30, "RUN", 2);
      if(frame & 16) DrawTextC(page, 50, "PRESS FIRE", 1);
    }
    else if(msgTimer && (msgTimer > 10 || (msgTimer & 2))) {
      if(msgNum && !msgText[0]) {
        char s[2] = {'0' + msgNum, 0};
        DrawTextC(page, 26, s, 2);
      }
      else if(msgNum) {
        DrawTextC(page, 26, msgText, 1);
        DrawNumber(page, 64 + 18, 26, msgNum, 1, 1);
      }
      else DrawTextC(page, 26, msgText, 1);
    }

    // Send page to OLED
    JOY_OLED_data_start(page);
    for(uint8_t x = 0; x < 128; x++) JOY_OLED_send(buf[x]);
    JOY_OLED_end();
  }
}

// ===================================================================================
// Title Screen: the car drives by itself
// ===================================================================================
void TitleScreen(void) {
  level = 0; branch = 0; pos = 0; px = 0; speed = 0; crash = 0; goal = 0;
  NewStage();
  msgTimer = 0; attract = 1;
  while(JOY_act_pressed());
  for(frame = 0; ; frame++) {
    uint32_t start = STK->CNT;
    if(speed < 200) speed += 4;
    pos += speed >> 3;
    if(pos >= ((uint32_t)(STAGE_LEN - FORK_LEN - 8) << SEG_SHIFT)) pos = 0;
    int8_t c = CurveAt(pos >> SEG_SHIFT);
    bgX += (c * (int16_t)speed) >> 7;
    px = 0; steer = 0;
    MoveTraffic();
    crash = 0;
    Tiny_Flip(1);
    if(JOY_act_pressed()) break;
    JOY_random();
    WaitFrame(start);
  }
  attract = 0;
  JOY_sound(150, 30); JOY_sound(200, 30); JOY_sound(250, 50);
}
