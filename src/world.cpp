#include "GL/gl_integration.h"

#include <box2d/box2d.h>
#include <libdragon.h>
#include <malloc.h>
#include <sys/queue.h>
#include <new>
#include <cfloat>
#include <cinttypes>

#include "misc.h"
#include "world.h"
#include "actors.h"
#include "main.h"

#define WATER_BOX_SIZE (4096.0)
#define WATER_DAMPING 0.5f

struct world_s {
  map_t *map;
  b2Body *water;
  b2World world;
};

typedef enum {
  COLL_END = 0,
  COLL_CIRCLE = 1,
  COLL_AABB = 2,
  COLL_TRIANGLE = 3,
  COLL_QUAD = 4,
  COLL_POLY = 5,
  COLL_EDGE = 6,
  COLL_CHAIN = 7,
} collision_type_t;

typedef enum {
  CF_SENSOR      = 1<<0,
  CF_INTERACTIVE = 1<<1,
} collision_flags_t;

typedef struct {
  collision_t collision;
  float r;
  float x0, y0;
} collision_circle_t;

typedef struct {
  collision_t collision;
  float x0, y0, x1, y1;
} collision_aabb_t;

typedef struct {
  collision_t collision;
  float x0, y0, x1, y1, x2, y2;
} collision_tri_t;

typedef struct {
  collision_t collision;
  float x0, y0, x1, y1, x2, y2, x3, y3;
} collision_quad_t;

typedef struct {
  collision_t collision;
  uint32_t count;
  float verts[][2];
} collision_poly_t;

typedef struct {
  collision_t collision;
  float x0, y0, x1, y1;
} collision_edge_t;

typedef struct {
  collision_t collision;
  uint32_t count;
  float px, py, nx, ny;
  float verts[][2];
} collision_chain_t;

struct ContactListener : public b2ContactListener {
  void BeginContact(b2Contact *contact) override {
    this->Handle(contact, nullptr);
  }
  void EndContact(b2Contact *contact) override {
    this->Handle(contact, nullptr);
  }
  void PreSolve(b2Contact *contact, const b2Manifold *old) override {
    this->Handle(contact, old);
  }
  void Handle(b2Contact *contact, const b2Manifold *old);
} contact_listener;

#ifndef NDEBUG
static void world_set_debug_draw(world_t *world);
#endif

static void world_body_add_collision_fixtures(body_t *body, b2FixtureDef fix, collision_t *collision);

world_t *world_new(map_t *map, float gravity_x, float gravity_y, float water_y) {
  world_t *world = static_cast<world_t *>(malloc(sizeof(world_t)));
  world->map = map;

  auto gravity = POINT_SCALE * b2Vec2(gravity_x, gravity_y);
  new (&world->world) b2World(gravity);
  gravity.Normalize();
  map->gravity_norm_x = gravity.x;
  map->gravity_norm_y = gravity.y;

  if (water_y != FLT_MAX) {
    b2BodyDef waterDef;
    waterDef.userData.type = BODY_WATER;
    waterDef.position.Set(0.f, water_y * POINT_SCALE + (WATER_BOX_SIZE*0.5f));
    b2PolygonShape waterBox;
    waterBox.SetAsBox(WATER_BOX_SIZE*0.5f, WATER_BOX_SIZE*0.5f);
    world->water = world->world.CreateBody(&waterDef);
    b2FixtureDef fixDef;
    fixDef.shape = &waterBox;
    fixDef.isSensor = true;
    fixDef.filter.categoryBits = CB_WATER;
    world->water->CreateFixture(&fixDef);
  }
  if (map->header->collision) {
    b2BodyDef bodyDef;
    bodyDef.userData.type = BODY_GROUND;
    b2Body *mapBody = world->world.CreateBody(&bodyDef);
    b2FixtureDef fix;
    fix.density = 1.f;
    fix.friction = 0.4f;
    fix.filter.categoryBits = CB_GROUND;
    world_body_add_collision_fixtures(mapBody, fix, map->header->collision);
  }
  world->world.SetContactListener(&contact_listener);
#ifndef NDEBUG
  world_set_debug_draw(world);
#endif
  return world;
}

void world_tick(world_t *world) {
  world->world.Step(1.0 / (float) FPS, 6, 2);
  world->world.SetAutoClearForces(true);
  world->world.SetAllowSleeping(true);
}

