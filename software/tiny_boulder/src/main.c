// ===================================================================================
// Project:   Tiny Boulder - Boulder Dash-like Digging Game for CH32V003
// Version:   v1.0
// Year:      2026
// Github:    https://github.com/wagiminator
// EasyEDA:   https://easyeda.com/wagiminator
// License:   http://creativecommons.org/licenses/by-sa/3.0/
// ===================================================================================
//
// Description:
// ------------
// A small Boulder Dash-like game for the CH32V003 Mini Game Console. Dig your way
// through six scrolling caves, collect the required number of diamonds before the
// time runs out and escape through the exit. Beware of falling boulders and of the
// fireflies and butterflies; drop a boulder on a butterfly to turn it into diamonds.
//
// Controls:
// ---------
// Joypad:              walk and dig (push boulders sideways)
// Fire button + pad:   dig or collect next to you without moving
// Fire button:         start game, restart the cave after losing a life
//
// Compilation Settings:
// ---------------------
// - Make sure GCC toolchain (gcc-riscv64-unknown-elf, newlib) and Python3 with rvprog
//   are installed. In addition, Linux requires access rights to WCH-LinkE programmer.
// - Connect the WCH-LinkE programmer to the MCU board.
// - Run 'make flash'.


#include <driver.h>           // TinyJoypad conversion driver
#include <spritebank.h>       // tiles, fonts and caves

// ===================================================================================
// Game Settings
// ===================================================================================
#define FRAME_TIME    (33 * DLY_MS_TIME)  // ~30 frames per second
#define TICK_FRAMES   4       // cave update every 4 frames (7.5 moves per second)
#define CW            40      // cave size in cells
#define CH            22
#define IDX(x, y)     (((y) << 5) + ((y) << 3) + (x))     // y * 40 without multiply
#define LIVES         3
#define EXTRA_LIFE    1000    // points for an extra life
#define START_TICKS   15      // ticks before the player comes out of the inbox
#define CAM_MAX_X     ((CW << 3) - 128)
#define CAM_MAX_Y     ((CH << 3) - 56)

// Cell types (bits 0-4), creature direction (bits 5-6), "already moved" flag (bit 7)
enum {
  C_EMPTY, C_DIRT, C_WALL, C_STEEL, C_BOULDER, C_BOULDER_F, C_DIAMOND, C_DIAMOND_F,
  C_PLAYER, C_INBOX, C_EXIT, C_EXIT_OPEN, C_FIREFLY, C_BUTTERFLY,
  C_EXPL1, C_EXPL2, C_EXPL3, C_DEXPL1, C_DEXPL2, C_DEXPL3
};
#define TYPE(c)       ((c) & 0x1F)
#define DIR(c)        (((c) >> 5) & 3)
#define MOVED         0x80

enum {R_PLAY, R_DEAD, R_EXIT, R_TIMEOUT};          // cave results

// Directions: up, right, down, left
const int8_t DIR_OFF[4] = {-CW, 1, CW, -1};

// ===================================================================================
// Game Variables
// ===================================================================================
uint8_t  cave[CW * CH];       // the cave
uint8_t  buf[128];            // one display page

uint8_t  caveNum, loop, lives;
uint32_t score, hiscore, nextLife;
uint8_t  need, collected, value, extra, exitOpen;
uint16_t timeLeft;
uint8_t  frame, tick, startTicks, flash, result, alive;
uint8_t  playerX, playerY, faceLeft, walkAnim, pushCount, joyDir, joyHeld, joyFire;
uint16_t scanPos;
int16_t  camX, camY;
uint8_t  scrollX, scrollY;

const char *msgText;
uint8_t  msgNum, msgTimer;

// ===================================================================================
// Function Prototypes
// ===================================================================================
void TitleScreen(void);
void GameOverScreen(void);
uint8_t PlayCave(void);
void LoadCave(void);
void CaveTick(void);
void Explode(uint16_t pos, uint8_t diamonds);
void PlayerMove(uint16_t pos);
void FallingObject(uint16_t pos, uint8_t c);
void Creature(uint16_t pos, uint8_t c);
void ReadJoypad(void);
void UpdateCamera(uint8_t instant);
void AddScore(uint16_t points);
void ShowMessage(const char *text, uint8_t num, uint8_t time);
void Tiny_Flip(uint8_t mode);
void WaitFrame(uint32_t start);

