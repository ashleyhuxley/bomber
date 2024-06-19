#include <furi.h>

#include "bomber.h"
#include "bomber_ui.h"
#include "bomber_loop.h"
#include "levels.h"
#include "helpers.h"

static BomberAppState* state;

BomberAppState* bomber_app_state_get() {
    return state;
}

void bomber_app_init() {
    FURI_LOG_T(TAG, "bomber_app_init");
    state = malloc(sizeof(BomberAppState));
    state->queue = furi_message_queue_alloc(8, sizeof(BomberEvent));
    state->level = level1;
    state->player = bomber_app_get_player(state->level);
    state->data_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    state->mode = BomberAppMode_Uninitialised;

    state->notification = furi_record_open(RECORD_NOTIFICATION);
    notification_message_block(state->notification, &sequence_display_backlight_enforce_on);

    state->timer = furi_timer_alloc(bomber_game_update_timer_callback, FuriTimerTypePeriodic, state->queue);
    furi_timer_start(state->timer, furi_kernel_get_tick_frequency() / 4);

    state->bomb_ix = 0;
}

void bomber_game_update_timer_callback() {
    FURI_LOG_T(TAG, "bomber_game_update_timer_callback");

    state->now++;

    for (int i = 0; i < 10; i++)
    {
        Bomb bomb = state->bombs[i];
        if (bomb.planted > 0)
        {
            int time = state->now - bomb.planted;
            if (time > 10) { state->bombs[i].state = BombState_Hot; }
            if (time > 11) { state->bombs[i].state = BombState_Planted; }
            if (time > 12) { state->bombs[i].state = BombState_Hot; }
            if (time > 13) { state->bombs[i].state = BombState_Planted; }
            if (time > 14) { state->bombs[i].state = BombState_Hot; }
            if (time > 15) {
                state->bombs[i].state = BombState_Explode;
                (state->level)[ix(bomb.x - 1, bomb.y)] = BlockType_Empty;
                (state->level)[ix(bomb.x + 1, bomb.y)] = BlockType_Empty;
                (state->level)[ix(bomb.x, bomb.y - 1)] = BlockType_Empty;
                (state->level)[ix(bomb.x, bomb.y + 1)] = BlockType_Empty;
            }
            if (time > 16) { 
                state->bombs[i].planted = 0;
                state->bombs[i].state = BombState_None;
            }
        }
    }

    view_port_update(state->view_port);
}

void bomber_app_destroy() {
    FURI_LOG_T(TAG, "bomber_app_destroy");
    furi_timer_free(state->timer);
    furi_message_queue_free(state->queue);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    furi_mutex_free(state->data_mutex);
    free(state);
}

int32_t bomber_main(void* p) {
    FURI_LOG_T(TAG, "bomber_app");
    UNUSED(p);

    FURI_LOG_I(TAG, "Initialising app");
    bomber_app_init();
    bomber_ui_init(state);
    state->mode = BomberAppMode_Ready;

    bomber_main_loop(state);

    FURI_LOG_I(TAG, "Destroying app");
    bomber_ui_destroy(state);
    bomber_app_destroy();
    return 0;
}
