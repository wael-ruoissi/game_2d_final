#include "header.h"
#include <math.h>

static int player_collides(Player *p, Assets *a, GameState *state);
static void find_valid_spawn(Player *p, Assets *a, GameState *state);

/* RGBA + suppression du fond très clair (JPEG : blanc, gris clair, franges). */
static SDL_Surface *prepare_player_sheet_surface(SDL_Surface *loaded)
{
    SDL_Surface *s = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA8888, 0);
    SDL_FreeSurface(loaded);
    if (!s) {
        return NULL;
    }

    if (SDL_LockSurface(s) != 0) {
        SDL_FreeSurface(s);
        return NULL;
    }

    for (int y = 0; y < s->h; y++) {
        Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + y * s->pitch);
        for (int x = 0; x < s->w; x++) {
            Uint8 r, g, b, a;
            SDL_GetRGBA(row[x], s->format, &r, &g, &b, &a);
            int mx = r > g ? r : g;
            mx = mx > b ? mx : b;
            int mn = r < g ? r : g;
            mn = mn < b ? mn : b;
            int sum = (int)r + (int)g + (int)b;
            int is_bg = (r >= 248 && g >= 248 && b >= 248) ||
                        (sum >= 728 && mx >= 242 && (mx - mn) <= 24);
            if (is_bg) {
                row[x] = SDL_MapRGBA(s->format, 0, 0, 0, 0);
            } else {
                row[x] = SDL_MapRGBA(s->format, r, g, b, 255);
            }
        }
    }

    SDL_UnlockSurface(s);
    return s;
}

static Uint32 get_pixel(SDL_Surface *s, int x, int y)
{
    if (!s || x < 0 || y < 0 || x >= s->w || y >= s->h) {
        return 0;
    }

    Uint8 *base = (Uint8 *)s->pixels + y * s->pitch + x * s->format->BytesPerPixel;
    switch (s->format->BytesPerPixel) {
        case 1: return *base;
        case 2: return *(Uint16 *)base;
        case 3:
            if (SDL_BYTEORDER == SDL_BIG_ENDIAN) {
                return (Uint32)base[0] << 16 | (Uint32)base[1] << 8 | (Uint32)base[2];
            }
            return (Uint32)base[0] | (Uint32)base[1] << 8 | (Uint32)base[2] << 16;
        case 4: return *(Uint32 *)base;
        default: return 0;
    }
}

/* Laser rouge sur le masque : franchissable (pas un mur bleu). */
static int is_red_laser_pixel(Uint8 r, Uint8 g, Uint8 b)
{
    return (r >= 90 && r > g + 20 && r > b + 20 && g < 135 && b < 135);
}

/* Mur / zone bleue sur Sans titre.jpeg = interdit. Le reste = zone non bleue (sol gris, salle).
 * Les bleus très sombres (b < 118) comptaient à tort comme praticables : on les bloque aussi
 * si B domine nettement R et G (caverne, eau, extérieur). Exception : sol cyan (G élevé). */
static int is_blue_wall_color(Uint8 r, Uint8 g, Uint8 b)
{
    if (is_red_laser_pixel(r, g, b)) {
        return 0;
    }
    /* Sol type cyan / gris-vert : pas un mur bleu. */
    if (g >= 94 && (int)b - (int)g < 58) {
        return 0;
    }
    /* Bleu sombre ou moyen : B au-dessus de R et G (ex. (6,66,116)). */
    if (b >= 85 && (int)b - (int)r >= 26 && (int)b - (int)g >= 20) {
        return 1;
    }
    if (b < 118) {
        return 0;
    }
    if (r >= 122) {
        return 0;
    }
    if ((int)b - (int)r < 38) {
        return 0;
    }
    if ((int)b - (int)g < 36) {
        return 0;
    }
    if (g >= 108 && (int)b - (int)g < 50) {
        return 0;
    }
    return 1;
}