// ===================================================================================
// Main Function
// ===================================================================================
int main(void) {
  // Setup
  JOY_init();

  // Loop
  while(1) {
    TitleScreen();
    score = 0; nextLife = EXTRA_LIFE; lives = LIVES;
    caveNum = 0; loop = 0;
    while(lives) {
      uint8_t r = PlayCave();
      if(r == R_EXIT) {
        if(++caveNum >= CAVES) {caveNum = 0; if(loop < 5) loop++;}
      }
      else lives--;
    }
    GameOverScreen();
  }
}

// ===================================================================================
// Screens
// ===================================================================================
enum {M_TITLE, M_GAME, M_GAMEOVER};

void TitleScreen(void) {
  caveNum = 0; LoadCave();                        // background: the first cave
  camX = 0; camY = 0;
  while(JOY_act_pressed());
  for(frame = 0; ; frame++) {
    uint32_t start = STK->CNT;
    Tiny_Flip(M_TITLE);
    if(JOY_act_pressed()) break;
    JOY_random();
    if(!(frame & 1) && camX < CAM_MAX_X) camX++;  // slowly pan through the cave
    WaitFrame(start);
  }
  JOY_sound(150, 30); JOY_sound(200, 30); JOY_sound(250, 50);
}

void GameOverScreen(void) {
  if(score > hiscore) hiscore = score;
  JOY_sound(120, 80); JOY_sound(90, 80); JOY_sound(60, 150);
  for(frame = 0; ; frame++) {
    uint32_t start = STK->CNT;
    Tiny_Flip(M_GAMEOVER);
    if(frame > 60 && JOY_act_pressed()) break;
    if(frame > 250) frame = 61;
    WaitFrame(start);
  }
}

void WaitFrame(uint32_t start) {
  while((int32_t)(STK->CNT - start) < FRAME_TIME);
}

void AddScore(uint16_t points) {
  score += points;
  if(score >= nextLife) {
    nextLife += EXTRA_LIFE;
    if(lives < 9) lives++;
    JOY_sound(200, 30); JOY_sound(240, 40);
  }
}

void ShowMessage(const char *text, uint8_t num, uint8_t time) {
  msgText = text; msgNum = num; msgTimer = time;
}

// ===================================================================================
// Cave
// ===================================================================================

// Build the cave from the run length encoded data; random dirt is resolved with
// a fixed seed per cave, so every cave always looks the same
void LoadCave(void) {
  const uint8_t *p = &CAVE_PARAM[caveNum << 3];
  const uint8_t *d = CAVE_DATA[caveNum];
  uint16_t rnd = p[6] | (p[7] << 8);
  uint8_t x, y;
  need = p[0]; value = p[1]; extra = p[2];
  timeLeft = p[3] - (loop << 3);                  // a bit less time in every loop
  for(y = 0; y < CH; y++)
    for(x = 0; x < CW; x++) cave[IDX(x, y)] = C_STEEL;
  uint8_t n = 0, code = 0;
  for(y = 1; y < CH - 1; y++) {
    for(x = 1; x < CW - 1; x++) {
      if(!n) {code = *d >> 4; n = (*d++ & 15) + 1;}
      n--;
      uint8_t c;
      switch(code) {
        case 0:  c = C_EMPTY; break;
        case 1:                                   // random dirt
          rnd ^= rnd << 7; rnd ^= rnd >> 9; rnd ^= rnd << 8;
          c = ((uint8_t)rnd < p[4]) ? C_BOULDER : ((uint8_t)rnd < p[4] + p[5]) ? C_DIAMOND : C_DIRT;
          break;
        case 2:  c = C_WALL; break;
        case 3:  c = C_STEEL; break;
        case 4:  c = C_BOULDER; break;
        case 5:  c = C_DIAMOND; break;
        case 6:  c = C_INBOX; playerX = x; playerY = y; break;
        case 7:  c = C_EXIT; break;
        case 8:  c = C_FIREFLY | (3 << 5); break;   // creatures start moving left
        case 9:  c = C_BUTTERFLY | (3 << 5); break;
        default: c = C_DIRT; break;
      }
      cave[IDX(x, y)] = c;
    }
  }
  collected = 0; exitOpen = 0; startTicks = START_TICKS;
  faceLeft = 0; walkAnim = 0; pushCount = 0; flash = 0;
}