void ContactListener::Handle(b2Contact *contact, const b2Manifold *old) {
  auto fixA = contact->GetFixtureA();
  auto fixB = contact->GetFixtureB();
  auto bodyA = fixA->GetBody();
  auto bodyB = fixB->GetBody();
  auto typeA = bodyA->GetUserData().type;
  auto typeB = bodyB->GetUserData().type;
  if (typeB == BODY_ACTOR) {
    SWAP(typeA, typeB);
    SWAP(bodyA, bodyB);
    SWAP(fixA, fixB);
  }
  if (typeA == BODY_ACTOR) {
    actor_t *a = bodyA->GetUserData().actor;
    switch (typeB) {
    case BODY_GROUND:
      if (a->collider)
        a->collider(a, fixA, NULL, fixB, contact, old);
      break;
    case BODY_ACTOR:
      {
        actor_t *b = bodyB->GetUserData().actor;
        //debugf("collide actors %d %d\n", a->type, b->type);
        if (b->cls->collide_priority > a->cls->collide_priority) {
          SWAP(a, b);
          SWAP(fixA, fixB);
        }
        if (a->collider)
          a->collider(a, fixA, b, fixB, contact, old);
        if (b->collider)
          b->collider(b, fixB, a, fixA, contact, old);
      }
      break;
    case BODY_WATER:
      if (!fixA->IsSensor()) {
        if (contact->IsTouching()) {
          if (!(a->flags & AF_UNDERWATER)) {
            if (bodyA->GetLinearVelocity().y > POINT_SCALE) {
              map_t *map = world_body_get_map(bodyB);
              particle_spawn_splash(map, bodyA->GetWorldCenter().x * INV_POINT_SCALE);
            }
            a->flags |= AF_UNDERWATER;
            bodyA->SetAngularDamping(bodyA->GetAngularDamping() + WATER_DAMPING);
            bodyA->SetLinearDamping(bodyA->GetLinearDamping() + WATER_DAMPING);
          }
        } else if (a->flags & AF_UNDERWATER) {
          if (bodyA->GetLinearVelocity().y < -POINT_SCALE) {
            map_t *map = world_body_get_map(bodyB);
            particle_spawn_splash(map, bodyA->GetWorldCenter().x * INV_POINT_SCALE);
          }
          a->flags &= ~AF_UNDERWATER;
          bodyA->SetAngularDamping(bodyA->GetAngularDamping() - WATER_DAMPING);
          bodyA->SetLinearDamping(bodyA->GetLinearDamping() - WATER_DAMPING);
        }
      }
      if (a->collider)
        a->collider(a, fixA, NULL, fixB, contact, old);
      break;
    default:
      break;
    }
  }
}

void world_destroy(world_t *world) {
  world->world.~b2World();
  world->water = nullptr;
  free(world);
}

struct QueryCallback : public b2QueryCallback {
  world_actor_func_t func;
  const irect2_t *rect;
  void *arg;
  bool ReportFixture(b2Fixture *fix) override {
    auto userdata = fix->GetBody()->GetUserData();
    if (userdata.type == BODY_ACTOR)
      return this->func(userdata.actor, this->rect, arg);
    return true;
  }
};

void world_foreach_actor_in_rect(world_t *world, const irect2_t *rect, int32_t expand, world_actor_func_t func, void *arg) {
  QueryCallback cb;
  cb.func = func;
  cb.arg = arg;
  cb.rect = rect;
  b2AABB aabb = {
    { (rect->x0 - expand) * POINT_SCALE, (rect->y0 - expand) * POINT_SCALE },
    { (rect->x1 + expand) * POINT_SCALE, (rect->y1 + expand) * POINT_SCALE },
  };
  world->world.QueryAABB(&cb, aabb);
}

void world_link_actor(world_t *world, actor_t *actor, float x, float y, float angle) {
  if (!actor->body) {
    uint32_t flags = actor->flags;
    b2BodyDef bodyDef;
    bodyDef.userData.actor = actor;
    bodyDef.userData.type = BODY_ACTOR;
    if (actor->cls->category_bits & CB_PLAYER)
      bodyDef.bullet = true;
    if (flags & AF_KINEMATIC)
      bodyDef.type = b2_kinematicBody;
    else if (!(flags & AF_STATIC))
      bodyDef.type = b2_dynamicBody;
    if (!(flags & AF_GRAVITY))
      bodyDef.gravityScale = 0.f;
    bodyDef.position.Set(x * POINT_SCALE, y * POINT_SCALE);
    bodyDef.angle = angle;
    bodyDef.fixedRotation = !(flags & AF_ROTATES);
    actor->body = world->world.CreateBody(&bodyDef);
    if (y > world->map->water_line && !(actor->flags & AF_UNDERWATER)) {
      actor->flags |= AF_UNDERWATER;
      actor->body->SetAngularDamping(actor->body->GetAngularDamping() + WATER_DAMPING);
      actor->body->SetLinearDamping(actor->body->GetLinearDamping() + WATER_DAMPING);
    }
  }
  world_update_actor_collision(actor);
}

