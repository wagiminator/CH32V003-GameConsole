// ===================================================================================
// Project:   Tiny Bubble - Bubble Shooting Platform Game for CH32V003
// Version:   v1.0
// Year:      2026
// Github:    https://github.com/wagiminator
// EasyEDA:   https://easyeda.com/wagiminator
// License:   http://creativecommons.org/licenses/by-sa/3.0/
// ===================================================================================
//
// Description:
// ------------
// A small Bubble Bobble-like platform game for the CH32V003 Mini Game Console. You
// are a little dragon who blows bubbles. Catch the monsters in bubbles and burst
// the bubbles to defeat them; burst several at once for a combo bonus. Defeated
// monsters turn into fruit. Bounce on bubbles by holding the jump button. Fall
// through the gap in the floor to come back at the top. Clear all monsters to
// reach the next round, but hurry: after a while the monsters get angry and the
// invincible skull comes for you.
//
// Controls:
// ---------
// Left/Right:  walk
// Up:          jump (hold it to bounce on bubbles)
// Fire button: blow a bubble, start game
//
// Scoring:
// --------
// Monster 1000 (doubled for every further monster in the same burst),
// fruit 500, empty bubble 10. Extra life every 20000 points.
//
// Compilation Settings:
// ---------------------
// - Make sure GCC toolchain (gcc-riscv64-unknown-elf, newlib) and Python3 with rvprog
//   are installed. In addition, Linux requires access rights to WCH-LinkE programmer.
// - Connect the WCH-LinkE programmer to the MCU board.
// - Run 'make flash'.


#include <driver.h>           // TinyJoypad conversion driver
#include <spritebank.h>       // grafix and rounds

// ===================================================================================
// Game Settings
// ===================================================================================
#define FRAME_TIME    (33 * DLY_MS_TIME)  // ~30 frames per second
#define SUB           4       // positions in 1/16 pixels
#define FIELD_W       128     // playfield size in pixels (below the score line)
#define FIELD_H       56
#define LEFT_WALL     4       // inner side of the side walls
#define RIGHT_WALL    116     // rightmost sprite position
#define GRAVITY       3
#define JUMP_SPEED    (-46)
#define MAX_FALL      36
#define WALK_SPEED    14      // player: ~0.9 pixels per frame
#define MAX_ENEMIES   8
#define MAX_BUBBLES   6
#define MAX_FRUITS    6
#define LIVES         3
#define HURRY_TIME    (30 * 30)   // frames until the monsters get angry
#define BARON_TIME    (40 * 30)   // frames until the skull comes
#define EXTRA_LIFE    20000

enum {E_NONE, E_WALKER, E_FLYER};                 // enemy types
enum {B_FREE, B_SHOT, B_FLOAT, B_POP};            // bubble states
enum {R_TITLE, R_GAME, R_GAMEOVER};               // render modes

#define P(v)          ((int16_t)((v) * (1 << SUB)))      // pixels to positions
#define PX(v)         ((v) >> SUB)                // positions to pixels

// ===================================================================================
// Game Objects
// ===================================================================================
typedef struct {
  int16_t x, y;               // position (1/16 pixels)
  int8_t  vx, vy;             // speed
  uint8_t type;               // E_NONE: free
  uint8_t angry, ground, anim;
} ENEMY;

typedef struct {
  int16_t x, y;
  int8_t  vx;
  uint8_t state;
  uint8_t timer;              // lifetime
  uint8_t trapped;            // monster inside: type | 0x80 when angry
  uint8_t pop;                // marked for bursting
} BUBBLE;

typedef struct {
  int16_t x, y;
  int8_t  vy;
  uint8_t type;               // sprite, 0: free
  uint8_t timer;
} FRUIT;

ENEMY    enemy[MAX_ENEMIES];
BUBBLE   bubble[MAX_BUBBLES];
FRUIT    fruit[MAX_FRUITS];
uint8_t  buf[128];            // one display page

// Player
int16_t  bx, by;              // position
int8_t   bvy;
uint8_t  bdir, bground, banim, shootTimer, invuln, deadTimer, lives;
uint32_t score, hiscore, nextLife;