/* 1 = pixel praticable (zone non bleue du masque, alignée sur levil1 par salle). */
static int is_walkable_mask_at(Assets *a, int wx, int wy)
{
    if (!a->mask_surface) {
        return 1;
    }

    if (wx < 0 || wy < 0 || wx >= a->world_w || wy >= a->world_h) {
        return 0;
    }

    int local_x = (wx * a->mask_w) / a->world_w;
    int local_y = (wy * a->mask_h) / a->world_h;
    if (local_x < 0 || local_x >= a->mask_w || local_y < 0 || local_y >= a->mask_h) {
        return 0;
    }

    Uint32 pixel = get_pixel(a->mask_surface, local_x, local_y);
    Uint8 r, g, b;
    SDL_GetRGB(pixel, a->mask_surface->format, &r, &g, &b);

    if (is_red_laser_pixel(r, g, b)) {
        return 1;
    }
    return !is_blue_wall_color(r, g, b);
}

/* Hitbox pieds, centrée en bas du sprite : suit le chemin du masque (Sans titre / levil1). */
static void player_feet_hitbox(const Player *p, int *x1, int *y1, int *x2, int *y2)
{
    int hbw = PLAYER_HITBOX_W;
    int hbh = PLAYER_HITBOX_H;
    *x1 = (int)p->x + p->width / 2 - hbw / 2;
    *y1 = (int)p->y + p->height - hbh;
    *x2 = *x1 + hbw - 1;
    *y2 = *y1 + hbh - 1;
}

static int player_collides(Player *p, Assets *a, GameState *state)
{
    int x1, y1, x2, y2;
    player_feet_hitbox(p, &x1, &y1, &x2, &y2);

    int cx = (x1 + x2) / 2;
    int cy = (y1 + y2) / 2;

    int points[5][2] = {
        {x1, y1}, {x2, y1}, {x1, y2}, {x2, y2},
        {cx, cy}
    };

    for (int i = 0; i < 5; i++) {
        if (!is_walkable_mask_at(a, points[i][0], points[i][1])) {
            return 1;
        }
    }

    if (state) {
        for (int i = 0; i < LASER_COUNT; i++) {
            Laser *l = &state->lasers[i];
            if (x1 < l->x + l->w &&
                x2 > l->x &&
                y1 < l->y + l->h &&
                y2 > l->y) {
                return 1;
            }
        }
    }
    return 0;
}

static int dir_to_sheet_row(int dir)
{
    /* 0=bas, 3=haut. Gauche et droite partagent la ligne du profil (ligne 1) :
       la droite affiche la même marche avec SDL_FLIP_HORIZONTAL. */
    switch (dir) {
        case DIR_UP:    return 3;
        case DIR_DOWN:  return 0;
        case DIR_LEFT:
        case DIR_RIGHT: return 1;
        default:        return 0;
    }
}

static void find_valid_spawn(Player *p, Assets *a, GameState *state)
{
    const int max_x = a->world_w - p->width;
    const int max_y = a->world_h - p->height;
    if (max_x < 0 || max_y < 0) {
        p->x = 0.0f;
        p->y = 0.0f;
        return;
    }

    /* Start in center of first room for clear visibility */
    float center_x = (float)(a->world_w / ROOM_COUNT) / 2.0f - (float)p->width / 2.0f;
    float center_y = (float)a->world_h / 2.0f - (float)p->height / 2.0f;
    
    Player probe = *p;
    probe.x = center_x;
    probe.y = center_y;
    
    if (!player_collides(&probe, a, state)) {
        p->x = center_x;
        p->y = center_y;
        return;
    }

    /* Fallback to preferred positions if center is blocked */
    const float preferred[][2] = {
        { 400.0f, 350.0f },
        { 480.0f, 340.0f },
        { 420.0f, 360.0f },
        { 380.0f, 380.0f },
        { 520.0f, 320.0f }
    };
    for (int i = 0; i < (int)(sizeof(preferred) / sizeof(preferred[0])); i++) {
        float x = preferred[i][0];
        float y = preferred[i][1];
        if (x < 0.0f || y < 0.0f || x > (float)max_x || y > (float)max_y) {
            continue;
        }
        Player probe = *p;
        probe.x = x;
        probe.y = y;
        if (!player_collides(&probe, a, state)) {
            p->x = x;
            p->y = y;
            return;
        }
    }

    /* Cherche d’abord la moitié gauche (salle intérieure), puis la droite — toujours zone non bleue. */
    const int steps[4] = {8, 4, 2, 1};
    int split = (a->world_w / ROOM_COUNT) / 2 - p->width / 2;
    if (split < 0) {
        split = 0;
    }
    if (split > max_x) {
        split = max_x;
    }

    for (int si = 0; si < 4; si++) {
        int st = steps[si];
        for (int pass = 0; pass < 2; pass++) {
            int x0 = (pass == 0) ? 0 : split;
            int x1 = (pass == 0) ? split : max_x;
            if (pass == 1 && x0 > x1) {
                continue;
            }
            for (int y = max_y; y >= 0; y -= st) {
                for (int x = x0; x <= x1; x += st) {
                    Player probe = *p;
                    probe.x = (float)x;
                    probe.y = (float)y;
                    if (!player_collides(&probe, a, state)) {
                        p->x = probe.x;
                        p->y = probe.y;
                        return;
                    }
                }
            }
        }
    }

    printf("Erreur: aucune zone non bleue pour le joueur dans la salle.\n");
    p->x = 0.0f;
    p->y = 0.0f;
}

