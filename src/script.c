#define _GNU_SOURCE
#include <fmath.h>
#include <float.h>
#include <libdragon.h>
#include <malloc.h>
#include <inttypes.h>
#include <stdarg.h>

#include "script.h"
#include "actors.h"
#include "map.h"
#include "scriptops.h"
#include "sound.h"
#include "util.h"

#define TARGET_CAMERA ((int32_t)0x80000000)
#define TARGET_CALLER ((int32_t)0x80000001)
#define SCRIPT_ID_CHILD ((uint32_t)0xffffffff)

typedef struct {
  script_t script;
  void *ptr;
} op_free;

typedef struct {
  script_t script;
  uintptr_t target_id;
} op_jump;

typedef struct {
  script_t script;
  script_exec_t func;
} op_exec;

typedef struct {
  script_t script;
  uint32_t frames;
} op_timer;

typedef struct {
  script_t script;
  uint32_t id;
} op_id;

typedef struct {
  script_t script;
  uint32_t flags;
} op_changestate;

typedef struct {
  script_t script;
  uintptr_t text;
  int32_t target;
} op_showdialog;

typedef struct {
  script_t script;
  int32_t target;
  float speed;
} op_movecamera;

typedef struct {
  script_t script;
  float y;
  float step;
  color_t color;
} op_movewater;

typedef struct {
  script_t script;
  float x;
  float y;
} op_setgravity;

typedef struct {
  script_t script;
  uint32_t map;
  uint32_t fade_type;
  color_t color;
} op_loadmap;

typedef struct {
  script_t script;
  uint32_t music;
  float fade;
  uint32_t flags;
} op_changemusic;

typedef struct {
  script_t script;
  int32_t actor;
  uint32_t sound;
  int32_t priority;
} op_playsound;

typedef struct {
  script_t script;
  uintptr_t spawn;
} op_spawnactor;

typedef struct {
  script_t script;
  int32_t target;
  particle_spawn_t spawn;
} op_spawnparticles;

typedef struct {
  script_t script;
  int32_t actor;
  uint32_t mask;
  uint32_t bits;
} op_setactorstate;

typedef struct {
  script_t script;
  int32_t actor;
  int32_t target;
} op_setactortarget;

typedef struct {
  script_t script;
  int32_t actor;
  uint32_t damage;
  uint32_t source;
} op_damageactor;

typedef struct {
  script_t script;
  uint32_t frames;
  uint32_t strength;
} op_earthquake;

#define script_next(op) ((script_t *) &((op)[1]))

static script_state_t *script_lookup_script(map_t *map, uint32_t id);
static actor_target_t script_lookup_target(script_state_t *state, map_t *map, int32_t id);
static bool script_lookup_position(script_state_t *state, map_t *map, int32_t id, float *x, float *y);

