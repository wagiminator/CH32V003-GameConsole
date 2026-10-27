// ===================================================================================
// Project:   Tiny Karate - Karate Fighting Game for CH32V003
// Version:   v1.0
// Year:      2026
// Github:    https://github.com/wagiminator
// EasyEDA:   https://easyeda.com/wagiminator
// License:   http://creativecommons.org/licenses/by-sa/3.0/
// ===================================================================================
//
// Description:
// ------------
// A small International Karate-like fighting game for the CH32V003 Mini Game
// Console. Fight against the computer in bouts of 30 seconds. A clean hit scores
// a full point (IPPON), a less perfect one a half point (WAZA-ARI). The first to
// score two full points wins the bout and earns the next belt. The opponents get
// faster and smarter with every belt.
//
// Controls:
// ---------
// Without fire button:          With fire button:
// Right:      walk forward      (none):     punch
// Left:       walk back         Right:      front kick
// Up:         jump              Up:         high kick
// Down:       crouch            Up+Right:   high punch
// Up+Right:   flying kick       Down:       low punch
// Up+Left:    high block        Down+Right: low kick (sweep)
// Down+Left:  low block         Left:       punch
//
// Compilation Settings:
// ---------------------
// - Make sure GCC toolchain (gcc-riscv64-unknown-elf, newlib) and Python3 with rvprog
//   are installed. In addition, Linux requires access rights to WCH-LinkE programmer.
// - Connect the WCH-LinkE programmer to the MCU board.
// - Run 'make flash'.


#include <driver.h>           // TinyJoypad conversion driver
#include <spritebank.h>       // poses, background and fonts

// ===================================================================================
// Game Settings
// ===================================================================================
#define FRAME_TIME    (33 * DLY_MS_TIME)  // ~30 frames per second
#define GROUND_TOP    32      // screen row of the top of a standing fighter
#define ARENA_LEFT    10      // range of the fighters' centers
#define ARENA_RIGHT   118
#define MIN_DIST      11      // fighters can't come closer than this
#define JUMP_SPEED    (-16)   // jump in 1/4 pixels per frame
#define GRAVITY       2
#define BOUT_TIME     30      // seconds per bout
#define EXTRA_TIME    15
#define WIN_POINTS    4       // half points needed to win a bout
#define BELTS         7       // white ... black

// Fighter states
enum {ST_IDLE, ST_CROUCH, ST_BLOCK_H, ST_BLOCK_L, ST_JUMP, ST_FLYKICK, ST_ATTACK, ST_HIT, ST_FALL, ST_BOW, ST_WIN};
// What a fighter wants to do (from the joypad or the computer)
enum {I_NONE, I_FWD, I_BACK, I_CROUCH, I_BLOCK_H, I_BLOCK_L, I_JUMP, I_FLYKICK,
      I_PUNCH_M, I_PUNCH_H, I_PUNCH_L, I_KICK_M, I_KICK_H, I_KICK_L};
enum {Z_HIGH, Z_MID, Z_LOW};                      // attack heights
// Game phases
enum {PH_INTRO, PH_START, PH_FIGHT, PH_POINT, PH_END};

// Attacks: wind-up pose and frames, active pose and frames, recovery frames,
// reach (distance of the fighters' centers), height, crouching
typedef struct {
  uint8_t wind, tWind, act, tAct, tRec, reach, zone, low;
} MOVE;
const MOVE MOVES[6] = {
  {P_STAND,   3, P_PUNCH_M, 5, 4, 17, Z_MID,  0},    // punch
  {P_STAND,   3, P_PUNCH_H, 5, 4, 17, Z_HIGH, 0},    // high punch
  {P_CROUCH,  3, P_PUNCH_L, 5, 5, 17, Z_LOW,  1},    // low punch
  {P_CHAMBER, 5, P_KICK_M,  6, 6, 19, Z_MID,  0},    // front kick
  {P_CHAMBER, 6, P_KICK_H,  6, 7, 18, Z_HIGH, 0},    // high kick
  {P_CROUCH,  4, P_KICK_L,  6, 6, 19, Z_LOW,  1},    // low kick (sweep)
};
#define FLY_REACH     18

typedef struct {
  int16_t x;                  // center
  int16_t y;                  // height above the ground (1/4 pixels, <= 0)
  int8_t  vy, vx;
  uint8_t state, move, timer, phase, hitDone, moved, anim;
  uint8_t intent;
  uint8_t points;             // half points
  int8_t  dir;                // +1: facing right, -1: facing left
} FIGHTER;