int init_sdl(SDL_Window **win, SDL_Renderer **ren)
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL_Init error: %s\n", SDL_GetError());
        return 0;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    if (!(IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG) & (IMG_INIT_JPG | IMG_INIT_PNG))) {
        printf("IMG_Init error: %s\n", IMG_GetError());
        SDL_Quit();
        return 0;
    }

    *win = SDL_CreateWindow("Mini Map SDL",
                            SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED,
                            WINDOW_WIDTH,
                            WINDOW_HEIGHT,
                            SDL_WINDOW_SHOWN);
    if (!*win) {
        printf("CreateWindow error: %s\n", SDL_GetError());
        IMG_Quit();
        SDL_Quit();
        return 0;
    }

    *ren = SDL_CreateRenderer(*win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!*ren) {
        printf("CreateRenderer error: %s\n", SDL_GetError());
        SDL_DestroyWindow(*win);
        IMG_Quit();
        SDL_Quit();
        return 0;
    }

    return 1;
}

int load_assets(SDL_Renderer *ren, Assets *a)
{
    SDL_Surface *surface = NULL;

    a->player_sheet = NULL;
    a->loaded_rooms = 0;
    
    // Load only level1.jpeg as background
    surface = IMG_Load("levil1.jpeg");
    if (!surface) {
        printf("Cannot load levil1.jpeg: %s\n", IMG_GetError());
        return 0;
    }

    a->rooms[0] = SDL_CreateTextureFromSurface(ren, surface);
    if (!a->rooms[0]) {
        SDL_FreeSurface(surface);
        return 0;
    }

    a->room_w = surface->w;
    a->room_h = surface->h;
    a->world_w = 1955; // Largeur du monde pour permettre le scrolling (identique au masque de collision)
    a->world_h = WINDOW_HEIGHT;
    a->loaded_rooms = 1;
    SDL_FreeSurface(surface);

    surface = IMG_Load("map.jpeg");
    if (!surface) {
        printf("Cannot load map.jpeg: %s\n", IMG_GetError());
        return 0;
    }
    a->minimap = SDL_CreateTextureFromSurface(ren, surface);
    SDL_FreeSurface(surface);
    if (!a->minimap) {
        return 0;
    }
    SDL_QueryTexture(a->minimap, NULL, NULL, &a->minimap_tex_w, &a->minimap_tex_h);

    surface = IMG_Load("sans titre.jpeg");
    if (!surface) {
        surface = IMG_Load("Sans titre.jpeg");
    }
    if (!surface) {
        printf("Cannot load collision mask: %s\n", IMG_GetError());
        return 0;
    }

    a->mask_surface = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGB24, 0);
    SDL_FreeSurface(surface);
    if (!a->mask_surface) {
        return 0;
    }
    a->mask_w = a->mask_surface->w;
    a->mask_h = a->mask_surface->h;

    if (a->mask_w != a->room_w || a->mask_h != a->room_h) {
        printf("Attention: sans titre.jpeg (%dx%d) doit correspondre a levil1 (%dx%d).\n",
               a->mask_w, a->mask_h, a->room_w, a->room_h);
    }

    surface = IMG_Load("Spritesheet.jpeg");
    if (!surface) {
        surface = IMG_Load("spritesheet.jpeg");
    }
    if (!surface) {
        printf("Cannot load Spritesheet.jpeg: %s\n", IMG_GetError());
        return 0;
    }
    surface = prepare_player_sheet_surface(surface);
    if (!surface) {
        printf("prepare_player_sheet_surface failed\n");
        return 0;
    }
    a->player_sheet = SDL_CreateTextureFromSurface(ren, surface);
    SDL_FreeSurface(surface);
    if (!a->player_sheet) {
        return 0;
    }
    SDL_SetTextureBlendMode(a->player_sheet, SDL_BLENDMODE_BLEND);

    surface = IMG_Load("laser.jpeg");
    if (!surface) {
        printf("Cannot load laser.jpeg: %s\n", IMG_GetError());
        return 0;
    }
    a->laser_tex = SDL_CreateTextureFromSurface(ren, surface);
    SDL_FreeSurface(surface);
    if (!a->laser_tex) {
        return 0;
    }

    return 1;
}

