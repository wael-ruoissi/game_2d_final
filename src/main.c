#include "header.h"

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;

    if (!init_sdl(&window, &renderer)) {
        return 1;
    }

    Assets assets = {0};
    Player player = {0};
    GameState game_state = {0};

    if (!load_assets(renderer, &assets)) {
        cleanup(window, renderer, &assets, &player);
        return 1;
    }

    game_state.lasers[0].x = assets.world_w / 3.0f;
    game_state.lasers[0].y = 0.0f;
    game_state.lasers[0].speed = 3.0f;
    game_state.lasers[0].dir = 1;
    game_state.lasers[0].w = 45;
    game_state.lasers[0].h = 164;

    game_state.lasers[1].x = 2.0f * assets.world_w / 3.0f;
    game_state.lasers[1].y = 100.0f;
    game_state.lasers[1].speed = 4.0f;
    game_state.lasers[1].dir = -1;
    game_state.lasers[1].w = 45;
    game_state.lasers[1].h = 164;

    init_player(&player, &assets, &game_state);

    Camera camera = {0};
    init_camera(&camera, &player, &assets);

    SDL_Event event;
    int running = 1;

    while (running) {
        handle_events(&event, &running, &game_state);

        const Uint8 *keys = SDL_GetKeyboardState(NULL);
        update_player(&player, keys, &assets, &game_state);
        update_lasers(&game_state, &assets);
        check_laser_collision(&player, &assets, &game_state);
        update_camera(&camera, &player, &assets);
        render(renderer, &player, &camera, &assets, &game_state);

        SDL_Delay(16);
    }

    cleanup(window, renderer, &assets, &player);
    return 0;
}