static script_ret_t script_step(script_state_t *state, map_t *map) {
  script_t *script = state->pc;
  switch (script->opcode) {
  case OP_NOOP:
    state->pc = script_next(script);
    break;
  case OP_EXIT:
    return RET_EXIT;
  case OP_RET:
    if (state->stack_pos > 0)
      state->pc = state->stack[--state->stack_pos];
    else
      return RET_EXIT;
    break;
  case OP_JUMP:
    {
      op_jump *op = (void *) script;
      if (op->target_id >= (uintptr_t) KSEG0_START_ADDR) {
        state->pc = (script_t *) op->target_id;
      } else {
        assertf(op->target_id < map->header->script_count, "invalid script id %"PRIuPTR, op->target_id);
        state->pc = map->header->scripts[op->target_id];
      }
    }
    break;
  case OP_CALL:
    {
      op_jump *op = (void *) script;
      assertf(state->stack_pos < SCRIPT_RET_STACK_SIZE - 1, "script return stack overflow");
      state->stack[state->stack_pos++] = script_next(op);
      if (op->target_id >= (uintptr_t) KSEG0_START_ADDR) {
        state->pc = (script_t *) op->target_id;
      } else {
        assertf(op->target_id < map->header->script_count, "invalid script id %"PRIuPTR, op->target_id);
        state->pc = map->header->scripts[op->target_id];
      }
    }
    break;
  case OP_EXEC:
    {
      op_exec *op = (void *) script;
      script_ret_t ret = op->func(map, state);
      if (ret != RET_CONTINUE)
        return ret;
      state->pc = script_next(op);
    }
    break;
  case OP_SINGLETON:
    {
      script_state_t *active;
      TAILQ_FOREACH(active, &map->active_scripts, entry) {
        if (state != active && active->id == state->id)
          return RET_EXIT;
      }
      state->pc = script_next(script);
    }
    break;
  case OP_START_SCRIPT:
    {
      op_jump *op = (void *) script;
      if (op->target_id >= (uintptr_t) KSEG0_START_ADDR) {
        state->child = script_start_dynamic(map, (dynscript_t *) op->target_id, state->caller);
      } else {
        assertf(op->target_id < map->header->script_count, "invalid script id %"PRIuPTR, op->target_id);
        state->child = script_start(map, op->target_id, state->caller);
      }
      state->pc = script_next(op);
    }
    break;
  case OP_WAIT_SCRIPT:
    {
      op_id *op = (void *) script;
      if (op->id == SCRIPT_ID_CHILD) {
        if (state->child)
          return RET_WAIT;
      } else {
        if (script_lookup_script(map, op->id))
          return RET_WAIT;
      }
      state->pc = script_next(op);
    }
    break;
  case OP_STOP_ONE_SCRIPT:
    {
      op_id *op = (void *) script;
      if (op->id == SCRIPT_ID_CHILD) {
        if (state->child)
          script_destroy(state->child, map);
      } else {
        script_state_t *other = script_lookup_script(map, op->id);
        if (other)
          script_destroy(other, map);
      }
      state->pc = script_next(op);
    }
    break;
  case OP_STOP_SCRIPTS:
    {
      op_id *op = (void *) script;
      if (op->id == SCRIPT_ID_CHILD) {
        if (state->child)
          script_destroy(state->child, map);
      } else {
        script_state_t *other, *next;
        TAILQ_FOREACH_SAFE(other, &map->active_scripts, entry, next) {
          if (other->id == op->id)
            script_destroy(other, map);
        }
      }
      state->pc = script_next(op);
    }
    break;
  case OP_STOP_ALL_SCRIPTS:
    {
      script_state_t *other, *next;
      TAILQ_FOREACH_SAFE(other, &map->active_scripts, entry, next)
        script_destroy(other, map);
      state->pc = script_next(script);
    }
    break;
  case OP_DELAY:
    {
      op_timer *op = (void *) script;
      if (!state->waiting) {
        state->counter = op->frames;
        state->waiting = true;
      } else if (state->counter == 0) {
        state->pc = script_next(op);
        state->waiting = false;
      } else {
        state->counter--;
        return RET_WAIT;
      }
    }
    break;
  case OP_WAIT_STATE:
    {
      op_changestate *op = (void *) script;
      if (map->state_flags & op->flags)
        return RET_WAIT;
      state->pc = script_next(op);
    }
    break;
  case OP_ACQUIRE_STATE:
    {
      op_changestate *op = (void *) script;
      if (map->state_flags & op->flags)
        return RET_WAIT;
      map->state_flags |= op->flags;
      state->pc = script_next(op);
    }
    break;
  case OP_RELEASE_STATE:
    {
      op_changestate *op = (void *) script;
      assertf(map->state_flags & op->flags, "state %08"PRIx32" released twice", op->flags);
      map->state_flags &= ~op->flags;
      state->pc = script_next(op);
    }
    break;
  case OP_FORCE_STATE:
    {
      op_changestate *op = (void *) script;
      map->state_flags |= op->flags;
      state->pc = script_next(op);
    }
    break;
  case OP_SHOW_DIALOG:
    {
      op_showdialog *op = (void *) script;
      if (map->dialog_text)
        return RET_WAIT;
      const char *text;
      actor_t *target = NULL;
      if (op->text >= (uintptr_t) KSEG0_START_ADDR) {
        text = (const char *) op->text;
      } else {
        assertf(op->text < map->header->text_count, "invalid text id %"PRIuPTR, op->text);
        text = map->header->texts[op->text];
      }
      if (op->target == TARGET_CALLER)
        target = state->caller;
      else if (op->target > 0)
        target = script_lookup_target(state, map, op->target).actor;
      map_set_dialog(map, text, target);
      state->pc = script_next(op);
    }
    break;
  case OP_WAIT_DIALOG:
    if (map->dialog_text)
      return RET_WAIT;
    else
      state->pc = script_next(script);
    break;
  case OP_MOVE_CAMERA:
    {
      op_movecamera *op = (void *) script;
      if (map->state_flags & MSF_CAMERA_MOVING) {
        return RET_WAIT;
      } else {
        map->state_flags |= MSF_CAMERA_MOVING;
      }
      actor_target_t target = script_lookup_target(state, map, op->target);
      if (target.is_waypoint) {
        map->camera_target = NULL;
        map->camera_waypoint = target.waypoint;
        map->camera_target_vel = fabsf(op->speed);
      } else if (target.actor) {
        map->camera_target = target.actor;
        map->camera_waypoint = NULL;
        map->camera_target_vel = fabsf(op->speed);
      }
      state->pc = script_next(op);
    }
    break;
  case OP_MOVE_WATER:
    {
      op_movewater *op = (void *) script;
      if (map->state_flags & MSF_WATER_MOVING) {
        return RET_WAIT;
      } else {
        map->state_flags |= MSF_WATER_MOVING;
      }
      if (op->y != FLT_MAX) {
        map->target_water_line = op->y;
        map->water_speed = fabsf(op->step);
      }
      if (op->color.a)
        map->target_water_color = op->color;

      state->pc = script_next(op);
    }
    break;
  case OP_SET_GRAVITY:
    {
      op_setgravity *op = (void *) script;
      world_set_gravity(map->world, op->x, op->y);
      state->pc = script_next(op);
    }
    break;
  case OP_LOAD_MAP:
    {
      op_loadmap *op = (void *) script;
      map->pending_map = op->map;
      map->fade = op->fade_type;
      map->fade_color = op->color;
      map->fade_color.a = 255;
      if (map->fade == FADE_OUT_COLOR || map->fade == FADE_OUT_WIPE
          || map->fade == FADE_INOUT_COLOR || map->fade == FADE_INOUT_WIPE)
        map->fade_counter = FADE_LEN;
      else
        map->fade_counter = 1;
      state->pc = script_next(op);
    }
    break;
  case OP_CHANGE_MUSIC:
    {
      op_changemusic *op = (void *) script;
      sound_play_music(op->music, op->fade, op->flags);
      state->pc = script_next(op);
    }
    break;
  case OP_PLAY_SOUND:
    {
      op_playsound *op = (void *) script;
      assertf(op->actor > 0 || op->actor == TARGET_CALLER, "invalid actor id");
      if (op->actor == TARGET_CALLER) {
        if (state->caller)
          actor_play_fx(state->caller, op->sound, op->priority);
      } else {
        actor_t *actor;
        LIST_FOREACH(actor, &map->actors, map) {
          if (actor->id == op->actor) {
            actor_play_fx(actor, op->sound, op->priority);
            break;
          }
        }
      }
      state->pc = script_next(op);
    }
    break;
  case OP_SPAWN_ACTOR:
    {
      op_spawnactor *op = (void *) script;
      if (op->spawn >= (uintptr_t) KSEG0_START_ADDR) {
        actor_spawn(map, (actor_spawn_t *) op->spawn);
      } else {
        assertf(op->spawn < map->header->actor_spawn_count, "invalid actor spawn id %"PRIuPTR, op->spawn);
        actor_spawn(map, &map->header->actor_spawns[op->spawn]);
      }
      state->pc = script_next(op);
    }
    break;
  case OP_SPAWN_PARTICLES:
    {
      op_spawnparticles *op = (void *) script;
      float tx, ty;
      if (script_lookup_position(state, map, op->target, &tx, &ty))
        particle_spawn(map, tx, ty, &op->spawn);
      state->pc = script_next(op);
    }
    break;
  case OP_SET_ACTOR_STATE:
    {
      op_setactorstate *op = (void *) script;
      assertf(op->actor > 0 || op->actor == TARGET_CALLER, "invalid actor id");
      if (op->actor == TARGET_CALLER) {
        if (state->caller)
          state->caller->flags = (state->caller->flags & op->mask) | op->bits;
      } else {
        actor_t *actor;
        LIST_FOREACH(actor, &map->actors, map) {
          if (actor->id == op->actor) {
            actor->flags = (actor->flags & op->mask) | op->bits;
            world_update_actor_state(actor);
          }
        }
      }
      state->pc = script_next(op);
    }
    break;
  case OP_SET_ACTOR_TARGET:
    {
      op_setactortarget *op = (void *) script;
      assertf(op->actor > 0 || op->actor == TARGET_CALLER, "invalid actor id");
      assertf(op->actor != op->target, "actor cannot target itself");
      actor_target_t target = script_lookup_target(state, map, op->target);
      if (target.actor) {
        if (op->actor == TARGET_CALLER) {
          if (state->caller && state->caller->cls->set_target)
            state->caller->cls->set_target(map, state->caller, target);
        } else {
          actor_t *actor;
          LIST_FOREACH(actor, &map->actors, map) {
            if (actor->id == op->actor && actor->cls->set_target)
              state->caller->cls->set_target(map, actor, target);
          }
        }
      }
      state->pc = script_next(op);
    }
    break;
  case OP_DAMAGE_ACTOR:
    {
      op_damageactor *op = (void *) script;
      assertf(op->actor > 0 || op->actor == TARGET_CALLER, "invalid actor id");
      if (op->actor == TARGET_CALLER) {
        if (state->caller && state->caller->cls->damage)
          state->caller->cls->damage(map, state->caller, op->damage, op->source);
      } else {
        actor_t *actor;
        LIST_FOREACH(actor, &map->actors, map) {
          if (actor->id == op->actor && actor->cls->damage)
            actor->cls->damage(map, actor, op->damage, op->source);
        }
      }
      state->pc = script_next(op);
    }
    break;
  case OP_DESTROY_ACTOR:
    {
      op_id *op = (void *) script;
      assertf(op->id > 0, "invalid actor id");
      if (op->id == TARGET_CALLER) {
        if (state->caller)
          actor_destroy(map, state->caller);
      } else {
        actor_t *actor, *next;
        LIST_FOREACH_SAFE(actor, &map->actors, map, next) {
          if (actor->id == op->id)
            actor_destroy(map, actor);
        }
      }
      state->pc = script_next(op);
    }
    break;
  case OP_EARTHQUAKE:
    {
      op_earthquake *op = (void *) script;
      map->quake_counter = MAX(map->quake_counter, op->frames);
      map->quake_strength = MAX(map->quake_strength, op->strength);
      state->pc = script_next(op);
    }
    break;
  case OP_WAIT_EARTHQUAKE:
    if (map->quake_counter)
      return RET_WAIT;
    else
      state->pc = script_next(script);
    break;
  default:
    assertf(0, "unknown script opcode %"PRIu32, script->opcode);
    break;
  }
  return RET_CONTINUE;
}