const char *const BELT_NAMES[BELTS] = {"WHITE", "YELLOW", "ORANGE", "GREEN", "BLUE", "BROWN", "BLACK"};

// ===================================================================================
// Game Variables
// ===================================================================================
FIGHTER  pl, cpu;             // player (white) and computer (black)
uint8_t  buf[128];            // one display page
uint32_t inkL[26], maskL[26]; // fighter graphics for this frame (player)
uint32_t inkR[26], maskR[26]; // (computer)

uint32_t score, hiscore;
uint8_t  level, gamePhase, phaseTimer, timeLeft, secTimer, frame, gameOver;
uint8_t  aiTimer, aiIntent;
uint8_t  lastFull;            // last point was a full point

const char *msgText;
const char *msgText2;
uint8_t  msgTimer;

// ===================================================================================
// Function Prototypes
// ===================================================================================
void TitleScreen(void);
void GameOverScreen(void);
void Bout(void);
void ResetFighters(void);
void FighterUpdate(FIGHTER *f, FIGHTER *o);
void StartIntent(FIGHTER *f);
void CheckHit(FIGHTER *a, FIGHTER *d, uint8_t reach, uint8_t zone);
uint8_t PlayerIntent(void);
uint8_t ComputerIntent(void);
void ShowMessage(const char *t1, const char *t2, uint8_t time);
void Tiny_Flip(uint8_t title);
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
    score = 0; level = 0; gameOver = 0;
    while(!gameOver) Bout();
    GameOverScreen();
  }
}

// ===================================================================================
// Screens
// ===================================================================================
void TitleScreen(void) {
  ResetFighters();
  pl.x = 14; cpu.x = 114;                         // on both sides of the title
  msgTimer = 0;
  while(JOY_act_pressed());
  for(frame = 0; ; frame++) {
    uint32_t start = STK->CNT;
    pl.state = cpu.state = (frame & 64) ? ST_IDLE : ST_BOW;
    Tiny_Flip(1);
    if(JOY_act_pressed()) break;
    JOY_random();
    WaitFrame(start);
  }
  JOY_sound(150, 30); JOY_sound(200, 30); JOY_sound(250, 50);
}

void GameOverScreen(void) {
  if(score > hiscore) hiscore = score;
  JOY_sound(120, 80); JOY_sound(90, 80); JOY_sound(60, 150);
  ShowMessage("GAME OVER", 0, 255);
  for(frame = 0; ; frame++) {
    uint32_t start = STK->CNT;
    msgTimer = 255;
    Tiny_Flip(0);
    if(frame > 60 && JOY_act_pressed()) break;
    if(frame > 250) frame = 61;
    WaitFrame(start);
  }
}

void WaitFrame(uint32_t start) {
  while((int32_t)(STK->CNT - start) < FRAME_TIME);
}

void ShowMessage(const char *t1, const char *t2, uint8_t time) {
  msgText = t1; msgText2 = t2; msgTimer = time;
}

// ===================================================================================
// Bout
// ===================================================================================
void ResetFighters(void) {
  pl.x = 42; cpu.x = 86;
  pl.dir = 1; cpu.dir = -1;
  pl.y = cpu.y = 0; pl.vx = cpu.vx = 0;
  pl.state = cpu.state = ST_IDLE;
  pl.intent = cpu.intent = I_NONE;
  pl.anim = cpu.anim = 0;
  aiTimer = 20; aiIntent = I_NONE;
}