uint8_t PlayCave(void) {
  uint8_t endTimer = 0, sec = 0;
  LoadCave();
  result = R_PLAY; tick = 0;
  UpdateCamera(1);
  ShowMessage("CAVE", caveNum + 1 + loop * CAVES, 60);
  JOY_sound(200, 20); JOY_sound(220, 20);
  while(JOY_act_pressed());
  for(frame = 0; ; frame++) {
    uint32_t start = STK->CNT;
    ReadJoypad();
    if(++tick >= TICK_FRAMES) {
      tick = 0;
      if(result != R_EXIT) CaveTick();
      if(!joyHeld) joyDir = 0xFF;                 // released: forget it after the tick
    }

    if(result == R_PLAY) {
      // time runs out
      if(!startTicks && ++sec >= 30) {
        sec = 0;
        if(timeLeft) timeLeft--;
        if(timeLeft && timeLeft <= 10) JOY_sound(250, 6);
        if(!timeLeft) {
          ShowMessage("OUT OF TIME", 0, 90);
          Explode(IDX(playerX, playerY), 0);
          result = R_TIMEOUT; endTimer = 90;
        }
      }
      if(!startTicks && !alive) {result = R_DEAD; endTimer = 75;}
    }
    else if(result == R_EXIT) {
      // count the remaining time into the score
      if(timeLeft) {
        uint8_t k = timeLeft > 3 ? 3 : timeLeft;
        timeLeft -= k; AddScore(k);
        if(frame & 1) JOY_sound(180 + (timeLeft & 63), 3);
      }
      else if(!endTimer) endTimer = 40;
    }
    if(endTimer && --endTimer == 0) return result == R_EXIT ? R_EXIT : R_DEAD;
    if(result == R_DEAD || result == R_TIMEOUT) {
      if(endTimer < 45 && JOY_act_pressed()) return R_DEAD;
    }

    UpdateCamera(0);
    Tiny_Flip(M_GAME);
    if(msgTimer) msgTimer--;
    if(flash) flash--;
    WaitFrame(start);
  }
}

// The joypad is read every frame; a short press between two ticks is kept
void ReadJoypad(void) {
  uint8_t d = 0xFF;
  if(JOY_up_pressed())         d = 0;
  else if(JOY_down_pressed())  d = 2;
  else if(JOY_left_pressed())  d = 3;
  else if(JOY_right_pressed()) d = 1;
  joyHeld = (d != 0xFF);
  if(joyHeld) joyDir = d;
  joyFire = JOY_act_pressed();
}

// One step of the cave: everything is scanned from top left to bottom right.
// Objects that move down or right get the MOVED flag, so they are not moved twice.
void CaveTick(void) {
  if(startTicks) startTicks--;
  alive = 0;
  for(scanPos = CW + 1; scanPos < CW * (CH - 1) - 1; scanPos++) {
    uint8_t c = cave[scanPos];
    if(c & MOVED) {
      cave[scanPos] = c & ~MOVED;
      if(TYPE(c) == C_PLAYER) alive = 1;
      continue;
    }
    switch(TYPE(c)) {
      case C_PLAYER:
        alive = 1;
        PlayerMove(scanPos);
        break;
      case C_BOULDER: case C_BOULDER_F: case C_DIAMOND: case C_DIAMOND_F:
        FallingObject(scanPos, c);
        break;
      case C_FIREFLY: case C_BUTTERFLY:
        Creature(scanPos, c);
        break;
      case C_INBOX:
        if(!startTicks) {cave[scanPos] = C_PLAYER; alive = 1; JOY_sound(230, 10);}
        break;
      case C_EXPL1: case C_EXPL2: case C_DEXPL1: case C_DEXPL2:
        cave[scanPos] = c + 1;
        break;
      case C_EXPL3:  cave[scanPos] = C_EMPTY;   break;
      case C_DEXPL3: cave[scanPos] = C_DIAMOND; break;
    }
  }
  if(startTicks) alive = 1;

  // enough diamonds: open the exit
  if(!exitOpen && collected >= need) {
    exitOpen = 1; flash = 3;
    for(uint16_t i = 0; i < CW * CH; i++) if(cave[i] == C_EXIT) cave[i] = C_EXIT_OPEN;
    JOY_sound(250, 20); JOY_sound(200, 20); JOY_sound(250, 20);
  }
}

// Set a cell; cells behind the scan position are flagged, so they wait a tick
void SetCell(uint16_t pos, uint8_t c) {
  cave[pos] = (pos > scanPos) ? (c | MOVED) : c;
}

