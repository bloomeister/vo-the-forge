# _VoECSExample (ECS + Flecs) — Step-by-step Guide

This sample demonstrates how to structure ECS gameplay (via **flecs**) and render lots of entities efficiently using **The Forge** (GPU instancing + per-frame buffers).

## 1) What this sample teaches

### ECS concepts (flecs) – quick start for beginners

- **Mindset:** Entities are IDs. Components are plain data. Systems are functions that run over entities with specific components.
- **Minimal loop:**
  ```cpp
  // 1) Make a world
  gECSWorld = ecs_init();

  // 2) Register components
  ECS_COMPONENT_DEFINE(gECSWorld, PositionComponent);
  ECS_COMPONENT_DEFINE(gECSWorld, MoveComponent);
  ECS_COMPONENT_DEFINE(gECSWorld, SpriteComponent);
  ECS_COMPONENT_DEFINE(gECSWorld, AvoidComponent);
  ECS_COMPONENT_DEFINE(gECSWorld, WorldBoundsComponent);

  // 3) Register systems
  ecs_system_desc_t moveDesc = {};
  moveDesc.callback = MoveSystem;
  moveDesc.query.terms[0].id = ecs_id(PositionComponent);
  moveDesc.query.terms[1].id = ecs_id(MoveComponent);
  ecs_system_init(gECSWorld, &moveDesc);

  // 4) Create entities (Position + Move + Sprite) and some with AvoidComponent
  // 5) Each frame: ecs_progress(world, deltaTime);
  ```
- **Macro / typedef cheat sheet (what these names mean):**
  ```cpp
  ecs_init()                // create a flecs world
  // From flecs.h: macro that registers a struct as a component in the world.
  #define ECS_COMPONENT_DEFINE(world, id_) { ecs_component_desc_t desc = {0}; ... }
  ecs_id(Type)              // returns the flecs id for a registered component type
  // From flecs.h: system descriptor (callback + query terms).
  typedef struct ecs_system_desc_t { ecs_entity_t entity; ecs_query_desc_t query; ecs_iter_action_t callback; ... } ecs_system_desc_t;
  ecs_system_init(...)      // creates the system from that descriptor
  // From flecs.h: query descriptor (array of terms + optional DSL expr).
  typedef struct ecs_query_desc_t { ecs_term_t terms[FLECS_TERM_COUNT_MAX]; const char* expr; ... } ecs_query_desc_t;
  ecs_query_init(...)       // creates the query object
  ecs_field(it, T, idx)     // inside a system, fetches the idx-th term as array of T
  ecs_progress(world, dt)   // runs all systems once with delta time dt
  ```
- **Components (data only):**
  ```cpp
  struct PositionComponent { float x, y; };
  struct MoveComponent     { float velx, vely; };
  struct SpriteComponent   { float colorR, colorG, colorB; int spriteIndex; float scale; };
  struct WorldBoundsComponent { float xMin, xMax, yMin, yMax; };
  struct AvoidComponent    { float distanceSq; };
  ```
- **Systems (behavior):**
  - `MoveSystem` integrates velocity and bounces at bounds.
  - `AvoidanceSystem` flips velocity and tints color when close to avoiders.
  Both fetch components via `ecs_field(it, Type, index)`.
- **Queries (what entities we gather for rendering):**
  ```cpp
  // Sprites: Position + Move + Sprite
  ecs_query_desc_t spriteQuery = {};
  spriteQuery.terms[0].id = ecs_id(PositionComponent);
  spriteQuery.terms[1].id = ecs_id(MoveComponent);
  spriteQuery.terms[2].id = ecs_id(SpriteComponent);
  gECSSpriteQuery = ecs_query_init(gECSWorld, &spriteQuery);

  // Avoiders: Position + Sprite + Avoid
  ecs_query_desc_t avoidQuery = {};
  avoidQuery.terms[0].id = ecs_id(PositionComponent);
  avoidQuery.terms[1].id = ecs_id(SpriteComponent);
  avoidQuery.terms[2].id = ecs_id(AvoidComponent);
  gECSAvoidQuery = ecs_query_init(gECSWorld, &avoidQuery);
  ```
- **Multithreading:** Toggle at runtime with `ecs_set_threads(gECSWorld, gMultiThread ? gAvailableCores : 1);`.

### Rendering concepts (The Forge)
1. **ECS → packed instance data**  
   After `ecs_progress`, we iterate queries and fill `gSpriteData[]` (contiguous array used for instancing).

2. **Per-frame GPU buffers**  
   `gDataBufferCount = 2` frames in flight; update `pSpriteVertexBuffers[gFrameIndex]` each frame.