void Bout(void) {
  pl.points = 0; cpu.points = 0;
  ResetFighters();
  pl.state = cpu.state = ST_BOW;
  timeLeft = BOUT_TIME; secTimer = 0;
  gamePhase = PH_INTRO; phaseTimer = 60;
  ShowMessage(BELT_NAMES[level < BELTS ? level : BELTS - 1], "BELT", 55);

  while(1) {
    uint32_t start = STK->CNT;
    frame++;

    switch(gamePhase) {
      case PH_INTRO:                              // both fighters bow
        if(--phaseTimer == 0) {
          pl.state = cpu.state = ST_IDLE;
          gamePhase = PH_START; phaseTimer = 30;
          ShowMessage("HAJIME!", 0, 30);
          JOY_sound(200, 20); JOY_sound(250, 30);
        }
        break;
      case PH_START:                              // "begin!"
        if(--phaseTimer == 0) gamePhase = PH_FIGHT;
        break;
      case PH_FIGHT:
        pl.intent  = PlayerIntent();
        cpu.intent = ComputerIntent();
        if(++secTimer >= 30) {
          secTimer = 0;
          if(--timeLeft <= 5 && timeLeft) JOY_sound(220, 8);
          if(!timeLeft) {
            if(pl.points == cpu.points) {         // draw: extra time
              timeLeft = EXTRA_TIME;
              ResetFighters();
              gamePhase = PH_START; phaseTimer = 45;
              ShowMessage("EXTRA TIME", 0, 45);
            }
            else {
              gamePhase = PH_END; phaseTimer = 100;
              pl.state  = (pl.points > cpu.points) ? ST_WIN : ST_IDLE;
              cpu.state = (pl.points > cpu.points) ? ST_IDLE : ST_WIN;
              ShowMessage("TIME UP", pl.points > cpu.points ? "YOU WIN" : "YOU LOSE", 100);
            }
            JOY_sound(100, 40); JOY_sound(80, 60);
          }
        }
        break;
      case PH_POINT:                              // a fighter was hit
        if(phaseTimer == 50) {
          ShowMessage(lastFull ? "IPPON!" : "WAZA-ARI", 0, 50);
          JOY_sound(250, 15); JOY_sound(200, 25);
        }
        if(--phaseTimer == 0) {
          if(pl.points >= WIN_POINTS || cpu.points >= WIN_POINTS) {
            uint8_t won = pl.points >= WIN_POINTS;
            ResetFighters();
            pl.state  = won ? ST_WIN : ST_IDLE;
            cpu.state = won ? ST_IDLE : ST_WIN;
            gamePhase = PH_END; phaseTimer = 100;
            ShowMessage(won ? "YOU WIN" : "YOU LOSE", 0, 100);
          }
          else {
            ResetFighters();
            gamePhase = PH_START; phaseTimer = 30;
            ShowMessage("HAJIME!", 0, 30);
            JOY_sound(200, 20); JOY_sound(250, 30);
          }
        }
        break;
      case PH_END:                                // the winner celebrates
        if(phaseTimer == 99) {
          if(pl.state == ST_WIN) {JOY_sound(150, 30); JOY_sound(200, 30); JOY_sound(250, 60);}
          else {JOY_sound(100, 50); JOY_sound(60, 80);}
        }
        if(pl.state == ST_WIN && timeLeft && !(frame & 1)) {   // time bonus
          timeLeft--; score += 10;
        }
        if(--phaseTimer == 0) {
          if(pl.state == ST_WIN) {
            score += 1000;
            if(level < 255) level++;
          }
          else gameOver = 1;
          return;
        }
        break;
    }
    if(gamePhase != PH_FIGHT) pl.intent = cpu.intent = I_NONE;

    FighterUpdate(&pl, &cpu);
    FighterUpdate(&cpu, &pl);

    // keep the fighters apart and inside the arena
    if(pl.x < ARENA_LEFT) pl.x = ARENA_LEFT;
    if(cpu.x > ARENA_RIGHT) cpu.x = ARENA_RIGHT;
    if(cpu.x - pl.x < MIN_DIST) {
      if(pl.moved && !cpu.moved) pl.x = cpu.x - MIN_DIST;
      else if(cpu.moved && !pl.moved) cpu.x = pl.x + MIN_DIST;
      else {pl.x--; cpu.x = pl.x + MIN_DIST;}
      if(cpu.x > ARENA_RIGHT) {cpu.x = ARENA_RIGHT; pl.x = cpu.x - MIN_DIST;}
      if(pl.x < ARENA_LEFT)   {pl.x = ARENA_LEFT; cpu.x = pl.x + MIN_DIST;}
    }

    Tiny_Flip(0);
    if(msgTimer) msgTimer--;
    WaitFrame(start);
  }
}

// ===================================================================================
// Fighters
// ===================================================================================
uint8_t Airborne(FIGHTER *f) {
  return f->y < -4 * 4;
}

uint8_t Crouching(FIGHTER *f) {
  return f->state == ST_CROUCH || f->state == ST_BLOCK_L || (f->state == ST_ATTACK && MOVES[f->move].low);
}

