// ===================================================================================
// Project:   Tiny Doom - First Person Shooter for CH32V003
// Version:   v1.0
// Year:      2026
// Github:    https://github.com/wagiminator
// EasyEDA:   https://easyeda.com/wagiminator
// License:   http://creativecommons.org/licenses/by-sa/3.0/
// ===================================================================================
//
// Description:
// ------------
// A small Doom-like first person shooter for the CH32V003 Mini Game Console. Fight
// your way through three levels full of zombies, demons and a cacodemon, open the
// doors, pick up medkits and ammo and find the exit switch of each level.
//
// The 3D view is drawn with a ray caster: 64 rays (each two pixels wide) are cast
// through the 16x16 cell map with integer DDA steps; walls are shaded with dither
// patterns depending on wall type, side and distance. Monsters and items are
// scaled sprites with masks, hidden behind walls with a depth buffer. All math
// uses fixed point integers and tables, since the RV32EC core has no multiplier.
// The screen is drawn page by page (8 pixel rows) into a 128 byte buffer and sent
// to the OLED.
//
// Controls:
// ---------
// Up/Down:     walk forward/backward
// Left/Right:  turn
// Fire button: shoot, start game
// Walk into a door to open it, walk into the exit switch to finish the level.
//
// Compilation Settings:
// ---------------------
// - Make sure GCC toolchain (gcc-riscv64-unknown-elf, newlib) and Python3 with rvprog
//   are installed. In addition, Linux requires access rights to WCH-LinkE programmer.
// - Connect the WCH-LinkE programmer to the MCU board.
// - Run 'make flash'.


#include <driver.h>           // TinyJoypad conversion driver
#include <spritebank.h>       // tables, grafix and levels

// ===================================================================================
// Game Settings
// ===================================================================================
#define FRAME_TIME    (40 * DLY_MS_TIME)  // 25 frames per second
#define VIEW_PAGES    7       // 3D view: pages 0..6 (56 rows), status bar: page 7
#define HORIZON       28      // middle row of the view
#define FOCAL         96      // projection distance in pixels (field of view ~67 deg)
#define NRAYS         64      // rays per frame, two pixel columns each
#define MAX_OBJ       16      // monsters and items per level
#define MOVE_SPEED    26      // walking speed in 1/256 cells per frame
#define TURN_SPEED    20      // turning speed in 1/1024 turns per frame
#define PLAYER_R      72      // player radius in 1/256 cells

// Map cells
enum {C_FLOOR, C_STONE, C_BRICK, C_TECH, C_DOOR, C_EXIT};

// Object types and states
enum {O_NONE, O_ZOMBIE, O_DEMON, O_CACO, O_MEDKIT, O_AMMO};
enum {S_IDLE, S_CHASE, S_ATTACK, S_PAIN, S_DEAD, S_TAKEN};

// Level results
enum {L_DIED, L_DONE};

// ===================================================================================
// Game State
// ===================================================================================
typedef struct {
  int16_t x, y;               // position in 1/256 cells
  uint8_t type, state, hp;
  uint8_t timer;              // attack cooldown / state time
  uint8_t anim;               // walking animation
  uint8_t seen;               // has line of sight to the player
} OBJECT;

typedef struct {              // object on screen, for drawing
  int16_t  sx;                // screen column of the center
  uint16_t depth;             // distance in 1/256 cells
  uint8_t  size;              // height and width in pixels
  int16_t  top;               // first row
  uint8_t  spr;               // sprite number
  uint8_t  idx;               // object number
  uint16_t step;              // sprite texels per pixel (8.8)
} VISIBLE;

uint8_t  level_map[256];            // current level, one byte per cell
OBJECT   obj[MAX_OBJ];
VISIBLE  vis[MAX_OBJ];
uint8_t  order[MAX_OBJ];      // visible objects sorted far to near
uint8_t  nvis;

uint16_t zbuf[NRAYS];         // wall distance per ray (1/256 cells)
uint8_t  wallH[NRAYS];        // wall height per ray in pixels
uint8_t  wallInfo[NRAYS];     // cell type << 4 | side << 3 | texture column
uint8_t  buf[128];            // one display page