3. **Instanced draw**  
   One draw call: `cmdDrawIndexedInstanced(..., instanceCount = gDrawSpriteCount, ...)`.

### Rendering concepts (The Forge)
1. **ECS → packed instance data**
   - Each frame, ECS is progressed, then entities are iterated and written to `gSpriteData[]` (contiguous array).

2. **Per-frame GPU buffers (frames-in-flight)**
   - `gDataBufferCount = 2`.
   - `pSpriteVertexBuffers[gFrameIndex]` is updated each frame with just the data needed for rendering.

3. **Instanced drawing**
   - The draw call uses `cmdDrawIndexedInstanced(..., instanceCount = gDrawSpriteCount, ...)` so many entities render in one call.

## 2) How the frame works (high level)

1. `Init()`
   - GPU config + renderer setup:
     - `initGPUConfiguration(...)`
     - `initRenderer(...)`
     - `setupGPUConfigurationPlatformParameters(...)`
   - Create GPU resources (buffers, texture, pipeline).
   - Create ECS world and register components/systems/queries.
   - Create entities (sprites and avoiders).

2. `Update(deltaTime)`
   - Updates simulation:
     - `ecs_progress(gECSWorld, deltaTime * 3.0f);`
   - Builds render instance data into `gSpriteData[]` by iterating:
     - `gECSSpriteQuery`
     - `gECSAvoidQuery`
   - Updates `gDrawSpriteCount`.

3. `Draw()`
   - Acquire swapchain image.
   - Upload `gSpriteData` into `pSpriteVertexBuffers[gFrameIndex]`.
   - Record and submit GPU commands.
   - Issue a single instanced draw for all sprites.

4. `Exit()`
   - Destroy ECS queries/world.
   - Release renderer resources.
   - Call `exitGPUConfiguration()` (required to avoid config parsing leaks).

## 3) Build & run (Visual Studio)

1. Open the solution in Visual Studio.
2. Set startup project to `_VoECSExample`.
3. Build: `Build > Build Solution`.
4. Run: `Debug > Start Debugging (F5)`.

If assets aren't found, ensure resource dirs point to Art/ and CompiledShaders/ (see `fsSetPathForResourceDir(...)` in `Init()`).

## 4) ECS walk-through (current code)

- **Components:** defined at the top of `_VoECSExample.cpp` (`PositionComponent`, `MoveComponent`, `SpriteComponent`, `AvoidComponent`, `WorldBoundsComponent`).
- **Systems:**  
  - `MoveSystem` integrates velocity, bounces at bounds.  
  - `AvoidanceSystem` checks distances against avoiders, flips velocity and tints color on collision.
- **Queries:**  
  - `gECSSpriteQuery` → Position + Move + Sprite.  
  - `gECSAvoidQuery` → Position + Sprite + Avoid.  
  Field order matches how we call `ecs_field` in `Update()`.
- **Per-frame:** `ecs_progress` runs systems; then we fill `gSpriteData` from both queries for instanced rendering.

## 5) Debugging tips

- If you change query layouts, update the `ecs_field(..., index)` calls to match the term order.
- Use the UI “Threading” checkbox to force single-threading if you suspect race conditions.
- Keep `gDrawSpriteCount <= gMaxSpriteCount` (already asserted in `Draw()`).

## 6) Suggested exercises

1. Add a `RotationComponent` and rotate sprites over time.  
2. Steer instead of bouncing in `AvoidanceSystem`.  
3. Increase `gSpriteEntityCount` and profile CPU vs GPU time.

## 7) Key code pointers

- ECS setup: `EntityComponentSystem::Init()`
- Simulation: `MoveSystem`, `AvoidanceSystem`, `EntityComponentSystem::Update()`
- Rendering: `EntityComponentSystem::Draw()`
- Shutdown: `EntityComponentSystem::Exit()`

## 8) Folder organization & ECS layout for larger games (e.g., ARPG/MMO)

Here’s a pragmatic way to keep ECS code tidy as a project grows (Diablo II / WoW style):

- **Folders**
  - `ECS/Components/` – POD structs only (e.g., `Transform.h`, `Stats.h`, `Inventory.h`, `Ability.h`, `Buff.h`, `Faction.h`, `Quest.h`, `Spawn.h`, `Path.h`, `NetSync.h`).
  - `ECS/Systems/` – logic per domain (e.g., `MovementSystem.cpp`, `CombatSystem.cpp`, `LootSystem.cpp`, `AIBehaviorSystem.cpp`, `BuffSystem.cpp`, `QuestSystem.cpp`, `NetReplicationSystem.cpp`).
  - `ECS/Archetypes/` – helper factories for entity types (player, mob, npc, projectile, item, portal, map trigger).
  - `ECS/Queries/` – prebuilt queries used across systems (e.g., “targets in radius”, “projectiles needing collision”, “network-owned actors”).
  - `ECS/Data/` – static data tables (JSON/CSV) for abilities, items, monsters; loaded into components at spawn time.