void Explode(uint16_t pos, uint8_t diamonds) {
  for(int8_t dy = -CW; dy <= CW; dy += CW) {
    for(int8_t dx = -1; dx <= 1; dx++) {
      uint16_t p = pos + dy + dx;
      uint8_t t = TYPE(cave[p]);
      if(t == C_STEEL || t == C_EXIT || t == C_EXIT_OPEN || t == C_INBOX) continue;
      SetCell(p, diamonds ? C_DEXPL1 : C_EXPL1);
    }
  }
  JOY_sound(40, 8); JOY_sound(90, 6); JOY_sound(30, 10);
}

void PlayerMove(uint16_t pos) {
  playerX = pos % CW; playerY = pos / CW;
  if(result != R_PLAY) return;
  if(joyDir > 3) {pushCount = 0; return;}
  uint8_t d = joyDir;
  uint16_t t = pos + DIR_OFF[d];
  uint8_t c = TYPE(cave[t]);
  if(d == 1) faceLeft = 0;
  if(d == 3) faceLeft = 1;

  // collect a diamond
  if(c == C_DIAMOND || c == C_DIAMOND_F) {
    collected++;
    AddScore(exitOpen ? extra : value);
    JOY_sound(240, 4);
    c = C_EMPTY; cave[t] = C_EMPTY;
  }
  if(joyFire) {                                   // dig without moving
    if(c == C_DIRT) cave[t] = C_EMPTY;
    return;
  }
  if(c == C_EMPTY || c == C_DIRT || c == C_EXIT_OPEN) {
    cave[pos] = C_EMPTY;
    SetCell(t, C_PLAYER);
    walkAnim++;
    pushCount = 0;
    if(c == C_DIRT && (walkAnim & 1)) JOY_sound(20, 1);
    if(c == C_EXIT_OPEN) {
      cave[t] = C_EXIT;                           // the player is gone
      result = R_EXIT; alive = 1;
      ShowMessage("WELL DONE", 0, 120);
      JOY_sound(150, 30); JOY_sound(200, 30); JOY_sound(250, 60);
    }
    playerX = t % CW; playerY = t / CW;
    return;
  }
  // push a boulder sideways (it takes a short moment)
  if(c == C_BOULDER && (d & 1)) {
    uint16_t b = t + DIR_OFF[d];
    if(cave[b] == C_EMPTY && ++pushCount >= 2) {
      SetCell(b, C_BOULDER);
      cave[pos] = C_EMPTY;
      SetCell(t, C_PLAYER);
      pushCount = 0; walkAnim++;
      playerX = t % CW;
      JOY_sound(50, 3);
    }
    return;
  }
  pushCount = 0;
}

// Round objects: boulders, diamonds and brick walls let other things roll off
static uint8_t Rounded(uint8_t c) {
  c = TYPE(c);
  return c == C_BOULDER || c == C_DIAMOND || c == C_WALL;
}

void FallingObject(uint16_t pos, uint8_t c) {
  uint8_t still = (c == C_BOULDER_F || c == C_BOULDER) ? C_BOULDER : C_DIAMOND;
  uint8_t falling = still + 1;
  uint16_t below = pos + CW;
  uint8_t b = cave[below];
  if(b == C_EMPTY) {                              // fall
    cave[pos] = C_EMPTY; SetCell(below, falling);
    return;
  }
  if(c == falling) {
    uint8_t bt = TYPE(b);
    if(bt == C_PLAYER || bt == C_FIREFLY || bt == C_BUTTERFLY) {
      Explode(below, bt == C_BUTTERFLY);
      return;
    }
  }
  if(Rounded(b)) {                                // roll off to the left or right
    if(cave[pos - 1] == C_EMPTY && cave[below - 1] == C_EMPTY) {
      cave[pos] = C_EMPTY; cave[pos - 1] = falling;
      return;
    }
    if(cave[pos + 1] == C_EMPTY && cave[below + 1] == C_EMPTY) {
      cave[pos] = C_EMPTY; SetCell(pos + 1, falling);
      return;
    }
  }
  if(c == falling) {                              // landed
    cave[pos] = still;
    if(still == C_BOULDER) JOY_sound(30, 2); else JOY_sound(250, 2);
  }
}