int16_t  px, py;              // player position (1/256 cells)
uint16_t pa;                  // view angle (1/1024 turns)
int16_t  health;
uint8_t  ammo, level, kills, monsters;
uint8_t  gunTimer, hurtTimer, walkAnim, frame, inverted, exitReached;

// ===================================================================================
// Function Prototypes
// ===================================================================================
void    TitleScreen(void);
uint8_t PlayLevel(void);
void    MessageScreen(const char *line1, const char *line2, int16_t num1, int16_t num2);
void    DrawText(uint8_t page, int16_t x, int8_t y, const char *s, uint8_t scale);
void    DrawNumber(uint8_t page, int16_t x, int8_t y, uint8_t n, uint8_t big);
void    LoadLevel(uint8_t n);
void    MovePlayer(int16_t speed);
void    Shoot(void);
void    UpdateObjects(void);
void    HurtPlayer(uint8_t damage);
uint8_t LineOfSight(int16_t x0, int16_t y0);
void    CastRays(void);
void    PrepareSprites(void);
void    Tiny_Flip(uint8_t hud, const char *text, uint8_t big);
void    WaitFrame(uint32_t start);

// ===================================================================================
// Fixed Point Helpers
// ===================================================================================
int16_t Sin(uint16_t a) {
  a &= 1023;
  if(a < 256) return  SINQ[a];
  if(a < 512) return  SINQ[512 - a];
  if(a < 768) return -SINQ[a - 512];
  return -SINQ[1024 - a];
}
#define Cos(a) Sin((a) + 256)

// 256 / |sin(a)| (length of a ray step between two grid lines)
uint16_t InvAbsSin(uint16_t a) {
  a &= 511;
  if(a > 256) a = 512 - a;
  return INVQ[a];
}
#define InvAbsCos(a) InvAbsSin((a) + 256)

static inline int16_t Abs(int16_t v) { return v < 0 ? -v : v; }

// Approximate length of a vector (octagonal distance)
uint16_t Dist(int16_t dx, int16_t dy) {
  uint16_t ax = Abs(dx), ay = Abs(dy);
  return ax > ay ? ax + (ay >> 1) - (ay >> 3) : ay + (ax >> 1) - (ax >> 3);
}

uint8_t Cell(int8_t x, int8_t y) {
  if((uint8_t)x > 15 || (uint8_t)y > 15) return C_STONE;
  return level_map[(y << 4) | x];
}

#define CellAt(x, y)  Cell((x) >> 8, (y) >> 8)

// ===================================================================================
// Main Function
// ===================================================================================
int main(void) {
  // Setup
  JOY_init();

  // Loop
  while(1) {
    TitleScreen();
    level = 0; health = 100; ammo = 50;
    while(level < LEVELS) {
      LoadLevel(level);
      MessageScreen("LEVEL", LEVEL_NAME[level], level + 1, -1);
      if(PlayLevel() == L_DIED) {
        JOY_sound(60, 100); JOY_sound(40, 100); JOY_sound(20, 150);
        MessageScreen("YOU DIED", "TRY AGAIN", -1, -1);
        health = 100;
        if(ammo < 30) ammo = 30;
      }
      else {
        JOY_sound(150, 60); JOY_sound(200, 60); JOY_sound(250, 80);
        MessageScreen("KILLS", "LEVEL COMPLETE", kills, monsters);
        level++;
      }
    }
    MessageScreen("YOU WIN!", "CONGRATULATIONS", -1, -1);
  }
}

// ===================================================================================
// Screens
// ===================================================================================

// Title: slowly turning view of the first level
void TitleScreen(void) {
  LoadLevel(0);
  px = 8 * 256; py = 3 * 256 + 128;
  frame = 0;
  while(JOY_act_pressed());
  while(1) {
    uint32_t start = STK->CNT;
    pa = (pa + 3) & 1023;
    frame++;
    CastRays();
    PrepareSprites();
    Tiny_Flip(0, (frame & 16) ? "PRESS FIRE" : 0, 1);
    if(JOY_act_pressed()) break;
    JOY_random();
    WaitFrame(start);
  }
  JOY_sound(100, 60); JOY_sound(150, 60); JOY_sound(200, 100);
}