// Round
const uint32_t *roundMap;
uint8_t  rnd, loop, frame, clearTimer, gameOver;
uint16_t roundTime;
uint8_t  baronOn;
int16_t  baronX, baronY;

// Message in the middle of the screen
const char *msgText;
uint8_t  msgNum, msgTimer;

// ===================================================================================
// Function Prototypes
// ===================================================================================
void TitleScreen(void);
void GameOverScreen(void);
void NewRound(void);
void UpdateGame(void);
void PlayerUpdate(void);
void PlayerHit(void);
void ShootBubble(void);
void BubblesUpdate(void);
void BurstBubbles(void);
void EnemiesUpdate(void);
void FruitsUpdate(void);
void BaronUpdate(void);
void AddScore(uint32_t points);
void ShowMessage(const char *text, uint8_t num, uint8_t time);
void Tiny_Flip(uint8_t mode);
void WaitFrame(uint32_t start);
void DrawText(uint8_t page, int16_t x, int8_t y, const char *s, uint8_t scale);

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
    rnd = 0; loop = 0; gameOver = 0;
    NewRound();
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

// Title screen with bubbles floating up
void TitleScreen(void) {
  uint8_t i;
  roundMap = ROUND_MAP[0];
  for(i = 0; i < MAX_ENEMIES; i++) enemy[i].type = E_NONE;
  for(i = 0; i < MAX_FRUITS; i++)  fruit[i].type = 0;
  for(i = 0; i < MAX_BUBBLES; i++) {
    bubble[i].state = B_FLOAT; bubble[i].trapped = (i & 1) ? E_WALKER : 0;
    bubble[i].x = P(10 + i * 19); bubble[i].y = P(10 + i * 9); bubble[i].timer = 255;
  }
  baronOn = 0; frame = 0;
  while(JOY_act_pressed());
  while(1) {
    uint32_t start = STK->CNT;
    frame++;
    for(i = 0; i < MAX_BUBBLES; i++) {             // bubbles rise and come back below
      bubble[i].y -= 6;
      if(bubble[i].y < P(-8)) {bubble[i].y = P(FIELD_H); bubble[i].x = P(8 + (JOY_random() % 104));}
    }
    Tiny_Flip(R_TITLE);
    if(JOY_act_pressed()) break;
    JOY_random();
    WaitFrame(start);
  }
  JOY_sound(150, 40); JOY_sound(200, 40); JOY_sound(250, 60);
}

void GameOverScreen(void) {
  if(score > hiscore) hiscore = score;
  JOY_sound(120, 80); JOY_sound(90, 80); JOY_sound(60, 150);
  for(frame = 0; ; frame++) {
    uint32_t start = STK->CNT;
    Tiny_Flip(R_GAMEOVER);
    if(frame > 60 && JOY_act_pressed()) break;
    if(frame > 250) frame = 61;
    WaitFrame(start);
  }
}

void WaitFrame(uint32_t start) {
  while((int32_t)(STK->CNT - start) < FRAME_TIME);
}

// ===================================================================================
// Rounds
// ===================================================================================

// Is the cell (cx, cy) a platform? Rows outside the field are empty.
uint8_t Solid(int8_t cx, int8_t cy) {
  if(cy < 0 || cy > 13 || cx < 0 || cx > 31) return 0;
  return (roundMap[cy] >> cx) & 1;
}

void NewRound(void) {
  uint8_t i;
  const uint8_t *s = ROUND_SPAWN[rnd];
  roundMap = ROUND_MAP[rnd];
  for(i = 0; i < MAX_BUBBLES; i++) bubble[i].state = B_FREE;
  for(i = 0; i < MAX_FRUITS; i++)  fruit[i].type = 0;
  for(i = 0; i < MAX_ENEMIES; i++) {
    ENEMY *e = &enemy[i];
    e->type = E_NONE;
    if(*s) {
      e->type = *s++;
      e->x = P(*s++ * 4); e->y = P(*s++ * 4 - 4);
      e->vx = (JOY_random() & 1) ? 8 : -8;
      e->vy = (e->type == E_FLYER) ? 8 : 0;
      e->angry = 0; e->ground = 0; e->anim = 0;
    }
  }
  bx = P(ROUND_START[rnd << 1] * 4); by = P(ROUND_START[(rnd << 1) + 1] * 4 - 4);
  bvy = 0; bdir = 1; invuln = 60; deadTimer = 0; shootTimer = 0;
  roundTime = 0; clearTimer = 0; baronOn = 0;
  ShowMessage("ROUND", rnd + 1 + loop * ROUNDS, 60);
  JOY_sound(200, 30); JOY_sound(220, 30);
}