script_state_t *script_start(map_t *map, uint32_t script_id, actor_t *caller) {
  assertf(script_id < map->header->script_count, "invalid script id %"PRIu32, script_id);
  script_state_t *state = calloc(1, sizeof(script_state_t));
  state->id = script_id;
  state->pc = map->header->scripts[script_id];
  state->caller = caller;
  if (caller)
    LIST_INSERT_HEAD(&caller->callers, state, caller_entry);
  TAILQ_INSERT_TAIL(&map->active_scripts, state, entry);
  bool ret = script_tick(state, map);
  if (ret)
    return state;
  return NULL;
}

void script_destroy(script_state_t *state, map_t *map) {
  if (state->parent && state->parent->child == state)
    state->parent->child = NULL;
  if (state->child && state->child->parent == state)
    state->child->parent = NULL;
  if (state->caller)
    LIST_REMOVE(state, caller_entry);
  TAILQ_REMOVE(&map->active_scripts, state, entry);
  if (state->id >= (uintptr_t) KSEG0_START_ADDR)
    script_unref(state->dyn);
  free(state);
}

bool script_tick(script_state_t *state, map_t *map) {
  script_ret_t ret;
  while (1) {
    ret = script_step(state, map);
    if (ret == RET_WAIT)
      return true;
    if (ret == RET_EXIT) {
      script_destroy(state, map);
      return false;
    }
  }
}

