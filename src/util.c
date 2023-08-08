#include "util.h"

#include <string.h>

int strcmp0(const char *a, const char *b) {
  if (a == NULL)
    return b == NULL ? 0 : -1;
  if (b == NULL)
    return a == NULL ? 0 : 1;
  return strcmp(a, b);
}

float ease_quad_inout(float v) {
  v *= 2.0;
  if (v <= 1.0)
    return (v * v) * 0.5;
  v -= 1.0;
  return (v * (2.0 - v) + 1.0) * 0.5;
}

uint32_t pcg32_random_r(pcg32_random_t* rng) {
  uint64_t oldstate = rng->state;
  rng->state = oldstate * 6364136223846793005ULL + (rng->inc|1);
  uint32_t xorshifted = ((oldstate >> 18u) ^ oldstate) >> 27u;
  uint32_t rot = oldstate >> 59u;
  return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

extern inline float ang16_to_radians(uint16_t ang);