void init_player(Player *p, Assets *a, GameState *state)
{
    p->width = PLAYER_WIDTH;
    p->height = PLAYER_HEIGHT;
    p->dir = DIR_DOWN;
    p->walk_frame = 0;
    p->last_anim_ms = SDL_GetTicks();

    find_valid_spawn(p, a, state);
}

void init_camera(Camera *cam, Player *p, Assets *a)
{
    cam->w = WINDOW_WIDTH;
    cam->h = WINDOW_HEIGHT;
    
    // Start at position 0,0 to show first room at beginning
    cam->x = 0;
    cam->y = 0;
}

void handle_events(SDL_Event *e, int *running, GameState *state)
{
    while (SDL_PollEvent(e)) {
        if (e->type == SDL_QUIT) {
            *running = 0;
        }
        if (e->type == SDL_KEYDOWN && e->key.keysym.sym == SDLK_ESCAPE) {
            *running = 0;
        }
        if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
            int mouse_x = e->button.x;
            int mouse_y = e->button.y;
            
            SDL_Rect minimap_rect;
            if (state->minimap_enlarged) {
                // Zone de clic plein écran
                minimap_rect.x = 0;
                minimap_rect.y = 0;
                minimap_rect.w = WINDOW_WIDTH;
                minimap_rect.h = WINDOW_HEIGHT;
            } else {
                // Zone de clic normale
                minimap_rect.x = WINDOW_WIDTH - MINIMAP_W - MINIMAP_MARGIN;
                minimap_rect.y = MINIMAP_MARGIN;
                minimap_rect.w = MINIMAP_W;
                minimap_rect.h = MINIMAP_H;
            }
            
            if (mouse_x >= minimap_rect.x && mouse_x <= minimap_rect.x + minimap_rect.w &&
                mouse_y >= minimap_rect.y && mouse_y <= minimap_rect.y + minimap_rect.h) {
                state->minimap_enlarged = !state->minimap_enlarged;
            }
        }
    }
}