// Start what the fighter wants to do, if he is free to act
void StartIntent(FIGHTER *f) {
  uint8_t i = f->intent;
  f->moved = 0;
  switch(i) {
    case I_FWD: case I_BACK:
      f->state = ST_IDLE;
      f->x += (i == I_FWD) ? f->dir : -f->dir;
      f->moved = 1; f->anim++;
      break;
    case I_CROUCH:  f->state = ST_CROUCH; break;
    case I_BLOCK_H: f->state = ST_BLOCK_H; break;
    case I_BLOCK_L: f->state = ST_BLOCK_L; break;
    case I_JUMP: case I_FLYKICK:
      f->state = (i == I_JUMP) ? ST_JUMP : ST_FLYKICK;
      f->vy = JUMP_SPEED; f->vx = (i == I_JUMP) ? 0 : f->dir;
      f->hitDone = 0; f->timer = 0;
      JOY_sound(180, 4);
      break;
    case I_NONE:
      f->state = ST_IDLE;
      break;
    default:                                      // attacks
      f->state = ST_ATTACK; f->move = i - I_PUNCH_M;
      f->phase = 0; f->timer = MOVES[f->move].tWind; f->hitDone = 0;
      break;
  }
}

void FighterUpdate(FIGHTER *f, FIGHTER *o) {
  f->moved = 0;
  switch(f->state) {
    case ST_IDLE: case ST_CROUCH: case ST_BLOCK_H: case ST_BLOCK_L:
      StartIntent(f);
      break;

    case ST_JUMP: case ST_FLYKICK:
      f->y += f->vy; f->vy += GRAVITY;
      if(f->vx) {f->x += f->vx; f->moved = 1;}
      f->timer++;
      if(f->state == ST_FLYKICK && f->timer >= 4 && !f->hitDone) CheckHit(f, o, FLY_REACH, Z_HIGH);
      if(f->y >= 0) {f->y = 0; f->state = ST_IDLE;}
      break;

    case ST_ATTACK: {
      const MOVE *m = &MOVES[f->move];
      if(--f->timer == 0) {
        if(f->phase == 0) {                       // wind-up done: strike
          f->phase = 1; f->timer = m->tAct;
          JOY_sound(240, 2);
          CheckHit(f, o, m->reach, m->zone);
        }
        else if(f->phase == 1) {f->phase = 2; f->timer = m->tRec;}
        else f->state = m->low ? ST_CROUCH : ST_IDLE;
      }
      break;
    }

    case ST_HIT:
      if(f->y < 0) {f->y += 4; if(f->y > 0) f->y = 0;}
      if(--f->timer == 0) f->state = ST_FALL;
      break;
  }
}

// Does the attack of fighter a hit fighter d?
void CheckHit(FIGHTER *a, FIGHTER *d, uint8_t reach, uint8_t zone) {
  int16_t dist = d->x - a->x;
  if(dist < 0) dist = -dist;
  if(dist > reach || d->state >= ST_HIT) return;
  // jumping over low and middle attacks, ducking under high ones
  if(Airborne(d) && zone != Z_HIGH) return;
  if(Crouching(d) && zone == Z_HIGH) return;
  a->hitDone = 1;
  if((d->state == ST_BLOCK_H && zone != Z_LOW) || (d->state == ST_BLOCK_L && zone == Z_LOW)) {
    JOY_sound(200, 6); JOY_sound(160, 6);         // blocked
    a->x -= a->dir * 3;
    return;
  }
  // hit: a well-aimed strike with the full reach (or a counter) scores a full point
  uint8_t full = (dist >= reach - 4) || (d->state == ST_ATTACK && d->phase == 0) || a->state == ST_FLYKICK;
  lastFull = full;
  a->points += full ? 2 : 1;
  if(a == &pl) score += full ? 400 : 200;
  d->state = ST_HIT; d->timer = 10;
  if(a->state == ST_ATTACK) a->timer = 20;        // hold the pose
  JOY_sound(40, 20); JOY_sound(20, 30);
  gamePhase = PH_POINT; phaseTimer = 70;
}

// ===================================================================================
// Control
// ===================================================================================
uint8_t PlayerIntent(void) {
  uint8_t u = JOY_up_pressed(), d = JOY_down_pressed();
  uint8_t l = JOY_left_pressed(), r = JOY_right_pressed();
  if(JOY_act_pressed()) {
    if(u) return r ? I_PUNCH_H : I_KICK_H;
    if(d) return r ? I_KICK_L : I_PUNCH_L;
    if(r) return I_KICK_M;
    return I_PUNCH_M;
  }
  if(u) return r ? I_FLYKICK : l ? I_BLOCK_H : I_JUMP;
  if(d) return l ? I_BLOCK_L : I_CROUCH;
  if(r) return I_FWD;
  if(l) return I_BACK;
  return I_NONE;
}