// Text on a black screen until the fire button is pressed (and at least 1 s)
void MessageScreen(const char *line1, const char *line2, int16_t num1, int16_t num2) {
  uint8_t l1 = 0, l2 = 0, n1 = 0;
  while(line1[l1]) l1++;
  while(line2[l2]) l2++;
  n1 = l1;
  if(num1 >= 0) l1 += 2 + (num1 > 9);
  if(num2 >= 0) l1 += 2 + (num2 > 9);
  if(inverted) {JOY_OLED_send_command(OLED_INVERT_OFF); inverted = 0;}
  for(uint8_t t = 0; ; t++) {
    uint32_t start = STK->CNT;
    for(uint8_t page = 0; page < 8; page++) {
      for(uint8_t i = 0; i < 128; i++) buf[i] = 0;
      int16_t x = 64 - l1 * 3;
      DrawText(page, x, 14, line1, 1);
      if(num1 >= 0) {
        x += (n1 + 1) * 6;
        DrawNumber(page, x, 14, num1, 1);
        if(num2 >= 0) {
          x += (num1 > 9) ? 12 : 6;
          DrawText(page, x, 14, "/", 1);
          DrawNumber(page, x + 6, 14, num2, 1);
        }
      }
      DrawText(page, 64 - l2 * 3, 30, line2, 1);
      if(t > 25 && (t & 16)) DrawText(page, 34, 50, "PRESS FIRE", 1);
      JOY_OLED_data_start(page);
      for(uint8_t i = 0; i < 128; i++) JOY_OLED_send(buf[i]);
      JOY_OLED_end();
    }
    if(t > 25 && JOY_act_pressed()) break;
    if(t > 200) t = 26;
    WaitFrame(start);
  }
  while(JOY_act_pressed());
}

void WaitFrame(uint32_t start) {
  while((int32_t)(STK->CNT - start) < FRAME_TIME);
}

// ===================================================================================
// Level
// ===================================================================================
void LoadLevel(uint8_t n) {
  const uint8_t *m = LEVEL_MAP[n], *o = LEVEL_OBJ[n];
  uint8_t i;
  for(i = 0; i < 128; i++) {
    level_map[i << 1]       = m[i] & 15;
    level_map[(i << 1) + 1] = m[i] >> 4;
  }
  monsters = kills = 0;
  for(i = 0; i < MAX_OBJ; i++) {
    OBJECT *b = &obj[i];
    b->type = O_NONE;
    if(*o) {
      static const uint8_t HP[] = {0, 2, 3, 12, 0, 0};
      b->type  = *o++;
      b->x     = (*o++ << 8) + 128;
      b->y     = (*o++ << 8) + 128;
      b->state = S_IDLE; b->hp = HP[b->type];
      b->timer = 30 + (JOY_random() & 63); b->anim = 0; b->seen = 0;
      if(b->type <= O_CACO) monsters++;
    }
  }
  px = (LEVEL_START[n << 1] << 8) + 128;
  py = (LEVEL_START[(n << 1) + 1] << 8) + 128;
  pa = 0;
  gunTimer = hurtTimer = exitReached = 0;
}