void update_player(Player *p, const Uint8 *keys, Assets *a, GameState *state)
{
    float ix = 0.0f;
    float iy = 0.0f;

    if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) {
        iy -= 1.0f;
    }
    if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) {
        iy += 1.0f;
    }
    if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) {
        ix -= 1.0f;
    }
    if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) {
        ix += 1.0f;
    }

    if (ix == 0.0f && iy == 0.0f) {
        p->walk_frame = 0;
        return;
    }

    float dx = ix * PLAYER_SPEED;
    float dy = iy * PLAYER_SPEED;
    if (ix != 0.0f && iy != 0.0f) {
        const float inv = 0.70710678f;
        dx *= inv;
        dy *= inv;
    }

    int new_dir = p->dir;
    if (ix != 0.0f && iy != 0.0f) {
        if (fabsf(ix) >= fabsf(iy)) {
            new_dir = (ix < 0.0f) ? DIR_LEFT : DIR_RIGHT;
        } else {
            new_dir = (iy < 0.0f) ? DIR_UP : DIR_DOWN;
        }
    } else if (ix != 0.0f) {
        new_dir = (ix < 0.0f) ? DIR_LEFT : DIR_RIGHT;
    } else {
        new_dir = (iy < 0.0f) ? DIR_UP : DIR_DOWN;
    }

    float ox = p->x;
    float oy = p->y;

    Player candidate = *p;
    candidate.x += dx;
    candidate.y += dy;

    if (candidate.x < 0) candidate.x = 0;
    if (candidate.y < 0) candidate.y = 0;
    if (candidate.x > a->world_w - candidate.width) candidate.x = (float)(a->world_w - candidate.width);
    if (candidate.y > a->world_h - candidate.height) candidate.y = (float)(a->world_h - candidate.height);

    if (!player_collides(&candidate, a, NULL)) {
        *p = candidate;
    } else {
        Player x_only = *p;
        x_only.x += dx;
        if (x_only.x < 0) x_only.x = 0;
        if (x_only.x > a->world_w - x_only.width) x_only.x = (float)(a->world_w - x_only.width);
        if (!player_collides(&x_only, a, NULL)) {
            p->x = x_only.x;
        }

        Player y_only = *p;
        y_only.y += dy;
        if (y_only.y < 0) y_only.y = 0;
        if (y_only.y > a->world_h - y_only.height) y_only.y = (float)(a->world_h - y_only.height);
        if (!player_collides(&y_only, a, NULL)) {
            p->y = y_only.y;
        }
    }

    float mx = p->x - ox;
    float my = p->y - oy;
    if (fabsf(mx) < 0.001f && fabsf(my) < 0.001f) {
        p->walk_frame = 0;
        p->dir = new_dir;
        return;
    }

    /* Déplacement horizontal : orientation profil + animation des pieds (spritesheet ligne gauche/droite). */
    if (fabsf(mx) > 0.001f) {
        p->dir = (mx < 0.0f) ? DIR_LEFT : DIR_RIGHT;
        Uint32 now = SDL_GetTicks();
        if (now - p->last_anim_ms >= WALK_ANIM_MS) {
            p->last_anim_ms = now;
            p->walk_frame = (p->walk_frame + 1) % SPRITE_FRAME_COLS;
        }
    } else {
        /* Uniquement vertical : vue de face / de dos, pas de cycle de marche latérale. */
        p->dir = (my < 0.0f) ? DIR_UP : DIR_DOWN;
        p->walk_frame = 0;
    }

}

void update_camera(Camera *cam, Player *p, Assets *a)
{
    /* Centrage horizontal sur le sprite ; vertical sur le niveau des pieds (personnage « à sa place »). */
    int anchor_y = (int)(p->y + (float)p->height - (float)PLAYER_HITBOX_H * 0.5f);
    cam->x = (int)p->x - cam->w / 2 + p->width / 2;
    cam->y = anchor_y - cam->h / 2;

    if (cam->x < 0) cam->x = 0;
    if (cam->y < 0) cam->y = 0;
    if (cam->x > a->world_w - cam->w) cam->x = a->world_w - cam->w;
    if (cam->y > a->world_h - cam->h) cam->y = a->world_h - cam->h;
}

void update_lasers(GameState *state, Assets *a)
{
    if (!state) return;
    for (int i = 0; i < LASER_COUNT; i++) {
        Laser *l = &state->lasers[i];
        l->y += l->speed * l->dir;
        if (l->y < 0) {
            l->y = 0;
            l->dir = 1;
        }
        if (l->y + l->h > a->world_h) {
            l->y = a->world_h - l->h;
            l->dir = -1;
        }
    }
}

void check_laser_collision(Player *p, Assets *a, GameState *state)
{
    if (!state) return;

    int x1, y1, x2, y2;
    player_feet_hitbox(p, &x1, &y1, &x2, &y2);

    for (int i = 0; i < LASER_COUNT; i++) {
        Laser *l = &state->lasers[i];
        if (x1 < l->x + l->w &&
            x2 > l->x &&
            y1 < l->y + l->h &&
            y2 > l->y) {
            init_player(p, a, state);
            return;
        }
    }
}

static void render_rooms(SDL_Renderer *ren, Camera *cam, Assets *a, GameState *state)
{
    (void)state;
    // On affiche la portion de levil1.jpeg qui correspond à la caméra
    SDL_Rect src = {
        (cam->x * a->room_w) / a->world_w,
        (cam->y * a->room_h) / a->world_h,
        (cam->w * a->room_w) / a->world_w,
        (cam->h * a->room_h) / a->world_h
    };
    
    SDL_Rect dst = {0, 0, WINDOW_WIDTH, WINDOW_HEIGHT};
    SDL_RenderCopy(ren, a->rooms[0], &src, &dst);
}