- **Entity types (examples)**
  - Player/hero, NPC, Mob/elite/boss, Projectile, Item drop, Portal/trigger, Summon/pet, Environmental hazard.

- **Core components**
  - Spatial: `Transform`, `Velocity`, `PathFollow`, `NavAgent`.
  - Gameplay: `Stats` (hp/mana/armor/resist), `Faction`, `Threat`, `BuffList`, `AbilityLoadout`, `Cooldowns`, `ResourceRegen`.
  - Interaction: `Hitbox`, `Hurtbox`, `Projectile`, `LootTable`, `QuestGiver`, `QuestState`, `Vendor`.
  - AI: `BehaviorState`, `BlackboardRef`, `Perception` (vision/hearing), `Leash`.
  - Networking: `NetId`, `NetOwner`, `NetDirty`, `NetSnapshot`, `PredictionState`.
  - Presentation: `AnimationState`, `VFXRequest`, `SFXRequest`, `UIBillboard`.

- **Systems you’ll likely need**
  - Movement/Nav: path following, steering/avoidance, knockback.
  - Combat: target selection, hit detection, damage application, lifesteal/on-hit effects, death handling.
  - Buffs/Debuffs: apply/remove, tick over time, stat modification.
  - Abilities: cast time, cooldowns, resource costs, triggers (on hit/kill/timer), area effects.
  - Loot/XP: drop generation, pickup, XP/share, level-up stat growth.
  - AI: perception → decision (behavior tree/utility) → action selection; leashing and threat management.
  - Quests/Triggers: area enters, kills, item pickups; state replication to UI.
  - Networking: ownership, delta/snapshot sync, prediction/reconciliation for movement and abilities.
  - Animation/VFX/SFX: drive presentation from gameplay state, queue events.

- **Practical tips**
  - Keep components small and focused; split “heavy” state into separate components.
  - Use archetype/factory helpers to spawn consistent bundles (e.g., `CreateMob(typeId, spawnPos)` attaches the right components from data tables).
  - Centralize queries you reuse (e.g., “enemies in radius”, “net-owned actors”) to avoid drift in term order.
  - Be consistent with term order and `ecs_field` indices; when you change queries, update field access together.
  - For multiplayer, isolate net-sync state into explicit components and run net systems in a predictable phase.

## 9) Entities in this sample (with code)

This demo spawns two archetypes:

- **Sprite entities** (have `PositionComponent`, `MoveComponent`, `SpriteComponent`)
  ```cpp
  // Creation in EntityComponentSystem::Init()
  ecs_entity_t entityId = ecs_new(gECSWorld);
  PositionComponent position = { x, y };
  MoveComponent     move     = createMoveComponent(0.3f, 0.6f);
  SpriteComponent   sprite   = { /*color*/1,1,1, /*spriteIndex*/randomInt(0,5), /*scale*/0.5f };

  ecs_set(gECSWorld, entityId, PositionComponent, position);
  ecs_set(gECSWorld, entityId, MoveComponent, move);
  ecs_set(gECSWorld, entityId, SpriteComponent, sprite);
  ```

- **Avoid entities** (add `AvoidComponent` so others bounce off them)
  ```cpp
  ecs_entity_t entityId = ecs_new(gECSWorld);
  PositionComponent position = { x * 0.2f, y * 0.2f };
  MoveComponent     move     = createMoveComponent(0.3f, 0.6f);
  SpriteComponent   sprite   = { randomFloat(0.5f,1.0f), randomFloat(0.5f,1.0f),
                                randomFloat(0.5f,1.0f), /*spriteIndex*/5, /*scale*/1.0f };
  AvoidComponent    avoid    = { 1.3f * 1.3f };

  ecs_set(gECSWorld, entityId, PositionComponent, position);
  ecs_set(gECSWorld, entityId, MoveComponent, move);
  ecs_set(gECSWorld, entityId, SpriteComponent, sprite);
  ecs_set(gECSWorld, entityId, AvoidComponent, avoid);
  ```

Related code:
- Component structs: `PositionComponent`, `MoveComponent`, `SpriteComponent`, `AvoidComponent`.
- Queries: `gECSSpriteQuery` selects Position + Move + Sprite; `gECSAvoidQuery` selects Position + Sprite + Avoid.
- Systems: `MoveSystem` integrates position; `AvoidanceSystem` reacts to avoiders.