void script_ref(dynscript_t *script) {
  script->refcount++;
}

void script_unref(dynscript_t *script) {
  script->refcount--;
  if (script->refcount == 0) {
    for (size_t i = 0; i < script->free_count; i++)
      free(script->free_ptrs[i]);
    free(script->script);
    free(script);
  }
}

static actor_target_t script_lookup_target(script_state_t *state, map_t *map, int32_t id) {
  actor_target_t target = { .is_waypoint = false };
  if (id == TARGET_CALLER) {
    target.actor = state->caller;
  } else if (id < 0) {
    id = -id - 1;
    if (id < map->header->waypoint_count) {
      target.is_waypoint = true;
      target.waypoint = &map->header->waypoints[id];
    }
  } else {
    actor_t *actor;
    LIST_FOREACH(actor, &map->actors, map) {
      if (actor->id == id) {
        target.actor = actor;
        break;
      }
    }
  }
  return target;
}

static bool script_lookup_position(script_state_t *state, map_t *map, int32_t id, float *x, float *y) {
  if (id == 0) {
    *x = 0;
    *y = 0;
    return true;
  }
  if (id == TARGET_CAMERA) {
    *x = map->camera_x;
    *y = map->camera_y;
    return true;
  }
  actor_target_t target = script_lookup_target(state, map, id);
  if (target.actor) {
    if (target.is_waypoint) {
      *x = target.waypoint->x;
      *y = target.waypoint->y;
    } else {
      world_get_actor_position(target.actor, x, y);
    }
    return true;
  }
  return false;
}