// One level; returns L_DIED or L_DONE
uint8_t PlayLevel(void) {
  frame = 0;
  while(1) {
    uint32_t start = STK->CNT;
    frame++;

    // Turn and walk
    if(JOY_left_pressed())  pa = (pa - TURN_SPEED) & 1023;
    if(JOY_right_pressed()) pa = (pa + TURN_SPEED) & 1023;
    if(JOY_up_pressed())   {MovePlayer(MOVE_SPEED);      walkAnim++;}
    if(JOY_down_pressed()) {MovePlayer(-MOVE_SPEED / 2); walkAnim++;}

    // Exit switch reached?
    if(exitReached) return L_DONE;

    // Shoot
    if(gunTimer) gunTimer--;
    if(JOY_act_pressed() && !gunTimer) {
      gunTimer = 9;
      if(ammo) {ammo--; Shoot();}
      else JOY_sound(250, 4);                    // click
    }

    UpdateObjects();
    if(health <= 0) return L_DIED;

    // Red screen: inverted display after a hit
    if(hurtTimer) hurtTimer--;
    if((hurtTimer > 4) != inverted) {
      inverted = !inverted;
      JOY_OLED_send_command(inverted ? OLED_INVERT : OLED_INVERT_OFF);
    }

    CastRays();
    PrepareSprites();
    Tiny_Flip(1, 0, 0);
    WaitFrame(start);
  }
}

// ===================================================================================
// Player
// ===================================================================================

// Is a position blocked? Opens doors the player walks into.
uint8_t Blocked(int16_t x, int16_t y, uint8_t isPlayer) {
  int16_t r = isPlayer ? PLAYER_R : 80;
  for(uint8_t k = 0; k < 4; k++) {
    int16_t cx = x + ((k & 1) ? r : -r), cy = y + ((k & 2) ? r : -r);
    uint8_t c = CellAt(cx, cy);
    if(c) {
      if(c == C_EXIT && isPlayer) exitReached = 1;
      if(c == C_DOOR && isPlayer) {
        level_map[((cy >> 8) << 4) | (cx >> 8)] = C_FLOOR;
        JOY_sound(40, 30); JOY_sound(60, 30);
      }
      return 1;
    }
  }
  // Monsters are solid
  for(uint8_t i = 0; i < MAX_OBJ; i++) {
    OBJECT *b = &obj[i];
    if(b->type == O_NONE || b->type > O_CACO || b->state == S_DEAD) continue;
    if(isPlayer ? (Abs(b->x - x) < 150 && Abs(b->y - y) < 150) : 0) return 1;
  }
  return 0;
}

// Walk along the view direction, sliding along walls
void MovePlayer(int16_t speed) {
  int16_t dx = ((int32_t)Cos(pa) * speed) >> 14;
  int16_t dy = ((int32_t)Sin(pa) * speed) >> 14;
  if(!Blocked(px + dx, py, 1)) px += dx;
  if(!Blocked(px, py + dy, 1)) py += dy;
}

// Hitscan: the nearest monster in the middle of the screen is hit
void Shoot(void) {
  OBJECT *target = 0;
  uint16_t best = 0xFFFF;
  JOY_sound(60, 14); JOY_sound(30, 10);
  for(uint8_t i = 0; i < nvis; i++) {
    VISIBLE *v = &vis[i];
    OBJECT *b = &obj[v->idx];
    if(b->type > O_CACO || b->state == S_DEAD) continue;  // only living monsters
    int16_t half = (v->size >> 2) + 3;
    if(Abs(v->sx - 64) > half) continue;
    if(v->depth > zbuf[NRAYS / 2] || v->depth >= best) continue;
    best = v->depth;
    target = b;
  }
  if(!target) return;
  if(target->hp) target->hp--;
  if(target->hp == 0) {
    target->state = S_DEAD;
    kills++;
    JOY_sound(80, 40); JOY_sound(50, 60);
  }
  else {
    target->state = S_PAIN; target->timer = 8; target->seen = 1;
    JOY_sound(200, 10);
  }
}

void HurtPlayer(uint8_t damage) {
  health -= damage;
  hurtTimer = 8;
  JOY_sound(30, 30);
}

// ===================================================================================
// Monsters and Items
// ===================================================================================

// Is the player visible from (x0, y0)? Steps of a quarter cell through the map.
uint8_t LineOfSight(int16_t x0, int16_t y0) {
  int16_t dx = px - x0, dy = py - y0;
  int16_t n = (Dist(dx, dy) >> 6) + 1;
  int16_t sx = dx / n, sy = dy / n;
  while(--n > 0) {
    x0 += sx; y0 += sy;
    if(CellAt(x0, y0)) return 0;
  }
  return 1;
}