**FAQ: How do you “refer” to entities if they’re just IDs?**  
Entities are IDs, but their meaning comes from the components they carry. A sprite entity and an avoid entity are both just IDs; what makes them different is that avoid entities *also* have `AvoidComponent`, so queries/systems can select them separately. You generally don’t hard-code IDs—use queries to find the sets you need (e.g., `gECSSpriteQuery` vs `gECSAvoidQuery`) or store an `ecs_entity_t` handle if you must keep a reference. The ID value (0/1/etc.) isn’t used to distinguish types; the attached components are.

Example of “referring” to entities via queries:
```cpp
// Iterate sprites (Position + Move + Sprite)
ecs_iter_t it = ecs_query_iter(gECSWorld, gECSSpriteQuery);
while (ecs_query_next(&it)) {
    // This batch gives you the entities and their components:
    const ecs_entity_t* ents = it.entities;          // array of IDs
    PositionComponent* pos   = ecs_field(&it, PositionComponent, 1);
    MoveComponent*     mv    = ecs_field(&it, MoveComponent, 2);
    SpriteComponent*   spr   = ecs_field(&it, SpriteComponent, 3);
    // work with the batch...
}

// If you need to keep a handle:
ecs_entity_t e = ecs_new(gECSWorld);
// ... later, you can add/remove/query components on 'e'
```

**Performance note (ECS vs OOP):**  
You don’t “while-loop per entity” to hunt one object; ECS is fast because queries return contiguous batches of components, and systems process many entities in one cache-friendly pass. Per-entity random access is discouraged—prefer queries/iterators for bulk work. When you must touch one entity, keep its `ecs_entity_t` handle and use `ecs_has/ecs_get/ecs_add/ecs_remove` directly; otherwise rely on systems + queries for the hot paths.

**Example: read characters’ positions by handle**
```cpp
// Suppose you stored the entity ids when you spawned characters:
ecs_entity_t characterA = ecs_new(gECSWorld);
ecs_entity_t characterB = ecs_new(gECSWorld);
ecs_set(gECSWorld, characterA, PositionComponent, { 0.0f, 0.0f });
ecs_set(gECSWorld, characterB, PositionComponent, { 5.0f, 2.0f });
// ... later, to read or update:
PositionComponent* posA = ecs_get_mut(gECSWorld, characterA, PositionComponent);
PositionComponent* posB = ecs_get_mut(gECSWorld, characterB, PositionComponent);
if (posA && posB) {
    float ax = posA->x, ay = posA->y;
    float bx = posB->x, by = posB->y;
    // update or read as needed
}
```
Prefer queries for bulk work, but for one-off lookups you can use the handle directly as above.

**What does “mut” mean? Is there an immutable version?**  
- The “mut” suffix stands for “mutable.” `ecs_get_mut` returns a writable pointer (and adds the component if missing).  
- For read-only access, use `ecs_get(world, entity, Component)` and bind to `const Component*`.  
- For stable read-only pointers across ticks, use refs: `ecs_ref_t ref = ecs_ref_init(world, entity, ecs_id(Component));` then `const Component* c = ecs_ref_get(world, &ref, Component);`. There is no `ecs_get_imt`; `ecs_get`/`ecs_ref_get` are the read-only paths.

**Everything is “just an entity”**  
An entity is only an ID. Its “type” comes from the components attached to that ID. Two entities are considered the same “kind” because they share the same component set, not because they share a class. Systems/queries group and process entities by matching component signatures.

**Example: “This skill affects all animals”**  
Give “animal” a tag component and query for it when applying the skill:
```cpp
// Declare a tag (no data)
struct AnimalTag { };
ECS_COMPONENT_DEFINE(gECSWorld, AnimalTag);

// When spawning an animal entity:
ecs_entity_t wolf = ecs_new(gECSWorld);
ecs_set(gECSWorld, wolf, PositionComponent, { /*...*/ });
ecs_set(gECSWorld, wolf, SpriteComponent,  { /*...*/ });
ecs_add(gECSWorld, wolf, AnimalTag); // mark as animal

// Skill system/query: select entities with AnimalTag
ecs_query_desc_t skillQuery = {};
skillQuery.terms[0].id = ecs_id(AnimalTag);
gSkillQuery = ecs_query_init(gECSWorld, &skillQuery);

// Apply skill to all animals
ecs_iter_t it = ecs_query_iter(gECSWorld, gSkillQuery);
while (ecs_query_next(&it)) {
    const ecs_entity_t* ents = it.entities;
    // apply effect to all animals in this batch
}
```
By tagging entities (AnimalTag, UndeadTag, BossTag, etc.) you can target groups with queries rather than hard-coding IDs.