// The computer opponent: reacts to the player's attacks and attacks when he is
// in range; faster and smarter with every belt
uint8_t ComputerIntent(void) {
  static uint8_t seen;
  uint8_t lv = level > 8 ? 8 : level;
  int16_t dist = cpu.x - pl.x;
  uint8_t r;

  // an attack of the player is noticed at once (the first frame of its wind-up);
  // better opponents block, duck or jump more often
  uint8_t threat = (pl.state == ST_ATTACK && pl.phase == 0) || (pl.state == ST_FLYKICK && !pl.hitDone);
  if(!threat) seen = 0;
  else if(!seen) {
    seen = 1;
    r = JOY_random() % 100;
    if(dist <= 22 && r < 15 + lv * 10 && cpu.state <= ST_BLOCK_L) {
      uint8_t z = (pl.state == ST_FLYKICK) ? Z_HIGH : MOVES[pl.move].zone;
      if(z == Z_LOW) aiIntent = (r & 1) ? I_BLOCK_L : I_JUMP;
      else if(z == Z_HIGH && (r & 1)) aiIntent = I_CROUCH;
      else aiIntent = I_BLOCK_H;
      aiTimer = 10;
      return aiIntent;
    }
  }
  if(aiIntent == I_FWD && dist <= 20 && lv >= 2) aiTimer = 0;   // walked into range: think again
  if(aiTimer) {aiTimer--; return aiIntent;}
  aiTimer = 12 - lv;
  aiIntent = I_NONE;

  r = JOY_random() % 100;
  if(dist <= 19 && r < 30 + lv * 8) {
    // pick an attack that reaches; better opponents pick one that scores a full point
    uint8_t k = JOY_random() % 6;
    for(uint8_t i = 0; i < 6; i++, k = (k + 1) % 6) {
      const MOVE *m = &MOVES[k];
      if(dist > m->reach) continue;
      if(lv >= 2 && dist < m->reach - 4 && i < 5) continue;
      if(Crouching(&pl) && m->zone == Z_HIGH && lv >= 1) continue;
      if(Airborne(&pl) && m->zone != Z_HIGH) continue;
      aiIntent = I_PUNCH_M + k;
      break;
    }
    aiTimer = 2;
    if(aiIntent != I_NONE) return aiIntent;
  }
  r = JOY_random() % 100;
  if(dist > 21)       aiIntent = (r < 85) ? I_FWD : (r < 92 && lv >= 3) ? I_FLYKICK : I_NONE;
  else if(dist < 14)  aiIntent = (r < 60) ? I_BACK : I_NONE;
  else                aiIntent = (r < 25) ? I_BACK : (r < 45) ? I_FWD : (r < 55) ? I_CROUCH : I_NONE;
  aiTimer = 4 + (JOY_random() & 7);
  return aiIntent;
}

// ===================================================================================
// Rendering
// ===================================================================================

// Current pose of a fighter
uint8_t FighterPose(FIGHTER *f) {
  static const uint8_t WALK[4] = {P_WALK1, P_STAND, P_WALK2, P_STAND};
  switch(f->state) {
    case ST_IDLE:    return f->moved ? WALK[(f->anim >> 2) & 3] : P_STAND;
    case ST_CROUCH:  return P_CROUCH;
    case ST_BLOCK_H: return P_BLOCK_H;
    case ST_BLOCK_L: return P_BLOCK_L;
    case ST_JUMP:    return P_JUMP;
    case ST_FLYKICK: return f->timer < 4 ? P_JUMP : P_FLYKICK;
    case ST_ATTACK:  return f->phase == 1 ? MOVES[f->move].act : MOVES[f->move].wind;
    case ST_HIT:     return P_HIT;
    case ST_FALL:    return P_FALL;
    case ST_BOW:     return P_BOW;
    default:         return P_WIN;
  }
}

// Prepare the 26 columns (pose plus outline) of a fighter: the player is drawn
// filled, the computer as an outline
void PrepareFighter(FIGHTER *f, uint32_t *ink, uint32_t *mask, uint8_t hollow) {
  const uint32_t *p = &POSES[FighterPose(f) * 24];
  uint32_t prev = 0, cur = 0;
  for(uint8_t k = 0; k < 26; k++) {               // k: art column k - 1
    uint32_t next = (k < 24) ? p[f->dir > 0 ? k : 23 - k] : 0;
    uint32_t m = prev | cur | next;
    mask[k] = m | (m << 1) | (m >> 1);
    ink[k] = hollow ? cur & ~(cur & (cur << 1) & (cur >> 1) & prev & next) : cur;
    prev = cur; cur = next;
  }
}