void UpdateObjects(void) {
  for(uint8_t i = 0; i < MAX_OBJ; i++) {
    OBJECT *b = &obj[i];
    if(b->type == O_NONE || b->state >= S_DEAD) continue;
    int16_t dx = px - b->x, dy = py - b->y;
    uint16_t d = Dist(dx, dy);

    // Items
    if(b->type >= O_MEDKIT) {
      if(d < 160) {
        if(b->type == O_MEDKIT && health < 100) {
          health += 25; if(health > 100) health = 100;
          b->state = S_TAKEN;
        }
        if(b->type == O_AMMO && ammo < 99) {
          ammo += 15; if(ammo > 99) ammo = 99;
          b->state = S_TAKEN;
        }
        if(b->state == S_TAKEN) {JOY_sound(200, 20); JOY_sound(240, 30);}
      }
      continue;
    }

    // Monsters: check the view now and then
    if(((frame + i) & 7) == 0) b->seen = (d < 12 * 256) && LineOfSight(b->x, b->y);
    if(b->timer) b->timer--;

    switch(b->state) {
      case S_IDLE:
        if(b->seen) {b->state = S_CHASE; JOY_sound(120, 15);}
        break;

      case S_PAIN:
        if(!b->timer) {b->state = S_CHASE; b->timer = 10;}
        break;

      case S_ATTACK:
        if(!b->timer) {b->state = S_CHASE; b->timer = (b->type == O_DEMON) ? 20 : 40 + (JOY_random() & 63);}
        break;

      case S_CHASE: {
        // Walk towards the player (demons come close, the others keep distance)
        uint16_t keep = (b->type == O_DEMON) ? 200 : 2 * 256;
        int16_t speed = (b->type == O_DEMON) ? 14 : (b->type == O_CACO ? 6 : 8);
        if(d > keep && (b->seen || b->type == O_DEMON)) {
          int16_t mx = dx > 32 ? speed : dx < -32 ? -speed : 0;
          int16_t my = dy > 32 ? speed : dy < -32 ? -speed : 0;
          if(!Blocked(b->x + mx, b->y, 0) && Abs(b->x + mx - px) + Abs(b->y - py) > 160) b->x += mx;
          if(!Blocked(b->x, b->y + my, 0) && Abs(b->x - px) + Abs(b->y + my - py) > 160) b->y += my;
          b->anim++;
        }
        // Attack
        if(!b->timer && b->seen) {
          if(b->type == O_DEMON) {
            if(d < 300) {
              b->state = S_ATTACK; b->timer = 8;
              HurtPlayer(6 + (JOY_random() & 7));
            }
          }
          else if(d < 9 * 256) {
            b->state = S_ATTACK; b->timer = 8;
            JOY_sound(110, 12);
            // chance to hit drops with the distance
            if((JOY_random() & 255) > (d >> 4) + 60) HurtPlayer(b->type == O_CACO ? 10 + (JOY_random() & 7) : 3 + (JOY_random() & 7));
          }
        }
      } break;
    }
  }
}