static script_state_t *script_lookup_script(map_t *map, uint32_t id) {
  script_state_t *active;
  TAILQ_FOREACH(active, &map->active_scripts, entry) {
    if (active->id == id)
      return active;
  }
  return NULL;
}

void script_builder_init(script_builder_t *builder) {
  memset(builder, 0, sizeof *builder);
}

static script_t *script_builder_extend(script_builder_t *builder, size_t extra, uint32_t opcode) {
  size_t old_size = builder->script_size;
  builder->script_size += extra;
  builder->script = realloc(builder->script, builder->script_size);

  script_t *script = (script_t *) (((uint8_t *) builder->script) + old_size);
  script->opcode = opcode;

  return script;
}

#define script_builder_push(builder, op, code) \
  ((typeof(op)) script_builder_extend((builder), sizeof(*(op)), (code)))

static dynscript_t *script_builder_finish(script_builder_t *builder) {
  dynscript_t *script = malloc(sizeof(dynscript_t) + builder->free_ptr_count * sizeof(void *));
  script->script = builder->script;
  script->refcount = 1;
  script->free_count = builder->free_ptr_count;
  if (builder->free_ptr_count) {
    memcpy(&script->free_ptrs[0], builder->free_ptrs, builder->free_ptr_count * sizeof(void *));
    free(builder->free_ptrs);
  }
  memset(builder, 0, sizeof *builder);
  return script;
}