static void world_body_add_collision_fixtures(body_t *body, b2FixtureDef fix, collision_t *collision) {
  if (!collision)
    return;
  uint16_t categoryBits = fix.filter.categoryBits;
  bool sensor = fix.isSensor;
  while (collision->type != COLL_END) {
    fix.isSensor = sensor || (collision->flags & CF_SENSOR) != 0;
    fix.filter.categoryBits = (collision->flags & CF_INTERACTIVE) ? CB_INTERACTIVE : categoryBits;
    fix.userData.id = collision->id;
    switch (collision->type) {
    case COLL_AABB:
      {
        collision_aabb_t *c = (collision_aabb_t *) collision;
        b2PolygonShape quad;
        const b2Vec2 points[4] = {
          { c->x0, c->y0 },
          { c->x0, c->y1 },
          { c->x1, c->y1 },
          { c->x1, c->y0 },
        };
        quad.Set(points, COUNT_OF(points));
        fix.shape = &quad;
        body->CreateFixture(&fix);
        //debugf("%p aabb %f %f %f %f\n", collision, c->x0, c->y0, c->x1, c->y1);
        collision = (collision_t *) &c[1];
      }
      break;
    case COLL_CIRCLE:
      {
        collision_circle_t *c = (collision_circle_t *) collision;
        b2CircleShape shape;
        shape.m_radius = c->r;
        shape.m_p.x = c->x0;
        shape.m_p.y = c->y0;
        fix.shape = &shape;
        body->CreateFixture(&fix);
        collision = (collision_t *) &c[1];
        //debugf("circle %f %f %f\n", c->r, c->x0, c->y0);
      }
      break;
    case COLL_TRIANGLE:
      {
        collision_tri_t *c = (collision_tri_t *) collision;
        b2PolygonShape tri;
        const b2Vec2 points[3] = {
          { c->x0, c->y0 },
          { c->x1, c->y1 },
          { c->x2, c->y2 },
        };
        //debugf("triangle %f %f %f %f %f %f\n", c->x0, c->y0, c->x1, c->y1, c->x2, c->y2);
        tri.Set(points, COUNT_OF(points));
        fix.shape = &tri;
        body->CreateFixture(&fix);
        collision = (collision_t *) &c[1];
      }
      break;
    case COLL_QUAD:
      {
        collision_quad_t *c = (collision_quad_t *) collision;
        b2PolygonShape quad;
        const b2Vec2 points[4] = {
          { c->x0, c->y0 },
          { c->x1, c->y1 },
          { c->x2, c->y2 },
          { c->x3, c->y3 },
        };
        //debugf("quad %f %f %f %f %f %f %f %f\n", c->x0, c->y0, c->x1, c->y1, c->x2, c->y2, c->x3, c->y3);
        quad.Set(points, COUNT_OF(points));
        fix.shape = &quad;
        body->CreateFixture(&fix);
        collision = (collision_t *) &c[1];
      }
      break;
    case COLL_POLY:
      {
        collision_poly_t *c = (collision_poly_t *) collision;
        assert(c->count >= 3);
        b2ChainShape poly;
        poly.m_count = c->count + 1;
        poly.m_vertices = (b2Vec2*) b2Alloc(poly.m_count * sizeof(b2Vec2));
        //debugf("poly(%lu)", c->count);
        for (size_t i = 0; i < c->count; i++) {
          //debugf(" [%.3f %.3f]", c->verts[i][0], c->verts[i][1]);
          poly.m_vertices[i].x = c->verts[i][0];
          poly.m_vertices[i].y = c->verts[i][1];
        }
        //debugf("\n");
        poly.m_vertices[c->count] = poly.m_vertices[0];
        poly.m_prevVertex = poly.m_vertices[poly.m_count - 2];
        poly.m_nextVertex = poly.m_vertices[1];
        fix.shape = &poly;
        body->CreateFixture(&fix);
        collision = (collision_t *) (((uint8_t *) c) + sizeof *c + sizeof(float[2]) * (size_t) c->count);
      }
      break;
    case COLL_EDGE:
      {
        collision_edge_t *c = (collision_edge_t *) collision;
        b2EdgeShape edge;
        edge.SetTwoSided(b2Vec2(c->x0, c->y0), b2Vec2(c->x1, c->y1));
        //debugf("edge %f %f %f %f\n", c->x0, c->y0, c->x1, c->y1);
        fix.shape = &edge;
        body->CreateFixture(&fix);
        collision = (collision_t *) &c[1];
      }
      break;
    case COLL_CHAIN:
      {
        collision_chain_t *c = (collision_chain_t *) collision;
        assert(c->count >= 2);
        b2ChainShape chain;
        chain.m_count = c->count;
        chain.m_vertices = (b2Vec2*) b2Alloc(chain.m_count * sizeof(b2Vec2));
        chain.m_prevVertex = { c->px, c->py };
        chain.m_nextVertex = { c->nx, c->ny };
        //debugf("chain(%lu)", c->count);
        for (size_t i = 0; i < c->count; i++) {
          //debugf(" %f %f", c->verts[i][0], c->verts[i][1]);
          chain.m_vertices[i].x = c->verts[i][0];
          chain.m_vertices[i].y = c->verts[i][1];
        }
        //debugf("\n");
        fix.shape = &chain;
        body->CreateFixture(&fix);
        collision = (collision_t *) (((uint8_t *) c) + sizeof *c + sizeof(float[2]) * (size_t) c->count);
      }
      break;
    default:
      assertf(0, "unknown collision type %" PRIu16, collision->type);
      break;
    }
  }
  //debugf("end\n");
}

