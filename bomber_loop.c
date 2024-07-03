#include "bomber_loop.h"
#include "helpers.h"
#include "subghz.h"

#define BOMB_HOT_TIME      furi_ms_to_ticks(2000)
#define BOMB_PLANTED_TIME  furi_ms_to_ticks(2100)
#define BOMB_EXPLODE_TIME  furi_ms_to_ticks(2500)
#define BOMB_RESET_TIME    furi_ms_to_ticks(2600)
#define MAX_X              16
#define MAX_Y              8

// End the game but don't quit the app
// TODO: Figure out whether this actually needs to be here. What should happen when the player presses Back during gameplay?
static void bomber_app_stop_playing(BomberAppState* state) 
{
    FURI_LOG_I(TAG, "Stop playing");
    bomber_app_set_mode(state, BomberAppMode_Finished);
}

// Quit the app
static void bomber_app_quit(BomberAppState* state)
{
    FURI_LOG_I(TAG, "Quitting");
    bomber_app_set_mode(state, BomberAppMode_Quit);
}

// Put the game in error state
static void bomber_app_error(BomberAppState* state)
{
    FURI_LOG_E(TAG, "Error occurred");
    bomber_app_set_mode(state, BomberAppMode_Error);
}

// Start playing
static void bomber_app_start(BomberAppState* state)
{
    FURI_LOG_I(TAG, "Start playing");
    bomber_app_set_mode(state, BomberAppMode_Playing);
}

// Handle direction keys to move the player around
// state: Pointer to the application state
// input: Represents the input event
// returns: true if the viewport should be updated, else false
static bool handle_game_direction(BomberAppState* state, InputEvent input)
{
    Player* player = get_player(state);

    Point newPoint = { player->x, player->y };

    switch(input.key)
    {
        case InputKeyUp:
            if (player->y == 0) return false;
            newPoint.y -= 1;
            break;
        case InputKeyDown:
            if (player->y >= 7) return false;
            newPoint.y += 1;
            break;
        case InputKeyLeft:
            if (player->x == 0) return false;
            newPoint.x -= 1;
            break;
        case InputKeyRight:
            if (player->x >= 15) return false;
            newPoint.x += 1;
            break;
        default:
            return false;
    }

    // Only allow move to new position if the block at that position is not occupied
    BlockType block = (BlockType)(state->level)[ix(newPoint.x, newPoint.y)];
    if (block == BlockType_Empty)
    {
        player->x = newPoint.x;
        player->y = newPoint.y;

        tx_new_position(player, state);

        return true;
    }

    return false;
}

// Handles input while on player select screen - just switch between Fox/Wolf
// state: Pointer to the application state
// input: Represents the input event
// returns: true if the viewport should be updated, else false
static bool handle_menu_input(BomberAppState* state, InputEvent input)
{
    if (input.type == InputTypeShort)
    {
        switch(input.key)
        {
            case InputKeyUp:
            case InputKeyDown:
            case InputKeyLeft:
            case InputKeyRight:
                state->isPlayerTwo = !state->isPlayerTwo;
                return true;
            case InputKeyOk:
                bomber_app_start(state);
                return true;
            default:
                return false;
        }
    }
    return false;
}

// Handle input while playing the game
// state: Pointer to the application state
// input: Represents the input event
// returns: true if the viewport should be updated, else false
static bool handle_game_input(BomberAppState* state, InputEvent input)
{
    Player* player = get_player(state);

    if(input.type == InputTypeShort)
    {
        switch(input.key)
        {
            case InputKeyOk:
                FURI_LOG_I(TAG, "Drop Bomb");
               
                Bomb bomb;
                bomb.x = player->x;
                bomb.y = player->y;
                bomb.state = BombState_Planted;
                bomb.planted = furi_get_tick();
                player->bombs[state->bomb_ix] = bomb;

                state->bomb_ix = (state->bomb_ix + 1) % 10;
                return true;
            case InputKeyUp:
            case InputKeyDown:
            case InputKeyLeft:
            case InputKeyRight:
                return handle_game_direction(state, input);
            case InputKeyBack:
                if(state->mode == BomberAppMode_Playing)
                {
                    bomber_app_stop_playing(state);
                    return true;
                }
                break;
            default:
                break;
        }
    }

    return false;
}

