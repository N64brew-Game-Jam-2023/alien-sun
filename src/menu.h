#pragma once

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct surface_s surface_t;
typedef struct map_s map_t;

global_state_t main_menu_loop(map_t *map);
global_state_t pause_loop(map_t *map);

#ifdef __cplusplus
}
#endif