world_position_t _world_get_actor_position(actor_t *actor) {
  world_position_u u;
  const auto vec = actor->body->GetPosition();
  u.x = vec.x * INV_POINT_SCALE;
  u.y = vec.y * INV_POINT_SCALE;
  return u.i;
}

world_position_t _world_get_actor_center(actor_t *actor) {
  world_position_u u;
  const auto vec = actor->body->GetWorldCenter();
  u.x = vec.x * INV_POINT_SCALE;
  u.y = vec.y * INV_POINT_SCALE;
  return u.i;
}

float world_get_actor_angle(actor_t *actor) {
  return actor->body->GetAngle();
}

void world_set_actor_position(actor_t *actor, float x, float y) {
  actor->body->SetTransform(POINT_SCALE * b2Vec2(x, y), actor->body->GetAngle());
}

void world_set_actor_position_centered(actor_t *actor, float x, float y) {
  auto body = actor->body;
  body->SetTransform(POINT_SCALE * b2Vec2(x, y) + body->GetLocalCenter(), body->GetAngle());
}

void world_set_actor_angle(actor_t *actor, float angle) {
  actor->body->SetTransform(actor->body->GetPosition(), angle);
}

void world_set_actor_transform(actor_t *actor, float x, float y, float angle) {
  actor->body->SetTransform(POINT_SCALE * b2Vec2(x, y), angle);
}

void world_update_actor_state(actor_t *actor) {
  uint32_t flags = actor->flags;
  b2Body *body = actor->body;
  b2Filter filter;

  filter.categoryBits = (flags & AF_NOCOLLIDE) ? 0 : actor->cls->category_bits;
  filter.maskBits = actor->cls->category_mask;
  if (!filter.maskBits)
    filter.maskBits = CB_ALL;
  body->SetType(
      (flags & AF_KINEMATIC)
      ? b2_kinematicBody
      : ((flags & AF_STATIC) ? b2_staticBody : b2_dynamicBody));
  body->SetGravityScale((flags & AF_GRAVITY) ? 1.f : 0.f);
  body->SetFixedRotation(!(flags & AF_ROTATES));
  for (b2Fixture *fixture = body->GetFixtureList(); fixture; fixture = fixture->GetNext()) {
    fixture->SetSensor(!(flags & AF_SOLID));
    fixture->SetFilterData(filter);
  }
}

void world_update_actor_collision(actor_t *actor) {
  {
    b2Fixture *fixture, *next;
    for (fixture = actor->body->GetFixtureList(); fixture; fixture = next) {
      next = fixture->GetNext();
      actor->body->DestroyFixture(fixture);
    }
  }

  b2FixtureDef fix;

  fix.density = actor->cls->density;
  fix.isSensor = !(actor->flags & AF_SOLID);
  fix.filter.categoryBits = (actor->flags & AF_NOCOLLIDE) ? 0 : actor->cls->category_bits;
  fix.filter.maskBits = actor->cls->category_mask;
  if (!fix.filter.maskBits)
    fix.filter.maskBits = CB_ALL;

  world_body_add_collision_fixtures(actor->body, fix, actor->collision);
}

void world_move_water(world_t *world, float y) {
  if (world->water)
    world->water->SetTransform(b2Vec2(0.f, y * POINT_SCALE + (WATER_BOX_SIZE*0.5f)), 0.f);
}

