#pragma once

#include <fmath.h>
#include <n64sys.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COUNT_OF(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))
#define TICKS_FROM_US(val) ((uint32_t)((val) * (TICKS_PER_SECOND / 1000000)))

#define MAX(a, b) ({ typeof(a) _a = (a); typeof(b) _b = (b); _a > _b ? _a : _b; })
#define MIN(a, b) ({ typeof(a) _a = (a); typeof(b) _b = (b); _a < _b ? _a : _b; })
#define CLAMP(a, min, max)  ({ \
      typeof(a) _a = (a); typeof(min) _min = (min); typeof(max) _max = (max); \
      _a < _min ? _min : (_a > _max ? _max : _a); \
    })
#define SWAP(a, b) do { typeof(a) _a = (a); typeof(b) _b = (b); (a) = _b; (b) = _a; } while (0)

#define SIGN(v) ({ typeof (v) _v = (v); _v > 0 ? 1 : (v < 0 ? -1 : 0); })
#define F2I(f) ({ uint32_t _i; memcpy(&_i, &(f), sizeof _i); _i; })
#define I2F(i) ({ float _f; memcpy(&_f, &(i), sizeof _f); _f; })
#define ALIGN(n, d) ({ \
      typeof(n) _n = (n); typeof(d) _d = (d) - 1; \
      (_n + _d) & ~_d; \
    })

#define FOREACH_ARRAYN(var, array, len) \
  for (typeof(&(array)[0]) var = &(array)[0]; var != &(array)[(len)]; var++)
#define FOREACH_ARRAY(var, array) FOREACH_ARRAYN(var, array, COUNT_OF(array))

#define TAG(x) ({ const char t[5] = (x); (uint32_t) (t[0]<<24)|(t[1]<<16)|(t[2]<<8)|t[3]; })

#define INC_WRAP(val, max) do { \
    if ((val) >= (max) - 1) \
      (val) = 0; \
    else \
      (val) += 1; \
  } while (0)
#define DEC_WRAP(val, max) do { \
    if ((val) == 0) \
      (val) = (max) - 1; \
    else \
      (val) -= 1; \
  } while (0)

#define STEP1(v, t) do { \
    if ((v) > (t)) \
      (v) -= 1; \
    else if ((v) < (t)) \
      (v) += 1; \
  } while (0)

#define STEPTOWARDS(v, t, step) do { \
    if ((v) > (t)) { \
      (v) -= (step); \
      if ((v) < (t)) \
        (v) = (t); \
    } else if ((v) < (t)) { \
      (v) += (step); \
      if ((v) > (t)) \
        (v) = (t); \
    } \
  } while (0)

#define PCG32_INITIALIZER  ((pcg32_random_t) { 0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL })

#define RANDN(state, n) ({ \
  __builtin_constant_p((n)) ? \
    (pcg32_random_r((state))%(n)) : \
    (uint32_t)(((uint64_t)pcg32_random_r((state)) * (n)) >> 32); \
  })

#define RANDF(state) ((pcg32_random_r((state)) >> 8) * (1.f/16777216.f))

#define DEG2RADF (1.f/360.f*(M_PI*2.f))
#define RAD2DEGF (1.f/(M_PI*2.f)*360.f)

#define containerof(ptr, type, member) ({ \
    const typeof(((type *) 0)->member) *_ptr = (ptr); \
    (type *) ((char *) _ptr - offsetof(type, member)); \
})

static const float F1 = 1.0;

typedef struct {
  int32_t x0, y0, x1, y1;
} irect2_t;

typedef struct {
  uint64_t state;
  uint64_t inc;
} pcg32_random_t;

int strcmp0(const char *a, const char *b);

float ease_quad_inout(float v);

uint32_t pcg32_random_r(pcg32_random_t* rng);

inline float ang16_to_radians(uint16_t ang) {
  return ((float) ang) * (1.0/65536.0) * (M_PI*2.0);
}


#ifdef __cplusplus
}
#endif