void UpdateGame(void) {
  frame++;
  if(roundTime < 0xFFFF) roundTime++;

  PlayerUpdate();
  BubblesUpdate();
  EnemiesUpdate();
  FruitsUpdate();
  BaronUpdate();

  // Hurry up: angry monsters, then the skull
  if(roundTime == HURRY_TIME) {
    ShowMessage("HURRY UP!", 0, 60);
    for(uint8_t i = 0; i < MAX_ENEMIES; i++) enemy[i].angry = 1;
    JOY_sound(250, 30); JOY_sound(250, 30);
  }
  if(roundTime == BARON_TIME) {
    baronOn = 1; baronX = P(60); baronY = P(-8);
    JOY_sound(40, 60);
  }

  // Round cleared when no monster is left (free or in a bubble)
  uint8_t left = 0;
  for(uint8_t i = 0; i < MAX_ENEMIES; i++) if(enemy[i].type) left++;
  for(uint8_t i = 0; i < MAX_BUBBLES; i++) if(bubble[i].state && (bubble[i].trapped & 3)) left++;
  if(!left && !clearTimer) {clearTimer = 100; baronOn = 0;}
  if(clearTimer && --clearTimer == 0) {
    JOY_sound(150, 40); JOY_sound(200, 40); JOY_sound(250, 80);
    if(++rnd >= ROUNDS) {rnd = 0; if(loop < 9) loop++;}
    NewRound();
  }

  if(msgTimer) msgTimer--;
}

void AddScore(uint32_t points) {
  score += points;
  if(score >= nextLife) {
    nextLife += EXTRA_LIFE;
    if(lives < 9) lives++;
    JOY_sound(200, 40); JOY_sound(240, 60);
  }
}

void ShowMessage(const char *text, uint8_t num, uint8_t time) {
  msgText = text; msgNum = num; msgTimer = time;
}

uint8_t Collide(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t d) {
  return (PX(x1) - PX(x2) < d) && (PX(x2) - PX(x1) < d) && (PX(y1) - PX(y2) < d) && (PX(y2) - PX(y1) < d);
}

// ===================================================================================
// Platform Physics (player, walkers and fruit)
// ===================================================================================

// Fall and land on platforms (only from above). Returns 1 when standing.
uint8_t FallAndLand(int16_t x, int16_t *y, int8_t *vy) {
  int16_t oldFeet = PX(*y) + 8;
  if(*vy < MAX_FALL) *vy += GRAVITY;
  *y += *vy;
  int16_t feet = PX(*y) + 8;
  if(*vy >= 0) {
    // every tile top between the old and the new feet position
    // (row 0 is the ceiling: it has the same tiles as the floor row but can't be stood on)
    int16_t t = (oldFeet + 3) >> 2;
    if(t < 1) t = 1;
    for(; (t << 2) <= feet; t++) {
      int8_t cx0 = (PX(x) + 2) >> 2, cx1 = (PX(x) + 5) >> 2;
      if(Solid(cx0, t) || Solid(cx1, t)) {
        *y = P((t << 2) - 8); *vy = 0;
        return 1;
      }
    }
  }
  // fell through the floor gap: come back at the top
  if(PX(*y) >= FIELD_H) *y -= P(FIELD_H + 8);
  return 0;
}

// Standing on a platform at the current position?
uint8_t OnPlatform(int16_t x, int16_t y) {
  int16_t feet = PX(y) + 8;
  if(feet & 3) return 0;
  int8_t t = feet >> 2;
  return Solid((PX(x) + 2) >> 2, t) || Solid((PX(x) + 5) >> 2, t);
}