// Fireflies follow the wall on their left, butterflies the wall on their right.
// Touching the player makes them explode.
void Creature(uint16_t pos, uint8_t c) {
  uint8_t t = TYPE(c), d = DIR(c);
  for(uint8_t i = 0; i < 4; i++) {
    if(TYPE(cave[pos + DIR_OFF[i]]) == C_PLAYER) {
      Explode(pos, t == C_BUTTERFLY);
      return;
    }
  }
  uint8_t turn = (t == C_FIREFLY) ? 3 : 1;        // preferred turn: left or right
  uint8_t nd = (d + turn) & 3;
  uint16_t np = pos + DIR_OFF[nd];
  if(cave[np] == C_EMPTY) {
    cave[pos] = C_EMPTY; SetCell(np, t | (nd << 5));
    return;
  }
  np = pos + DIR_OFF[d];
  if(cave[np] == C_EMPTY) {
    cave[pos] = C_EMPTY; SetCell(np, c);
    return;
  }
  cave[pos] = t | (((d - turn) & 3) << 5);       // turn the other way
}

// The camera follows the player smoothly and only when he comes near the edge
void UpdateCamera(uint8_t instant) {
  int16_t tx = (playerX << 3) - 60, ty = (playerY << 3) - 24;
  if(tx < 0) tx = 0;
  if(tx > CAM_MAX_X) tx = CAM_MAX_X;
  if(ty < 0) ty = 0;
  if(ty > CAM_MAX_Y) ty = CAM_MAX_Y;
  if(instant) {camX = tx; camY = ty; return;}
  int16_t dx = tx - camX, dy = ty - camY;
  if(dx > 32 || dx < -32) scrollX = 1;
  if(dy > 12 || dy < -12) scrollY = 1;
  if(scrollX) {
    int8_t s = (dx > 24 || dx < -24) ? 3 : 2;
    if(dx > s) camX += s; else if(dx < -s) camX -= s; else {camX = tx; scrollX = 0;}
  }
  if(scrollY) {
    int8_t s = (dy > 16 || dy < -16) ? 3 : 2;
    if(dy > s) camY += s; else if(dy < -s) camY -= s; else {camY = ty; scrollY = 0;}
  }
}

// ===================================================================================
// Rendering
// ===================================================================================

// Tile graphics for a cave cell
const uint8_t *CellTile(uint16_t pos) {
  uint8_t c = cave[pos], t = TYPE(c), s;
  switch(t) {
    case C_EMPTY:     s = T_BLANK; break;
    case C_DIRT:      s = T_DIRT; break;
    case C_WALL:      s = T_WALL; break;
    case C_BOULDER: case C_BOULDER_F: s = T_BOULDER; break;
    case C_DIAMOND: case C_DIAMOND_F:
      s = (((frame >> 2) + pos) & 7) ? T_DIAM0 : T_DIAM1; break;
    case C_PLAYER:
      if(joyDir == 1 || joyDir == 3) s = (faceLeft ? T_MANL0 : T_MANR0) + (walkAnim & 1);
      else s = T_MAN;
      break;
    case C_INBOX:     s = (frame & 4) ? T_EXIT : T_STEEL; break;
    case C_EXIT_OPEN: s = (frame & 8) ? T_EXIT : T_STEEL; break;
    case C_FIREFLY:   s = (frame & 4) ? T_FIRE1 : T_FIRE0; break;
    case C_BUTTERFLY: s = (frame & 4) ? T_BUTT1 : T_BUTT0; break;
    case C_EXPL1: case C_DEXPL1: s = T_EXPL0; break;
    case C_EXPL2: case C_DEXPL2: s = T_EXPL1; break;
    case C_EXPL3: case C_DEXPL3: s = T_EXPL2; break;
    default:          s = T_STEEL; break;   // steel and closed exit look the same
  }
  return &TILES[s << 3];
}

// Draw one page (8 pixel rows) of the cave with the camera position
void DrawCave(uint8_t page) {
  int16_t wy = camY + ((page - 1) << 3);
  uint8_t ty = wy >> 3, sy = wy & 7;
  uint8_t ty2 = (ty + 1 < CH) ? ty + 1 : ty;
  const uint8_t *a = 0, *b = 0;
  for(uint8_t x = 0; x < 128; x++) {
    int16_t wx = camX + x;
    uint8_t cx = wx & 7;
    if(!cx || !x) {
      uint8_t tx = wx >> 3;
      a = CellTile(IDX(tx, ty));
      b = CellTile(IDX(tx, ty2));
    }
    buf[x] = sy ? (a[cx] >> sy) | (b[cx] << (8 - sy)) : a[cx];
  }
}