void world_set_gravity(world_t *world, float gravity_x, float gravity_y) {
  auto gravity = POINT_SCALE * b2Vec2(gravity_x, gravity_y);
  world->world.SetGravity(gravity);
  gravity.Normalize();
  world->map->gravity_norm_x = gravity.x;
  world->map->gravity_norm_y = gravity.y;
}

map_t *world_body_get_map(body_t *body) {
  return containerof(body->GetWorld(), world_t, world)->map;
}

void world_body_destroy(body_t *body) {
  body->GetWorld()->DestroyBody(body);
  auto userdata = body->GetUserData();
  if (userdata.type == BODY_ACTOR)
    userdata.actor->body = nullptr;
}

fixture_t *contact_get_other_fixture(contact_t *contact, body_t *body) {
  auto fixA = contact->GetFixtureA();
  return fixA->GetBody() == body ? contact->GetFixtureB() : fixA;
}

#ifndef NDEBUG
struct DebugDrawer : public b2Draw {
  virtual void DrawPolygon(const b2Vec2* vertices, int32 vertexCount, const b2Color& color) override;
  virtual void DrawSolidPolygon(const b2Vec2* vertices, int32 vertexCount, const b2Color& color) override;
  virtual void DrawCircle(const b2Vec2& center, float radius, const b2Color& color) override;
  virtual void DrawSolidCircle(const b2Vec2& center, float radius, const b2Vec2& axis, const b2Color& color) override;
  virtual void DrawSegment(const b2Vec2& p1, const b2Vec2& p2, const b2Color& color) override;
  virtual void DrawTransform(const b2Transform& xf) override;
  virtual void DrawPoint(const b2Vec2& p, float size, const b2Color& color) override;
} debug_drawer;

static void world_set_debug_draw(world_t *world) {
  world->world.SetDebugDraw(&debug_drawer);
}

void world_debug_draw(world_t *world) {
  debug_drawer.SetFlags(b2Draw::e_shapeBit);
  gl_context_begin();
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_LIGHTING);
  glPushMatrix();
  glLoadIdentity();
  glTranslatef(-world->map->camera_x + screen_half_width, -world->map->camera_y + screen_half_height, 0);
  glScalef(INV_POINT_SCALE, INV_POINT_SCALE, 1.f);
  world->world.DebugDraw();
  glPopMatrix();
  glEnable(GL_LIGHTING);
  glEnable(GL_DEPTH_TEST);
  gl_context_end();
}

void DebugDrawer::DrawPolygon(const b2Vec2* vertices, int32 vertexCount, const b2Color& color) {
  glBegin(GL_LINE_LOOP);
  glColor4f(color.r, color.g, color.b, color.a);
  for (int i = 0; i < vertexCount; i++) {
    glVertex2f(vertices[i].x, vertices[i].y);
  }
  glEnd();
}
void DebugDrawer::DrawSolidPolygon(const b2Vec2* vertices, int32 vertexCount, const b2Color& color) {
  DrawPolygon(vertices, vertexCount, color);
}
#define CIRC_SEGS 32
#define CIRC_STEP (M_PI*2.f*(1.0/(float)CIRC_SEGS))
void DebugDrawer::DrawCircle(const b2Vec2& center, float radius, const b2Color& color) {
  glBegin(GL_LINE_LOOP);
  glColor4f(color.r, color.g, color.b, color.a);
  for (float a = 0; a < M_PI*2.f; a += CIRC_STEP) {
    float c = cosf(a);
    float s = sinf(a);
    glVertex2f(center.x + c * radius, center.y + s * radius);
  }
  glEnd();
}
void DebugDrawer::DrawSolidCircle(const b2Vec2& center, float radius, const b2Vec2& axis, const b2Color& color) {
  DrawCircle(center, radius, color);
}
void DebugDrawer::DrawSegment(const b2Vec2& p1, const b2Vec2& p2, const b2Color& color) {
  glBegin(GL_LINES);
  glColor4f(color.r, color.g, color.b, color.a);
  glVertex2f(p1.x, p1.y);
  glVertex2f(p2.x, p2.y);
  glEnd();
}
void DebugDrawer::DrawTransform(const b2Transform& xf) {
  glBegin(GL_LINES);
  glColor4f(1.f, 0.f, 0.f, 1.f);
  glVertex2f(xf.p.x, xf.p.y);
  glVertex2f(xf.p.x + xf.q.c * 6, xf.p.y + xf.q.s * 6);
  glEnd();
}
void DebugDrawer::DrawPoint(const b2Vec2& p, float size, const b2Color& color) {
  glPointSize(size);
  glBegin(GL_POINTS);
  glColor4f(color.r, color.g, color.b, color.a);
  glVertex2f(p.x, p.y);
  glEnd();
}

#endif