// ===================================================================================
// Player
// ===================================================================================
void PlayerUpdate(void) {
  if(deadTimer) {
    if(--deadTimer == 0) {
      if(--lives == 0) {gameOver = 1; return;}
      bx = P(ROUND_START[rnd << 1] * 4); by = P(ROUND_START[(rnd << 1) + 1] * 4 - 4);
      bvy = 0; invuln = 90;
    }
    return;
  }
  if(invuln) invuln--;

  // Walk
  if(JOY_left_pressed())  {bx -= WALK_SPEED; bdir = 0; banim++;}
  if(JOY_right_pressed()) {bx += WALK_SPEED; bdir = 1; banim++;}
  if(bx < P(LEFT_WALL))  bx = P(LEFT_WALL);
  if(bx > P(RIGHT_WALL)) bx = P(RIGHT_WALL);

  // Jump and fall
  if(bground && JOY_up_pressed()) {bvy = JUMP_SPEED; bground = 0; JOY_sound(180, 6);}
  if(!bground || !OnPlatform(bx, by)) bground = FallAndLand(bx, &by, &bvy);

  // Blow bubbles
  if(shootTimer) shootTimer--;
  if(JOY_act_pressed() && !shootTimer) {ShootBubble(); shootTimer = 10;}

  // Monsters and the skull
  if(!invuln) {
    for(uint8_t i = 0; i < MAX_ENEMIES; i++)
      if(enemy[i].type && Collide(bx, by, enemy[i].x, enemy[i].y, 6)) PlayerHit();
    if(baronOn && Collide(bx, by, baronX, baronY, 6)) PlayerHit();
  }
}

void PlayerHit(void) {
  if(deadTimer) return;
  deadTimer = 50;
  JOY_sound(120, 40); JOY_sound(80, 50); JOY_sound(40, 80);
}

void ShootBubble(void) {
  for(uint8_t i = 0; i < MAX_BUBBLES; i++) {
    BUBBLE *b = &bubble[i];
    if(b->state) continue;
    b->state = B_SHOT; b->trapped = 0; b->pop = 0;
    b->x = bx + (bdir ? P(4) : P(-4)); b->y = by;
    b->vx = bdir ? 48 : -48;
    b->timer = 12;
    JOY_sound(230, 4);
    return;
  }
}

// ===================================================================================
// Bubbles
// ===================================================================================
void BubblesUpdate(void) {
  uint8_t i, j;
  for(i = 0; i < MAX_BUBBLES; i++) {
    BUBBLE *b = &bubble[i];
    switch(b->state) {
      case B_SHOT:                                // fly sideways, catch monsters
        b->x += b->vx;
        if(b->x < P(LEFT_WALL)) b->x = P(LEFT_WALL);
        if(b->x > P(RIGHT_WALL)) b->x = P(RIGHT_WALL);
        for(j = 0; j < MAX_ENEMIES; j++) {
          ENEMY *e = &enemy[j];
          if(e->type && Collide(b->x, b->y, e->x, e->y, 7)) {
            b->trapped = e->type | (e->angry ? 0x80 : 0);
            e->type = E_NONE;
            b->timer = 1;
            JOY_sound(200, 8); JOY_sound(150, 8);
            break;
          }
        }
        if(--b->timer == 0) {
          b->state = B_FLOAT;
          b->timer = b->trapped ? 160 : 120;      // counted every other frame
        }
        break;

      case B_FLOAT:                               // rise, then drift to the middle
        if(PX(b->y) > 4) b->y -= 8;
        else {
          if(b->x < P(60)) b->x += 4; else if(b->x > P(60)) b->x -= 4;
        }
        if(frame & 1) {
          if(--b->timer == 0) {
            if(b->trapped) {                      // the monster breaks free, angry
              for(j = 0; j < MAX_ENEMIES; j++) {
                ENEMY *e = &enemy[j];
                if(e->type) continue;
                e->type = b->trapped & 3; e->x = b->x; e->y = b->y;
                e->vx = (JOY_random() & 1) ? 14 : -14; e->vy = (e->type == E_FLYER) ? 14 : 0;
                e->angry = 1; e->ground = 0;
                break;
              }
            }
            b->state = B_POP; b->timer = 4; b->trapped = 0;
          }
        }
        break;

      case B_POP:                                 // short burst animation
        if(--b->timer == 0) b->state = B_FREE;
        break;
    }
  }

  // The player and the bubbles: bounce on top (jump held) or burst them
  if(deadTimer) return;
  for(i = 0; i < MAX_BUBBLES; i++) {
    BUBBLE *b = &bubble[i];
    if(b->state != B_FLOAT && !(b->state == B_SHOT && b->timer < 8)) continue;
    if(!Collide(bx, by, b->x, b->y, 7)) continue;
    if(bvy > 0 && PX(by) + 8 <= PX(b->y) + 5 && JOY_up_pressed()) {
      bvy = JUMP_SPEED; bground = 0;
      JOY_sound(220, 4);
    }
    else b->pop = 1;
  }
  BurstBubbles();
}