// Main entry point for handling input
// state: Pointer to the application state
// input: Represents the input event
// returns: true if the viewport should be updated, else false
static bool bomber_app_handle_input(BomberAppState* state, InputEvent input)
{
    if(input.type == InputTypeLong && input.key == InputKeyBack)
    {
        bomber_app_quit(state);
        return false; // don't try to update the UI while quitting
    }

    switch (state->mode)
    {
        case BomberAppMode_Playing:
            return handle_game_input(state, input);
        case BomberAppMode_Menu:
            return handle_menu_input(state, input);
        default:
            break;
    }

    return false;
}

// Application main loop
// state: Pointer to the application state
void bomber_main_loop(BomberAppState* state)
{
    FURI_LOG_I(TAG, "Begin application main loop");
    view_port_update(state->view_port);

    BomberEvent event;

    state->running = true;

    while(state->running)
    {
        subghz_check_incoming(state);

        switch(furi_message_queue_get(state->queue, &event, LOOP_MESSAGE_TIMEOUT_ms))
        {
            case FuriStatusOk:
                FURI_LOG_I(TAG, "Event from queue: %d", event.type);
                bool updated = false;
                switch(event.type)
                {
                    case BomberEventType_Input:
                        updated = bomber_app_handle_input(state, event.input);
                        break;
                    default:
                        FURI_LOG_E(TAG, "Unknown event received from queue.");
                        break;
                }

                if(updated)
                {
                    view_port_update(state->view_port);
                }
                break;

            // error cases
            case FuriStatusErrorTimeout:
                FURI_LOG_D(TAG, "furi_message_queue_get timed out");
                break;
            default:
                FURI_LOG_E(TAG, "furi_message_queue_get was neither ok nor timeout");
                bomber_app_error(state);
        }

        if(state->mode == BomberAppMode_Quit || state->mode == BomberAppMode_Error)
        {
            state->running = false;
        }
    }
}

static bool update_bombs(Player* player, BomberAppState* state)
{
    bool changed = false;

    for (uint8_t i = 0; i < 10; i++)
    {
        Bomb* bomb = &player->bombs[i];
        if (bomb->state != BombState_None)
        {
            uint32_t time = furi_get_tick() - bomb->planted;

            if (time > BOMB_RESET_TIME)
            {
                bomb->planted = 0;
                bomb->state = BombState_None;
                continue;
            }

            if (time > BOMB_EXPLODE_TIME)
            {
                bomb->state = BombState_Explode;

                for (uint8_t j = 0; j < player->bomb_power + 1; j++)
                {
                    uint8_t bx, by;

                    bx = bomb->x - j;
                    by = bomb->y;
                    if (bx < MAX_X) {
                        changed = true;
                        state->level[ix(bx, by)] = BlockType_Empty;
                    }

                    bx = bomb->x + j;
                    by = bomb->y;
                    if (bx < MAX_X) {
                        changed = true;
                        state->level[ix(bx, by)] = BlockType_Empty;
                    }

                    bx = bomb->x;
                    by = bomb->y - j;
                    if (by < MAX_Y) {
                        changed = true;
                        state->level[ix(bx, by)] = BlockType_Empty;
                    }

                    bx = bomb->x;
                    by = bomb->y + j;
                    if (by < MAX_Y) {
                        changed = true;
                        state->level[ix(bx, by)] = BlockType_Empty;
                    }
                }

                continue;
            }

            if (time > BOMB_PLANTED_TIME) {
                bomb->state = BombState_Planted;
            } else if (time > BOMB_HOT_TIME) {
                bomb->state = BombState_Hot;
            }
        }
    }

    return changed;
}

void bomber_game_tick(BomberAppState* state)
{
    bool changed = false;
    changed &= update_bombs(&state->fox, state);
    changed &= update_bombs(&state->wolf, state);

    if (changed)
    {
        view_port_update(state->view_port);
    }
}

