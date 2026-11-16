---
name: add-unit
description: Add a new unit type or a new unit mechanic (attack, ability, effect) to this battle simulation. Use when asked to add a unit like a mage, tower, raven, or any new ability, attack, status effect, or unit stat.
---

# Add a new unit to the battle simulation

This repository is a turn-based battle simulation in C++20 with CMake.
`src/Core` is the engine. `src/Features` holds concrete mechanics.
A unit is an XML template in `assets/units/` that lists abilities in priority order.

Read these files before you change anything:

- `SOLUTION.md` — overview and the rules that were chosen.
- `docs/02_RULES.md` — exact rules of existing mechanics.
- `docs/04_EXTENSION_GUIDE.md` — step-by-step guide with a full example.
- `src/Features/README.md` — existing factory keys and their parameters.

## Hard rules

- Do not change the scenario input format or the event output format. Existing commands
  (`CREATE_MAP`, `SPAWN_SWORDSMAN`, `SPAWN_HUNTER`, `MARCH`) and events must stay byte-identical.
- Do not edit `src/Core` for a new unit unless the mechanic is impossible without it
  (for example, area effects on map cells). If Core must change, stop and describe the needed
  Core change to the user first.
- Do not edit other units' XML or other mechanics' `.cpp` files.
- Do not put unit-specific names (`mage`, `spirit`, ...) into Core.
- Put all randomness through `ActivationContext::random` and targets through
  `ActivationContext::chooseTarget()`. Never use `rand()` or your own RNG.
- Change the world only inside `IAction::execute()` or effect callbacks, through `World` methods
  (`damage`, `applyEffect`, `moveAlong`, `abilityUsed`). `propose()` and `canAct()` must not change state.

## Step 1. Choose the scope

| Need | What to do |
| --- | --- |
| Only existing mechanics with other stats or order | XML only (Step 2) |
| New instant attack result | New `AttackAbility` subclass and action in Features, then XML |
| Damage over time or a damage modifier | New `IEffect`, an action that applies it, a factory, then XML |
| Decision logic that is not an attack | Own `IAbility` with `canAct()` and `propose()` |
| Effect on map cells (firewall) | Core change needed. Ask the user first |
| Air layer, size (`Footprint`), invulnerability, range modifier or melee ban (raven, tower) | Core supports them in `UnitTemplate`, but the XML loader does not read them. Needs a change in `src/IO/Config/UnitLoader.cpp`. Ask the user first |

Existing factory keys: `attack`, `rending`, `poison_arrows`, `move`.
Check `src/Features/README.md` for their parameters.

## Step 2. XML template

Create `assets/units/<unit>.xml`. CMake embeds every `*.xml` from that folder into the binary.
The glob uses `CONFIGURE_DEPENDS`, so a normal rebuild picks up a new file.
Ability order is priority: the first ability that returns `Ready` acts, the rest are skipped.
Put `move` last so the unit moves only when it has no target.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<unit name="mage">
    <properties>
        <property name="spirit" type="int" value="3"/>
    </properties>
    <abilities>
        <ability implementation="attack" name="ranged">
            <properties>
                <property name="damageStat" type="string" value="spirit"/>
                <property name="kind" type="string" value="ranged"/>
                <property name="minRange" type="int" value="2"/>
                <property name="maxRangeStat" type="string" value="range"/>
            </properties>
        </ability>
        <ability implementation="move"/>
    </abilities>
</unit>
```

Notes:

- `damageStat`, `chanceStat`, `totalDamageStat`, `maxRangeStat` are names of unit stats, not values.
- Built-in stats with defaults: `hp=10`, `agility=1`, `strength=1`, `range=3`. Other stats must be
  declared in `<properties>`.
- Abilities that must hit the same target after a failed chance roll share one `targetGroup`
  with identical target settings.
- `maxRange` and `maxRangeStat` are mutually exclusive. `chanceStat` values must be in 0..1000.
- Ability `name` values must be unique within one unit.
- Unknown parameters are rejected at load time. That is intended. Fix the XML, not the validator.

## Step 3. New mechanic in C++ (only if needed)

Create one file `src/Features/<Area>/<Mechanic>.cpp`. CMake picks it up through a glob,
so you do not need to edit `CMakeLists.txt`. Use `src/Features/Attacks/PoisonArrows.cpp` as the model:

1. Effect (if needed): derive from `core::IEffect`. Set `type`, `source`, `target`, and duration
   in target activations. Override `onApply`, `onActivation`, or `modifyDamage`.
   Override `stackKey()` to include `source` if each attacker keeps its own instance.
   Override `hasPendingWork()` to return `false` for a passive modifier.
2. Action: derive from `core::IAction`. In `execute()`, call `world.abilityUsed(...)` if the ability
   must appear in the output, then `world.damage(...)` or `world.applyEffect(...)`.
3. Ability: for attacks, derive from `AttackAbility` and implement `makeAction()`.
   Otherwise implement `core::IAbility::canAct()` and `propose()`.
   `canAct()` must not use RNG and must not hide real work.
4. Factory: register it in an anonymous-namespace `Registrar` in the same file:
   `core::AbilityManager::instance()->registerAbility("<key>", factory)`.
   Read parameters with `AbilityProperties`, call `rejectUnknown()`, throw `std::invalid_argument`
   for bad values.
5. `cycleState()`: return a string that encodes every private value that affects future behavior.
   Return an empty string only when the object has no such state (as the built-in attacks do).
   If you cannot encode the state, keep the default `std::nullopt`. It disables cycle detection.

## Step 4. Scenario command (only if the user asks)

By default a new unit is created in tests through `World::spawn()`. A new `SPAWN_<UNIT>` command
extends the input format, so add it only on explicit request:

1. Add `src/IO/Commands/Spawn<Unit>.hpp` with the same structure as `SpawnHunter.hpp`.
2. In `src/main.cpp`, add a `parser.add<...>` handler that maps command fields to stat overrides
   and calls `world.spawn(..., templates.at("<unit>"), ...)`.
3. Do not touch existing commands.

## Step 5. Tests

Add tests to `tests/FeatureTests.cpp` (call the new function from `runFeatureTests()`).
Use `ScriptedRandom` for chance rolls and `RecordingSink` for events. Create a `core::Scheduler`
next to the `World` and call `scheduler.step()` or `scheduler.run()`. Check at least:

- The template loads, and the ability order is as expected.
- Damage and target selection match the rules, including the fallback when the special fails.
- Effects: they appear on the target with the right source, tick the right number of times,
  expire, and are removed when the target dies. Refresh and several sources behave as designed.
- No target gives `NotApplicable`, so the next ability can act.
- Bad stat references, wrong types, and unknown parameters are rejected.

## Step 6. Verify

Run from the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
for i in 1 2 3 4; do ./build/sw_battle_test scenario_$i.txt > /tmp/after_$i.txt; done
```

Before your change, save the same outputs as `/tmp/before_$i.txt` and compare them with `diff`.
They must be identical: a new unit must not change existing battles.
If clang-format is installed, also run `cmake --build build --target check-format`.

## Step 7. Documentation

- Add the unit to the table in `src/Features/README.md`.
- For a new factory key, document its parameters in the same file.
- For a new rule (timing, stacking, interaction with other effects), add it to `docs/02_RULES.md`.

Finish with a short report to the user: files added, rules you chose where the request was unclear,
and test results.
