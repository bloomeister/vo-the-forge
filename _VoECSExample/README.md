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