// OR one byte column into the page buffer at pixel row y
void DrawColumn(uint8_t page, int16_t x, int16_t y, uint8_t b) {
  int16_t row = y - (page << 3);
  if(x < 0 || x > 127 || row <= -8 || row >= 8) return;
  buf[x] |= (row >= 0) ? (b << row) : (b >> -row);
}

// 5x7 font, scale 1 or 2, with a black box behind the letters
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

// Number with a fixed count of digits: 3x5 digits in the status line (big = 0)
// or the 5x7 font (big = 1)
void DrawNumber(uint8_t page, int16_t x, int8_t y, uint32_t n, uint8_t digits, uint8_t big) {
  char s[8];
  s[digits] = 0;
  while(digits--) {s[digits] = '0' + n % 10; n /= 10;}
  if(big) {DrawText(page, x, y, s, 1); return;}
  for(char *p = s; *p; p++, x += 4)
    for(uint8_t i = 0; i < 3; i++) buf[x + i] |= DIGITS[(*p - '0') * 3 + i] << 1;
}

void DrawIcon(uint8_t x, const uint8_t *icon) {
  for(uint8_t i = 0; i < 5; i++) buf[x + i] |= icon[i];
}

// Status line: diamonds collected/needed, time, lives, score
void DrawStatus(void) {
  DrawIcon(0, ICON_DIAMOND);
  if(exitOpen && (frame & 8)) {
    for(uint8_t i = 0; i < 5; i++) buf[i] ^= 0x7F;
  }
  DrawNumber(0, 7, 0, collected, 2, 0);
  for(uint8_t i = 0; i < 3; i++) buf[15 + i] |= DIGITS[30 + i] << 1;    // '/'
  DrawNumber(0, 19, 0, need, 2, 0);
  if(timeLeft > 10 || (frame & 8)) {
    DrawIcon(38, ICON_CLOCK);
    DrawNumber(0, 45, 0, timeLeft, 3, 0);
  }
  DrawIcon(66, ICON_LIFE);
  DrawNumber(0, 73, 0, lives, 1, 0);
  DrawNumber(0, 104, 0, score, 6, 0);
}

// Draw the whole screen page by page and send it to the OLED
void Tiny_Flip(uint8_t mode) {
  for(uint8_t page = 0; page < 8; page++) {
    if(page == 0 && mode != M_TITLE) {
      for(uint8_t x = 0; x < 128; x++) buf[x] = 0;
      DrawStatus();
    }
    else {
      if(mode != M_TITLE) DrawCave(page);
      else if(page == 0 || page == 7) DrawCave(page + 1);   // title: cave strips
      else for(uint8_t x = 0; x < 128; x++) buf[x] = 0;
      if(flash) for(uint8_t x = 0; x < 128; x++) buf[x] = ~buf[x];
    }

    if(mode == M_TITLE) {
      DrawText(page, 40, 10, "TINY", 2);
      DrawText(page, 22, 27, "BOULDER", 2);
      if(frame & 16) DrawText(page, 34, 45, "PRESS FIRE", 1);
      DrawText(page, 37, 54, "HI", 1);
      DrawNumber(page, 55, 54, hiscore, 6, 1);
    }
    else if(mode == M_GAMEOVER) {
      DrawText(page, 10, 18, "GAME OVER", 2);
      DrawText(page, 25, 42, "SCORE", 1);
      DrawNumber(page, 61, 42, score, 6, 1);
    }
    else if(msgTimer && (msgTimer > 15 || (msgTimer & 4))) {
      uint8_t len = 0;
      while(msgText[len]) len++;
      if(msgNum) len += 2 + (msgNum > 9);
      int16_t tx = 64 - len * 3;
      DrawText(page, tx, 32, msgText, 1);
      if(msgNum) {
        int16_t nx = tx + (len - 1 - (msgNum > 9)) * 6;
        DrawText(page, nx - 6, 32, " ", 1);
        DrawNumber(page, nx, 32, msgNum, 1 + (msgNum > 9), 1);
      }
    }

    // Send page to OLED
    JOY_OLED_data_start(page);
    for(uint8_t x = 0; x < 128; x++) JOY_OLED_send(buf[x]);
    JOY_OLED_end();
  }
}