static void script_builder_push_free(script_builder_t *builder, void *ptr) {
  size_t end_index = builder->free_ptr_count;
  builder->free_ptr_count++;
  builder->free_ptrs = realloc(builder->free_ptrs, builder->free_ptr_count * sizeof(void *));
  builder->free_ptrs[end_index] = ptr;
}

script_t *script_builder_push_call(script_builder_t *builder, script_t *target) {
  op_jump *op = script_builder_push(builder, op, OP_CALL);
  op->target_id = (uintptr_t) target;
  return &op->script;
}

script_t *script_builder_push_delay(script_builder_t *builder, uint32_t frames) {
  op_timer *op = script_builder_push(builder, op, OP_DELAY);
  op->frames = frames;
  return &op->script;
}

script_t *script_builder_push_exec(script_builder_t *builder, script_exec_t func) {
  op_exec *op = script_builder_push(builder, op, OP_EXEC);
  op->func = func;
  return &op->script;
}

script_t *script_builder_push_dialogf(script_builder_t *builder, int32_t target, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  char *buf;
  int ret = vasprintf(&buf, fmt, args);
  assertf(ret >= 0, "vasprintf failed");
  va_end(args);

  op_showdialog *op = script_builder_push(builder, op, OP_SHOW_DIALOG);
  op->text = (uintptr_t) buf;
  op->target = target;
  script_builder_push_free(builder, buf);
  return &op->script;
}

script_t *script_builder_push_spawn_actor(script_builder_t *builder, void *alloc_arg, actor_spawn_t **spawn) {
  op_spawnactor *op = script_builder_push(builder, op, OP_SPAWN_ACTOR);
  *spawn = malloc(sizeof(actor_spawn_t));
  op->spawn = (uintptr_t) spawn;
  script_builder_push_free(builder, spawn);
  if (alloc_arg != NULL) {
    (*spawn)->arg = alloc_arg;
    script_builder_push_free(builder, alloc_arg);
  }
  return &op->script;
}

script_state_t *script_start_dynamic(map_t *map, dynscript_t *script, actor_t *caller) {
  script_state_t *state = calloc(1, sizeof(script_state_t));
  script_ref(script);
  state->dyn = script;
  state->pc = script->script;
  state->caller = caller;
  if (caller)
    LIST_INSERT_HEAD(&caller->callers, state, caller_entry);
  TAILQ_INSERT_TAIL(&map->active_scripts, state, entry);
  bool ret = script_tick(state, map);
  if (ret)
    return state;
  return NULL;
}

dynscript_t *script_builder_finish_exit(script_builder_t *builder, map_t *map) {
  script_t *end = script_builder_push(builder, end, OP_EXIT);
  return script_builder_finish(builder);
}

dynscript_t *script_builder_finish_ret(script_builder_t *builder, map_t *map) {
  script_t *end = script_builder_push(builder, end, OP_RET);
  return script_builder_finish(builder);
}

dynscript_t *script_builder_finish_jump(script_builder_t *builder, map_t *map, uint32_t id) {
  assertf(id < map->header->script_count, "invalid script id %"PRIu32, id);
  op_jump *end = script_builder_push(builder, end, OP_JUMP);
  end->target_id = id;
  return script_builder_finish(builder);
}

dynscript_t *script_builder_finish_jump_dynamic(script_builder_t *builder, map_t *map, script_t *script) {
  op_jump *end = script_builder_push(builder, end, OP_JUMP);
  end->target_id = (uintptr_t) script;
  return script_builder_finish(builder);
}
