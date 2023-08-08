#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <sys/queue.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCRIPT_RET_STACK_SIZE 4

typedef struct map_s map_t;
typedef struct actor_s actor_t;
typedef struct actor_spawn_s actor_spawn_t;

typedef struct {
  uint32_t opcode;
} script_t;

typedef struct {
  script_t *script;
  uint32_t refcount;
  size_t free_count;
  void *free_ptrs[];
} dynscript_t;

typedef struct script_state_s {
  union {
    uintptr_t id;
    dynscript_t *dyn;
  };
  script_t *pc;
  script_t *stack[SCRIPT_RET_STACK_SIZE];
  uint8_t stack_pos;
  bool waiting;
  uint32_t counter;
  struct script_state_s *parent;
  struct script_state_s *child;
  TAILQ_ENTRY(script_state_s) entry;
  actor_t *caller;
  LIST_ENTRY(script_state_s) caller_entry;
} script_state_t;

typedef struct {
  script_t *script;
  size_t script_size;
  void **free_ptrs;
  size_t free_ptr_count;
} script_builder_t;

typedef enum {
  RET_CONTINUE,
  RET_WAIT,
  RET_EXIT,
} script_ret_t;

typedef script_ret_t (*script_exec_t)(map_t *, script_state_t *);

script_state_t *script_start(map_t *map, uint32_t script_id, actor_t *caller);
bool script_tick(script_state_t *state, map_t *map);
void script_destroy(script_state_t *state, map_t *map);

void script_ref(dynscript_t *script);
void script_unref(dynscript_t *script);
script_state_t *script_start_dynamic(map_t *map, dynscript_t *script, actor_t *caller);

void script_builder_init(script_builder_t *builder);

script_t *script_builder_push_noop(script_builder_t *builder);
script_t *script_builder_push_call(script_builder_t *builder, script_t *target);
script_t *script_builder_push_delay(script_builder_t *builder, uint32_t frames);
script_t *script_builder_push_exec(script_builder_t *builder, script_exec_t func);
script_t *script_builder_push_dialogf(script_builder_t *builder, int32_t target, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
script_t *script_builder_push_spawn_actor(script_builder_t *builder, void *alloc_arg, actor_spawn_t **spawn);

dynscript_t *script_builder_finish_exit(script_builder_t *builder, map_t *map);
dynscript_t *script_builder_finish_ret(script_builder_t *builder, map_t *map);
dynscript_t *script_builder_finish_jump(script_builder_t *builder, map_t *map, uint32_t id);
dynscript_t *script_builder_finish_jump_dynamic(script_builder_t *builder, map_t *map, script_t *script);

#ifdef __cplusplus
}
#endif
