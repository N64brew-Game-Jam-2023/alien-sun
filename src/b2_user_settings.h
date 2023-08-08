#include <libdragon.h>
#include <cstdarg>

// Tunable Constants

/// You can use this to change the length scale used by your game.
/// For example for inches you could use 39.4.
#define b2_lengthUnitsPerMeter 1.f

/// The maximum number of vertices on a convex polygon. You cannot increase
/// this too much because b2BlockAllocator has a maximum object size.
#define b2_maxPolygonVertices	4

// User data
enum body_type_t {
  BODY_UNKNOWN,
  BODY_ACTOR,
  BODY_GROUND,
  BODY_WATER,
};

typedef struct actor_s actor_t;

/// You can define this to inject whatever data you want in b2Body
struct B2_API b2BodyUserData
{
	b2BodyUserData()
	{
		actor = nullptr;
    type = BODY_UNKNOWN;
	}

	/// For legacy compatibility
  actor_t *actor;
  body_type_t type;
};

/// You can define this to inject whatever data you want in b2Fixture
struct B2_API b2FixtureUserData
{
	b2FixtureUserData()
	{
		id = 0;
	}
  uint32_t id;
};

/// You can define this to inject whatever data you want in b2Joint
struct B2_API b2JointUserData
{
	b2JointUserData()
	{
	}
};

// Memory Allocation

/// Default allocation functions
B2_API void* b2Alloc_Default(int32 size);
B2_API void b2Free_Default(void* mem);

/// Implement this function to use your own memory allocator.
inline void* b2Alloc(int32 size)
{
	return b2Alloc_Default(size);
}

/// If you implement b2Alloc, you should also implement this function.
inline void b2Free(void* mem)
{
	b2Free_Default(mem);
}

/// Implement this to use your own logging.
inline void b2Log(const char* string, ...)
{
#ifndef NDEBUG
	va_list args;
	va_start(args, string);
	vfprintf(stderr, string, args);
	va_end(args);
#endif
}