// Draw a prepared fighter into the current page
void DrawFighter(uint8_t page, FIGHTER *f, uint32_t *ink, uint32_t *mask) {
  int16_t top = GROUND_TOP - 1 + (f->y >> 2);    // screen row of bit 0
  int16_t sh = (page << 3) - top;
  if(sh <= -8 || sh >= 32) return;
  int16_t x0 = f->x - 10;                         // screen column of k = 0
  if(f->dir < 0) x0 = f->x - 15;
  for(uint8_t k = 0; k < 26; k++) {
    int16_t cx = x0 + k;
    if(cx < 0 || cx > 127) continue;
    uint8_t mb, ib;
    if(sh >= 0) {mb = mask[k] >> sh; ib = ink[k] >> sh;}
    else        {mb = mask[k] << -sh; ib = ink[k] << -sh;}
    buf[cx] = (buf[cx] & ~mb) | ib;
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

// Centered text
void DrawTextC(uint8_t page, int8_t y, const char *s, uint8_t scale) {
  uint8_t len = 0;
  while(s[len]) len++;
  DrawText(page, 64 - len * 3 * scale + scale, y, s, scale);
}

// Number with a fixed count of digits: 3x5 digits (big = 0) or the 5x7 font (big = 1)
void DrawNumber(uint8_t page, int16_t x, int8_t y, uint32_t n, uint8_t digits, uint8_t big) {
  char s[8];
  s[digits] = 0;
  while(digits--) {s[digits] = '0' + n % 10; n /= 10;}
  if(big) {DrawText(page, x, y, s, 1); return;}
  for(char *p = s; *p; p++, x += 4)
    for(uint8_t i = 0; i < 3; i++) buf[x + i] |= DIGITS[(*p - '0') * 3 + i] << 1;
}

// Two point symbols: empty, half or full
void DrawPoints(uint8_t x, uint8_t points) {
  for(uint8_t i = 0; i < 2; i++, x += 8) {
    const uint8_t *ic = (points >= 2) ? ICON_FULL : points ? ICON_HALF : ICON_EMPTY;
    points = points >= 2 ? points - 2 : 0;
    for(uint8_t k = 0; k < 5; k++) buf[x + k] |= ic[k];
  }
}

// Draw the whole screen page by page and send it to the OLED
void Tiny_Flip(uint8_t title) {
  // the attacking fighter is drawn last (in front)
  uint8_t cpuFront = (cpu.state == ST_ATTACK || cpu.state == ST_FLYKICK) && pl.state != ST_ATTACK;
  PrepareFighter(&pl, inkL, maskL, 0);
  PrepareFighter(&cpu, inkR, maskR, 1);
  for(uint8_t page = 0; page < 8; page++) {
    if(page == 0) {
      for(uint8_t x = 0; x < 128; x++) buf[x] = 0;
      if(title) {
        DrawText(0, 37, 0, "HI", 1);
        DrawNumber(0, 55, 0, hiscore, 6, 1);
      }
      else {
        DrawPoints(0, pl.points);
        DrawNumber(0, 19, 0, score, 6, 0);
        DrawNumber(0, 58, 0, timeLeft, 2, 1);
        DrawPoints(112, cpu.points);
      }
    }
    else {
      const uint8_t *bg = &BACKGROUND[(page - 1) << 7];
      for(uint8_t x = 0; x < 128; x++) buf[x] = bg[x];
      if(cpuFront) {
        DrawFighter(page, &pl, inkL, maskL);
        DrawFighter(page, &cpu, inkR, maskR);
      }
      else {
        DrawFighter(page, &cpu, inkR, maskR);
        DrawFighter(page, &pl, inkL, maskL);
      }
    }

    if(title) {
      DrawTextC(page, 10, "TINY", 2);
      DrawTextC(page, 27, "KARATE", 2);
      if(frame & 16) DrawTextC(page, 46, "PRESS FIRE", 1);
    }
    else if(msgTimer && (msgTimer > 10 || (msgTimer & 2))) {
      if(msgText2) {
        DrawTextC(page, 13, msgText, 1);
        DrawTextC(page, 22, msgText2, 1);
      }
      else DrawTextC(page, 16, msgText, msgText[0] == 'G' || msgText[0] == 'I' ? 2 : 1);
    }

    // Send page to OLED
    JOY_OLED_data_start(page);
    for(uint8_t x = 0; x < 128; x++) JOY_OLED_send(buf[x]);
    JOY_OLED_end();
  }
}