// ===================================================================================
// Ray Casting
// ===================================================================================
void CastRays(void) {
  for(uint8_t c = 0; c < NRAYS; c++) {
    uint16_t ra = (pa + c * 3 - 94) & 1023;
    int16_t dx = Cos(ra), dy = Sin(ra);
    int8_t mx = px >> 8, my = py >> 8;
    int8_t sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    uint32_t ddx = InvAbsCos(ra), ddy = InvAbsSin(ra);
    uint32_t sdx = (ddx * ((dx < 0) ? (px & 255) : 256 - (px & 255))) >> 8;
    uint32_t sdy = (ddy * ((dy < 0) ? (py & 255) : 256 - (py & 255))) >> 8;
    uint32_t dist = 0;
    uint8_t side = 0, cell = C_STONE;

    // DDA: step from grid line to grid line until a wall is hit
    for(uint8_t n = 0; n < 32; n++) {
      if(sdx < sdy) {dist = sdx; sdx += ddx; mx += sx; side = 0;}
      else          {dist = sdy; sdy += ddy; my += sy; side = 1;}
      cell = Cell(mx, my);
      if(cell) break;
    }

    // Distance perpendicular to the view (no fisheye) and wall height
    uint32_t perp = (dist * COSOFF[c]) >> 14;
    if(perp < 40) perp = 40;
    zbuf[c] = perp > 0xFFFF ? 0xFFFF : perp;
    wallH[c] = (perp < 1024) ? HNEAR[perp >> 2] : (perp < 8192) ? HFAR[perp >> 5] : 2;

    // Where the wall was hit: texture column 0..7
    int32_t hit = side ? px + (((int32_t)dist * dx) >> 14) : py + (((int32_t)dist * dy) >> 14);
    wallInfo[c] = (cell << 4) | (side << 3) | ((hit >> 5) & 7);
  }
}

