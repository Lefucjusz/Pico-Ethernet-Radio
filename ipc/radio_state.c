#include "radio_state.h"
#include <utils.h>

static const char *radio_state_names[] = {
    "GET_LINK",
    "GET_IP",
    "READY",
    "STARTING_STREAM",
    "STARTING_DECODER",
    "STARTING_PLAYER",
    "PLAYBACK_RUNNING",
    "AWAITING_RECONNECT",
    "ERROR"
};

static_assert(RADIO_STATE_COUNT == UTILS_ARRAY_COUNT(radio_state_names), "States enum and name look-up table out of sync!");

const char *radio_state_get_name(radio_state_t state)
{
    if (state >= RADIO_STATE_COUNT) {
        return "UNKNOWN";
    }

    return radio_state_names[state];
}