// Burst all marked bubbles and the ones touching them (chain reaction)
void BurstBubbles(void) {
  uint8_t i, j, chain, combo = 0, any = 0;
  do {
    chain = 0;
    for(i = 0; i < MAX_BUBBLES; i++) {
      BUBBLE *b = &bubble[i];
      if(!b->pop) continue;
      b->pop = 0; any = 1;
      if(b->trapped) {                            // monster defeated: fruit drops
        AddScore(1000UL << (combo < 5 ? combo : 5));
        combo++;
        for(j = 0; j < MAX_FRUITS; j++) {
          FRUIT *f = &fruit[j];
          if(f->type) continue;
          f->type = SPR_CHERRY + (JOY_random() % 3); f->x = b->x; f->y = b->y;
          f->vy = -30; f->timer = 255;
          break;
        }
      }
      else AddScore(10);
      b->state = B_POP; b->timer = 4; b->trapped = 0;
      for(j = 0; j < MAX_BUBBLES; j++) {          // neighbours burst too
        BUBBLE *n = &bubble[j];
        if((n->state == B_FLOAT || n->state == B_SHOT) && !n->pop && Collide(b->x, b->y, n->x, n->y, 11)) {n->pop = 1; chain = 1;}
      }
    }
  } while(chain);
  if(any) {
    JOY_sound(100, 6); JOY_sound(60, 6);
    if(combo > 1) ShowMessage("COMBO", combo, 40);
  }
}

// ===================================================================================
// Monsters
// ===================================================================================
void EnemiesUpdate(void) {
  for(uint8_t i = 0; i < MAX_ENEMIES; i++) {
    ENEMY *e = &enemy[i];
    if(!e->type) continue;
    int8_t speed = (e->angry ? 14 : 8) + (loop << 1);
    e->anim++;
    if(e->type == E_WALKER) {
      // walk, turn at the walls, sometimes jump up towards the player
      e->x += e->vx > 0 ? speed : -speed;
      if(e->x <= P(LEFT_WALL))  {e->x = P(LEFT_WALL);  e->vx = speed;}
      if(e->x >= P(RIGHT_WALL)) {e->x = P(RIGHT_WALL); e->vx = -speed;}
      if(e->ground) {
        // at the end of a platform: turn around or walk off
        int16_t front = PX(e->x) + (e->vx > 0 ? 7 : 0);
        if(!Solid(front >> 2, (PX(e->y) + 8) >> 2) && !(JOY_random() & 7)) e->vx = -e->vx;
        if((by < e->y - P(8) && !(JOY_random() & 63)) || !(JOY_random() & 255)) {
          e->vy = JUMP_SPEED; e->ground = 0;
        }
      }
      if(!e->ground || !OnPlatform(e->x, e->y)) e->ground = FallAndLand(e->x, &e->y, &e->vy);
    }
    else {
      // flyer: diagonal flight, bouncing off the walls
      e->x += e->vx > 0 ? speed : -speed;
      e->y += e->vy > 0 ? speed : -speed;
      if(e->x <= P(LEFT_WALL))  {e->x = P(LEFT_WALL);  e->vx = 1;}
      if(e->x >= P(RIGHT_WALL)) {e->x = P(RIGHT_WALL); e->vx = -1;}
      if(e->y <= P(4))          {e->y = P(4);          e->vy = 1;}
      if(e->y >= P(44))         {e->y = P(44);         e->vy = -1;}
    }
  }
}

