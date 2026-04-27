#ifndef HEADER_H
#define HEADER_H

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <stdio.h>
#include <stdlib.h>

#define WINDOW_WIDTH 1024
#define WINDOW_HEIGHT 576

#define PLAYER_WIDTH 64
#define PLAYER_HEIGHT 64
#define PLAYER_SPEED 5

/* Collision pieds : proportion 18×14 pour sprite 56 → ×64/56. */
#define PLAYER_HITBOX_W 21
#define PLAYER_HITBOX_H 16

/* Spritesheet.jpeg : grille 5 colonnes × 4 lignes, cadre 256×256 (RPG Maker classique). */
#define SPRITE_FRAME_W 256
#define SPRITE_FRAME_H 256
#define SPRITE_FRAME_COLS 5
#define SPRITE_FRAME_ROWS 4
#define WALK_ANIM_MS 95

#define MINIMAP_W 220
#define MINIMAP_H 120
#define MINIMAP_MARGIN 12

#define ROOM_COUNT 3
#define LASER_COUNT 2

#define DIR_UP    0
#define DIR_DOWN  1
#define DIR_LEFT  2
#define DIR_RIGHT 3

typedef struct {
    float x, y;
    int   width, height;
    int   dir;
    int   walk_frame;
    Uint32 last_anim_ms;
} Player;

typedef struct {
    int x, y;
    int w, h;
} Camera;

typedef struct {
    float x, y;
    float speed;
    int dir;
    int w, h;
} Laser;

typedef struct {
    SDL_Texture *rooms[ROOM_COUNT];
    int loaded_rooms;
    SDL_Texture *minimap;
    SDL_Texture *player_sheet;
    SDL_Texture *laser_tex;
    SDL_Surface *mask_surface;
    int minimap_tex_w;
    int minimap_tex_h;
    int room_w;
    int room_h;
    int world_w;
    int world_h;
    int mask_w, mask_h;
} Assets;

typedef struct {
    int minimap_enlarged;
    Laser lasers[LASER_COUNT];
} GameState;

int init_sdl(SDL_Window **win, SDL_Renderer **ren);
int load_assets(SDL_Renderer *ren, Assets *a);
void init_player(Player *p, Assets *a, GameState *state);
void update_lasers(GameState *state, Assets *a);
void init_camera(Camera *cam, Player *p, Assets *a);
void handle_events(SDL_Event *e, int *running, GameState *state);
void update_player(Player *p, const Uint8 *keys, Assets *a, GameState *state);
void update_camera(Camera *cam, Player *p, Assets *a);
void render(SDL_Renderer *ren, Player *p, Camera *cam, Assets *a, GameState *state);
void cleanup(SDL_Window *win, SDL_Renderer *ren, Assets *a, Player *p);
void check_laser_collision(Player *p, Assets *a, GameState *state);

#endif