**How do I refer to a specific component or system?**  
- Components: use `ecs_id(ComponentType)` as the handle, and `ecs_has/ecs_get/ecs_get_mut/ecs_add/ecs_remove` to check/read/write on any entity.  
- Systems: `ecs_system_init` returns an `ecs_entity_t` for the system; store it if you want to enable/disable or tweak it later:
  ```cpp
  ecs_system_desc_t desc = { .callback = MoveSystem };
  desc.query.terms[0].id = ecs_id(PositionComponent);
  desc.query.terms[1].id = ecs_id(MoveComponent);
  ecs_entity_t moveSys = ecs_system_init(gECSWorld, &desc);

  // Later: disable/enable by id, or look up by name with ecs_lookup(world, "MoveSystem")
  ecs_enable(gECSWorld, moveSys, false); // disable
  ecs_enable(gECSWorld, moveSys, true);  // enable
  ```

**Are components/systems entities too?**  
Yes. Flecs represents components, systems, and tags as entities under the hood. You normally access them via helpers (`ecs_id(ComponentType)`, the return value from `ecs_system_init`), but they all have an entity id in the world.

**Analogy:** It’s a bit like JavaScript’s “everything is an object” mindset: in Flecs “everything is an entity.” The differentiation comes from which components (data) are attached, not from distinct class types.

## 10) If you did this with OOP classes (and why ECS instead)

You could build this sample with a `class Entity { Position pos; Velocity vel; Sprite sprite; ... }` and subclass for avoiders. That works for small projects, but:

- **OOP pros:** Familiar; per-object methods; easy to encapsulate behavior.
- **OOP cons:** Data for many objects scatters in memory (poor cache use); inheritance hierarchies get brittle; behavior tied to classes makes bulk operations harder.
- **ECS pros:** Data-oriented (arrays of components → cache-friendly); systems operate on batches; composition over inheritance (just add/remove components); easy to query “all X with Y but not Z.”
- **ECS cons:** You think in terms of data + queries (less familiar if you come from OOP); per-entity, one-off access can feel indirect; requires discipline to keep queries/fields in sync.

For a few objects, OOP is fine. For thousands of similar entities updated every frame (like this sprite swarm), ECS tends to be faster and simpler to extend (e.g., add a tag to include/exclude entities from a system).

## 5) Debugging tips

### Multithreading
- Toggle “Threading” in the UI to switch between single-thread and multi-thread ECS.
- If you suspect a race, force single-thread:
  - Set `static bool gMultiThread = false;`

### Validate entity counts
- Sprite entities: `gSpriteEntityCount`
- Avoid entities: `gAvoidEntityCount`
- Total capacity: `gMaxSpriteCount`
- Ensure `gDrawSpriteCount <= gMaxSpriteCount` (already asserted in `Draw()`).

### Leak checks
If you hit a memory leak assert on exit, ensure `Exit()` calls:
- `exitGPUConfiguration()` (GPU config parsing allocations)
- and that all GPU resources created in `Init()`/`Load()` are released in `Exit()`/`Unload()`.

## 6) Suggested exercises

1. Add a new component (e.g. `RotationComponent`) and update instance data to include it.
2. Add a new system that changes sprite scale over time.
3. Extend `AvoidanceSystem` to steer instead of flip velocity.
4. Increase `gSpriteEntityCount` and profile CPU vs GPU time (use the built-in profiler UI).

## 7) Key code pointers

- ECS setup: `EntityComponentSystem::Init()`
- Simulation: `MoveSystem`, `AvoidanceSystem`, `EntityComponentSystem::Update()`
- Rendering: `EntityComponentSystem::Draw()`
- Shutdown: `EntityComponentSystem::Exit()`

## 8) Next steps and optimization tips

1. **Explore ECS design patterns**: Look into more complex patterns like event-driven systems, state machines, etc.

2. **Profile and optimize**: Use profiling tools to find bottlenecks. Optimize systems, queries, and rendering as needed.

3. **Experiment with The Forge features**: Try out different rendering techniques, GPU culling, LODs, etc.

4. **Look into ECS best practices**: Such as data-oriented design, cache-friendly layouts, etc.

5. **Consider external libraries**: For math, physics, etc., to complement the ECS and rendering system.

Happy coding! Enjoy building your ECS-powered applications with flecs and The Forge.

---

*This guide is a high-level overview. Refer to the actual source code and comments for detailed implementation insights.*