void FruitsUpdate(void) {
  for(uint8_t i = 0; i < MAX_FRUITS; i++) {
    FRUIT *f = &fruit[i];
    if(!f->type) continue;
    if(!OnPlatform(f->x, f->y) || f->vy < 0) FallAndLand(f->x, &f->y, &f->vy);
    if(!deadTimer && Collide(bx, by, f->x, f->y, 7)) {
      f->type = 0;
      AddScore(500);
      JOY_sound(220, 10); JOY_sound(250, 15);
      continue;
    }
    if(--f->timer == 0) f->type = 0;
  }
}

// The skull flies straight through everything towards the player
void BaronUpdate(void) {
  if(!baronOn) return;
  int16_t s = 6 + loop;
  if(baronX < bx) baronX += s; else baronX -= s;
  if(baronY < by) baronY += s; else baronY -= s;
}

// ===================================================================================
// Rendering
// ===================================================================================

// OR one byte column into the page buffer at pixel row y
void DrawColumn(uint8_t page, int16_t x, int16_t y, uint8_t b) {
  int16_t row = y - (page << 3);
  if(x < 0 || x > 127 || row <= -8 || row >= 8) return;
  buf[x] |= (row >= 0) ? (b << row) : (b >> -row);
}

// 8x8 sprite at screen position (x, y), mirrored if needed; a black outline
// keeps it readable in front of the platforms
void DrawSprite(uint8_t page, int16_t x, int16_t y, uint8_t spr, uint8_t mirror) {
  int16_t row = y - 1 - (page << 3);              // row of the outline's top
  if(row <= -10 || row >= 8) return;
  const uint8_t *s = &SPRITES[spr << 3];
  uint8_t prev = 0, cur = 0, next = s[mirror ? 7 : 0];
  for(int8_t i = -1; i <= 8; i++) {
    int16_t cx = x + i;
    if(cx >= 0 && cx <= 127) {
      uint16_t m = prev | cur | next;
      m = (m << 2) | (m << 1) | m;                // outline mask, one pixel around
      uint16_t c = (uint16_t)cur << 1;
      buf[cx] = (row >= 0) ? ((buf[cx] & ~(m << row)) | (c << row))
                           : ((buf[cx] & ~(m >> -row)) | (c >> -row));
    }
    prev = cur; cur = next;
    next = (i + 2 <= 7) ? s[mirror ? 5 - i : i + 2] : 0;
  }
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
    for(uint8_t i = 0; i < 6 * scale; i++) {
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

// Number: 3x5 digits (big = 0) or 5x7 font (big = 1), fixed count of digits
void DrawNumber(uint8_t page, int16_t x, int8_t y, uint32_t n, uint8_t digits, uint8_t big) {
  char s[8];
  s[digits] = 0;
  while(digits--) {s[digits] = '0' + n % 10; n /= 10;}
  if(big) {DrawText(page, x, y, s, 1); return;}
  for(char *p = s; *p; p++, x += 4)
    for(uint8_t i = 0; i < 3; i++) DrawColumn(page, x + i, y, DIGITS[(*p - '0') * 3 + i]);
}

// Draw the whole screen page by page and send it to the OLED
void Tiny_Flip(uint8_t mode) {
  uint8_t page, x, i;
  const uint8_t *tile = &TILES[(rnd & 3) << 2];
  for(page = 0; page < 8; page++) {
    for(x = 0; x < 128; x++) buf[x] = 0;

    if(page == 0 && mode != R_TITLE) {
      // Score line: score, lives, round
      DrawNumber(0, 0, 1, score, 6, 0);
      for(i = 0; i < lives && i < 6; i++)
        for(x = 0; x < 5; x++) buf[30 + i * 7 + x] |= ICON_LIFE[x] << 1;
      for(x = 0; x < 3; x++) buf[113 + x] |= DIGITS[30 + x] << 1;   // R
      DrawNumber(0, 120, 1, rnd + 1 + loop * ROUNDS, 2, 0);
      if(baronOn && (frame & 8)) for(x = 0; x < 8; x++) buf[84 + x] = SPRITES[(SPR_BARON << 3) + x];
    }
    else if(page > 0) {
      // Platforms: two rows of 4x4 tiles per page
      int8_t cy = (page - 1) << 1;
      uint32_t r1 = roundMap[cy], r2 = roundMap[cy + 1];
      for(x = 0; x < 128; x++) {
        uint8_t cx = x >> 2, b = 0;
        if((r1 >> cx) & 1) b  = tile[x & 3];
        if((r2 >> cx) & 1) b |= tile[x & 3] << 4;
        buf[x] = b;
      }

      // Fruit, monsters, bubbles, player, skull (screen y = field y + 8)
      for(i = 0; i < MAX_FRUITS; i++)
        if(fruit[i].type && (fruit[i].timer > 60 || (frame & 4)))
          DrawSprite(page, PX(fruit[i].x), PX(fruit[i].y) + 8, fruit[i].type, 0);
      for(i = 0; i < MAX_ENEMIES; i++) {
        ENEMY *e = &enemy[i];
        if(!e->type) continue;
        uint8_t spr = (e->type == E_WALKER) ? SPR_WALK0 : SPR_FLY0;
        if(e->anim & (e->angry ? 2 : 4)) spr++;
        DrawSprite(page, PX(e->x), PX(e->y) + 8, spr, e->vx < 0);
      }
      for(i = 0; i < MAX_BUBBLES; i++) {
        BUBBLE *b = &bubble[i];
        if(!b->state) continue;
        uint8_t spr = (b->state == B_POP) ? SPR_POP : b->trapped ? ((b->trapped & 3) == E_WALKER ? SPR_TRAP0 : SPR_TRAP1) : SPR_BUBBLE;
        if(b->state == B_FLOAT && b->timer < 30 && (frame & 2)) continue;   // about to burst
        DrawSprite(page, PX(b->x), PX(b->y) + 8, spr, 0);
      }
      if(mode == R_TITLE) {
        DrawSprite(page, 14, 52, SPR_BUB0 + ((frame >> 3) & 1), 0);
        DrawSprite(page, 106, 52, SPR_BUB0 + ((frame >> 3) & 1), 1);
      }
      else if(!deadTimer || (deadTimer & 4)) {
        if(!invuln || !(invuln & 2)) {
          uint8_t spr = deadTimer ? SPR_POP : shootTimer > 6 ? SPR_BUBS : !bground ? SPR_BUBJ : SPR_BUB0 + ((banim >> 2) & 1);
          DrawSprite(page, PX(bx), PX(by) + 8, spr, !bdir);
        }
      }
      if(baronOn) DrawSprite(page, PX(baronX), PX(baronY) + 8, SPR_BARON, 0);
    }

    // Text
    if(mode == R_TITLE) {
      DrawText(page, 40, 4, "TINY", 2);
      DrawText(page, 28, 22, "BUBBLE", 2);
      if(frame & 16) DrawText(page, 34, 42, "PRESS FIRE", 1);
      DrawText(page, 37, 54, "HI", 1);
      DrawNumber(page, 55, 54, hiscore, 6, 1);
    }
    else if(mode == R_GAMEOVER) {
      DrawText(page, 10, 20, "GAME OVER", 2);
      if(frame & 16) {
        DrawText(page, 25, 44, "SCORE", 1);
        DrawNumber(page, 61, 44, score, 6, 1);
      }
    }
    else if(msgTimer && (msgTimer > 15 || (msgTimer & 4))) {
      uint8_t len = 0;
      while(msgText[len]) len++;
      if(msgNum) len += 2 + (msgNum > 9);
      int16_t tx = 64 - len * 3;
      DrawText(page, tx, 30, msgText, 1);
      if(msgNum) {
        int16_t nx = tx + (len - 1 - (msgNum > 9)) * 6;
        DrawText(page, nx - 6, 30, " ", 1);
        DrawNumber(page, nx, 30, msgNum, 1 + (msgNum > 9), 1);
      }
    }

    // Send page to OLED
    JOY_OLED_data_start(page);
    for(x = 0; x < 128; x++) JOY_OLED_send(buf[x]);
    JOY_OLED_end();
  }
}