// Monsters and items on the screen, sorted far to near
void PrepareSprites(void) {
  int16_t ca = Cos(pa), sa = Sin(pa);
  nvis = 0;
  for(uint8_t i = 0; i < MAX_OBJ; i++) {
    OBJECT *b = &obj[i];
    if(b->type == O_NONE || b->state == S_TAKEN) continue;
    int16_t dx = b->x - px, dy = b->y - py;
    int32_t depth = ((int32_t)dx * ca + (int32_t)dy * sa) >> 14;
    if(depth < 48) continue;
    int32_t side = ((int32_t)dy * ca - (int32_t)dx * sa) >> 14;
    if(side > depth || -side > depth) continue;   // outside the field of view
    int16_t sx = 64 + side * FOCAL / depth;
    int32_t size = (FOCAL * 205) / depth;         // 0.8 cells high
    if(size > 120) size = 120;
    if(size < 2) continue;
    if(sx + (size >> 1) < 0 || sx - (size >> 1) > 127) continue;

    VISIBLE *pv = &vis[nvis];
    #define v (*pv)
    v.sx = sx; v.size = size; v.depth = depth; v.idx = i;
    v.top = HORIZON + (FOCAL * 128) / depth - size;   // stands on the floor
    v.step = (16 << 8) / size;
    switch(b->type) {
      case O_ZOMBIE: v.spr = (b->state == S_DEAD) ? SPR_CORPSE : (b->state == S_ATTACK) ? SPR_ZOMBIEF : (b->anim & 8) ? SPR_ZOMBIE2 : SPR_ZOMBIE1; break;
      case O_DEMON:  v.spr = (b->state == S_DEAD) ? SPR_CORPSE : (b->state == S_ATTACK) ? SPR_DEMONF  : (b->anim & 4) ? SPR_DEMON2 : SPR_DEMON1; break;
      case O_CACO:   v.spr = (b->state == S_DEAD) ? SPR_CORPSE : (b->state == S_ATTACK) ? SPR_BOSSF : SPR_BOSS1; break;
      case O_MEDKIT: v.spr = SPR_MEDKIT; break;
      default:       v.spr = SPR_AMMO; break;
    }
    #undef v
    if(b->state == S_PAIN && (frame & 2)) continue;   // blink when hit

    // insert sorted by depth (far first)
    uint8_t k = nvis;
    while(k && vis[order[k - 1]].depth < pv->depth) {order[k] = order[k - 1]; k--;}
    order[k] = nvis++;
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

// 5x7 font, scale 1 or 2, with a black box behind the letters
void DrawText(uint8_t page, int16_t x, int8_t y, const char *s, uint8_t scale) {
  int8_t r0 = page << 3;
  if(y - 1 >= r0 + 8 || y + 7 * scale < r0) return;   // not in this page
  // rows of the black box (one pixel margin) in this page
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

// Number with the 5x7 font (big = 1) or 3x5 digits
void DrawNumber(uint8_t page, int16_t x, int8_t y, uint8_t n, uint8_t big) {
  char s[4];
  uint8_t len = n > 99 ? 3 : n > 9 ? 2 : 1;
  s[len] = 0;
  for(int8_t i = len - 1; i >= 0; i--) {s[i] = '0' + n % 10; n /= 10;}
  if(big) {DrawText(page, x, y, s, 1); return;}
  for(char *p = s; *p; p++, x += 4)
    for(uint8_t i = 0; i < 3; i++) DrawColumn(page, x + i, y, DIGITS[(*p - '0') * 3 + i]);
}

// Wall pattern for one pixel column
uint8_t WallPattern(uint8_t x, uint8_t info, uint8_t h) {
  uint8_t type = info >> 4, side = info & 8, tex = info & 7;
  uint8_t odd = x & 1, pat;
  switch(type) {
    case C_STONE:                                 // rough stone
      pat = side ? (odd ? 0x44 : 0x11) : (odd ? 0xAA : 0x55);
      if(tex == 0) pat = side ? 0x55 : 0xFF;
      break;
    case C_BRICK:                                 // bricks: mortar lines and joints
      pat = side ? 0x11 : 0x33;
      if(tex == 0 || tex == 4) pat |= (tex == 0) ? 0x0F : 0xF0;
      break;
    case C_TECH:                                  // tech panels with stripes
      pat = (tex == 1 || tex == 6) ? 0xFF : (tex == 3 || tex == 4) ? (side ? 0x00 : 0x55) : (side ? 0x22 : 0x00);
      break;
    case C_DOOR:                                  // door with a gap in the middle
      pat = (tex == 3 || tex == 4) ? 0x00 : (side ? 0xAA : 0xFF);
      break;
    default:                                      // exit switch: blinking panel
      pat = (tex >= 2 && tex <= 5) ? ((frame & 8) ? 0x99 : 0x66) : 0xFF;
      break;
  }
  // fade with the distance
  if(h < 12)      pat &= odd ? 0x22 : 0x88;
  else if(h < 22) pat &= odd ? 0xAA : 0x55;
  return pat;
}

// Draw the whole screen page by page and send it to the OLED.
// hud: status bar and gun; text: message in the middle (big: scale 2 title)
void Tiny_Flip(uint8_t hud, const char *text, uint8_t big) {
  uint8_t page, x;
  for(page = 0; page < 8; page++) {
    int8_t r0 = page << 3;
    if(page < VIEW_PAGES) {
      // Walls: one ray for two pixel columns
      for(x = 0; x < 128; x += 2) {
        uint8_t c = x >> 1, h = wallH[c], info = wallInfo[c];
        int16_t top = HORIZON - (h >> 1), bot = top + h;   // wall rows [top, bot)
        int16_t s = top - r0, e = bot - r0;
        uint8_t m = 0, edge = 0, b0 = 0, b1 = 0;
        if(s < 8 && e > 0) {
          m = 0xFF;
          if(s > 0) m &= 0xFF << s;
          if(e < 8) m &= 0xFF >> (8 - e);
          if(s >= 0) edge |= 1 << s;                // top edge
          if(e <= 8) edge |= 1 << (e - 1);          // bottom edge
          b0 = (WallPattern(x, info, h) & m) | edge;
          b1 = (WallPattern(x + 1, info, h) & m) | edge;
        }
        // Floor: sparse dots that get denser towards the bottom
        if(r0 >= 40) {
          uint8_t f0 = (r0 >= 48) ? 0x11 : ((x & 2) ? 0x04 : 0x40);
          uint8_t f1 = (r0 >= 48) ? 0x44 : 0;
          uint8_t fm = (s >= 8 || e <= 0) ? 0xFF : (e < 8) ? (0xFF << e) : 0;
          b0 |= f0 & fm; b1 |= f1 & fm;
        }
        buf[x] = b0; buf[x + 1] = b1;
      }

      // Monsters and items
      for(uint8_t i = 0; i < nvis; i++) {
        VISIBLE *v = &vis[order[i]];
        if(v->top >= r0 + 8 || v->top + v->size <= r0) continue;
        int16_t x0 = v->sx - (v->size >> 1);
        int16_t xs = x0 < 0 ? 0 : x0, xe = x0 + v->size;
        if(xe > 128) xe = 128;
        int16_t ys = v->top > r0 ? v->top : r0;
        int16_t ye = v->top + v->size < r0 + 8 ? v->top + v->size : r0 + 8;
        uint16_t u = (xs - x0) * v->step;
        uint16_t v0 = (ys - v->top) * v->step;
        const uint16_t *spr = &SPRITES[v->spr * 32];
        for(int16_t sx = xs; sx < xe; sx++, u += v->step) {
          if(zbuf[sx >> 1] < v->depth) continue;    // behind a wall
          uint16_t img = spr[u >> 8], msk = spr[16 + (u >> 8)];
          uint16_t vv = v0;
          for(int16_t y = ys; y < ye; y++, vv += v->step) {
            uint8_t t = vv >> 8, bit = 1 << (y - r0);
            if(msk & (1 << t)) {
              if(img & (1 << t)) buf[sx] |= bit;
              else               buf[sx] &= ~bit;
            }
          }
        }
      }

      if(hud) {
        // Gun (bobs while walking, flash when firing)
        if(page >= 4) {
          const uint16_t *g = (gunTimer > 6) ? &GUN[44] : GUN;
          int8_t bob = (walkAnim & 8) ? 1 : -1;
          int16_t gx = 53 + bob, gy = 40 + ((walkAnim & 4) ? 1 : 0) - (gunTimer > 6 ? 2 : 0);
          int8_t sh = gy - r0;
          if(sh > -16 && sh < 8) {
            for(uint8_t i = 0; i < 22; i++) {
              uint16_t img = g[i], msk = g[22 + i];
              uint8_t im8 = sh >= 0 ? img << sh : img >> -sh;
              uint8_t mk8 = sh >= 0 ? msk << sh : msk >> -sh;
              buf[gx + i] = (buf[gx + i] & ~mk8) | im8;
            }
          }
        }
        // Crosshair
        if(page == 3) {buf[63] ^= 0x10; buf[65] ^= 0x10; buf[64] ^= 0x28;}
      }
    }
    else {
      // Status bar: health, ammo, face, level
      for(x = 0; x < 128; x++) buf[x] = 0x01;      // line on top
      if(hud) {
        for(x = 0; x < 5; x++) buf[1 + x] |= ICON_HEART[x] << 2;
        DrawNumber(7, 8, 58, health > 0 ? health : 0, 0);
        for(x = 0; x < 3; x++) buf[26 + x] |= ICON_AMMO[x] << 2;
        DrawNumber(7, 31, 58, ammo, 0);
        const uint8_t *f = &FACES[(health > 66 ? 0 : health > 33 ? 1 : 2) * 8];
        uint8_t look = (frame & 64) ? 1 : 0;
        for(x = 0; x < 8; x++) buf[60 + x + look] = f[x];
        DrawNumber(7, kills > 9 ? 96 : 100, 58, kills, 0);
        for(x = 0; x < 3; x++) buf[104 + x] |= DIGITS[33 + x] << 2;   // slash
        DrawNumber(7, 108, 58, monsters, 0);
        for(x = 0; x < 3; x++) buf[120 + x] |= DIGITS[30 + x] << 2;   // L
        DrawNumber(7, 124, 58, level + 1, 0);
      }
    }

    // Text
    if(text) {
      if(big) {
        DrawText(page, 10, 4, "TINY DOOM", 2);
        DrawText(page, 34, 47, text, 1);
      }
      else DrawText(page, 64 - 3 * 10, 24, text, 1);
    }
    else if(big) DrawText(page, 10, 4, "TINY DOOM", 2);

    // Send page to OLED
    JOY_OLED_data_start(page);
    for(x = 0; x < 128; x++) JOY_OLED_send(buf[x]);
    JOY_OLED_end();
  }
}