static void render_minimap(SDL_Renderer *ren, Player *p, Assets *a, GameState *state)
{
    int minimap_w, minimap_h, map_x, map_y;
    
    if (state->minimap_enlarged) {
        // Taille plein écran
        minimap_w = WINDOW_WIDTH;
        minimap_h = WINDOW_HEIGHT;
        map_x = 0;
        map_y = 0;
    } else {
        // Taille normale
        minimap_w = MINIMAP_W;
        minimap_h = MINIMAP_H;
        map_x = WINDOW_WIDTH - MINIMAP_W - MINIMAP_MARGIN;
        map_y = MINIMAP_MARGIN;
    }
    
    SDL_Rect map_dst = {
        map_x,
        map_y,
        minimap_w,
        minimap_h
    };

    SDL_RenderCopy(ren, a->minimap, NULL, &map_dst);

    /* Point sur les pieds (hitbox) pour cohérence avec le masque / la position réelle. */
    float player_cx = p->x + p->width * 0.5f;
    float player_cy = p->y + (float)p->height - (float)PLAYER_HITBOX_H * 0.5f;

    float nx = player_cx / (float)a->world_w;
    float ny = player_cy / (float)a->world_h;
    if (nx < 0.0f) nx = 0.0f;
    if (nx > 1.0f) nx = 1.0f;
    if (ny < 0.0f) ny = 0.0f;
    if (ny > 1.0f) ny = 1.0f;

    int dot_x = map_dst.x + (int)(nx * (map_dst.w - 1));
    int dot_y = map_dst.y + (int)(ny * (map_dst.h - 1));
    int dot_size = state->minimap_enlarged ? 16 : 6;
    SDL_Rect dot = {dot_x - dot_size/2, dot_y - dot_size/2, dot_size, dot_size};

    SDL_SetRenderDrawColor(ren, 255, 0, 0, 255);
    SDL_RenderFillRect(ren, &dot);
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    SDL_RenderDrawRect(ren, &map_dst);
}

void render(SDL_Renderer *ren, Player *p, Camera *cam, Assets *a, GameState *state)
{
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);

    if (!state->minimap_enlarged) {
        // Mode normal : afficher les salles et le joueur
        render_rooms(ren, cam, a, state);

        SDL_Rect player_dst = {(int)p->x - cam->x, (int)p->y - cam->y, p->width, p->height};
        SDL_Rect src = {
            p->walk_frame * SPRITE_FRAME_W,
            dir_to_sheet_row(p->dir) * SPRITE_FRAME_H,
            SPRITE_FRAME_W,
            SPRITE_FRAME_H
        };
        /* La ligne profil du spritesheet regarde à droite : gauche = retournement. */
        SDL_RendererFlip flip = (p->dir == DIR_LEFT) ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
        SDL_RenderCopyEx(ren, a->player_sheet, &src, &player_dst, 0.0, NULL, flip);

        for (int i = 0; i < LASER_COUNT; i++) {
            Laser *l = &state->lasers[i];
            SDL_Rect laser_dst = {(int)l->x - cam->x, (int)l->y - cam->y, l->w, l->h};
            SDL_RenderCopy(ren, a->laser_tex, NULL, &laser_dst);
        }
    }

    render_minimap(ren, p, a, state);
    SDL_RenderPresent(ren);
}

void cleanup(SDL_Window *win, SDL_Renderer *ren, Assets *a, Player *p)
{
    for (int i = 0; i < a->loaded_rooms; i++) {
        if (a->rooms[i]) {
            SDL_DestroyTexture(a->rooms[i]);
        }
    }

    if (a->minimap) SDL_DestroyTexture(a->minimap);
    if (a->player_sheet) SDL_DestroyTexture(a->player_sheet);
    if (a->laser_tex) SDL_DestroyTexture(a->laser_tex);
    if (a->mask_surface) SDL_FreeSurface(a->mask_surface);

    (void)p;

    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);

    IMG_Quit();
    SDL_Quit();
}

