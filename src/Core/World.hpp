#pragma once

#include "Events.hpp"
#include "Properties.hpp"
#include "Types.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sw::core
{

	struct DamageRequest;
	struct Entity;
	struct TargetQuery;
	struct UnitTemplate;
	class IEffect;
	class IRandom;

	/**
	 * @brief Stores the simulation and controls how units, actions, and effects change it.
	 *
	 * Application code creates the map and units, then drives the simulation with a Scheduler.
	 * World stores units in spawn order and activates one unit when Scheduler asks for it.
	 * It processes effects before abilities.
	 * Features inspect entities through const references and change them through World methods.
	 * World stores entities until destruction, including dead entities.
	 * The event sink is supplied by the application and must remain alive.
	 * Use one call at a time.
	 * Callback exceptions propagate without rollback; stop using the simulation after such a failure.
	 */
	class World
	{
	public:
		/// Maximum size in bytes of one snapshot() result. A larger state has no snapshot.
		static constexpr std::size_t maxSnapshotBytes = 1'048'576;

		/**
		 * @brief Creates an empty simulation.
		 *
		 * Application code calls this constructor before creating a map or units.
		 *
		 * @param events Event receiver. It must remain alive until this World is destroyed.
		 */
		explicit World(IEventSink& events);

		/**
		 * @brief Transfers a simulation into a new World object.
		 *
		 * Application code can call this constructor when it transfers storage of a World.
		 * The new object uses the same external event sink.
		 * Do not continue the simulation through the old object.
		 *
		 * @param other World whose stored simulation is transferred.
		 */
		World(World&& other) noexcept;

		/**
		 * @brief Destroys all stored units, abilities, and effects.
		 *
		 * Application code destroys the World when it no longer needs the simulation. External dependencies
		 * are not destroyed.
		 */
		~World();

		/**
		 * @brief Sets map dimensions and emits a MapCreated event.
		 *
		 * Application command handling calls this method before spawning units or when resizing the map.
		 * The new map must contain every living unit. Zero dimensions are allowed if no living units exist.
		 *
		 * @param width Map width in cells.
		 * @param height Map height in cells.
		 * @throws std::invalid_argument If a living unit would be outside the map.
		 */
		void createMap(std::uint32_t width, std::uint32_t height);

		/**
		 * @brief Creates a unit and its abilities, then emits a Spawned event.
		 *
		 * Application command handling calls this method to add a unit.
		 * Overrides replace existing properties and must preserve their types.
		 * Ability factories validate the final properties. The unit is inserted only after construction succeeds.
		 * The hp property initializes Entity::hp and is removed from Entity::properties.
		 * Factory errors propagate to the caller.
		 *
		 * @param id Unique unit ID. An ID cannot be reused after its unit dies.
		 * @param position Top-left cell of the unit footprint.
		 * @param unitTemplate Configuration copied to create the unit and its abilities.
		 * @param overrides Replacement values for properties defined by defaults or the template.
		 * @throws std::invalid_argument If the ID, position, properties, or ability configuration are invalid.
		 */
		void spawn(EntityId id, Position position, const UnitTemplate& unitTemplate, const PropertyMap& overrides = {});

		/**
		 * @brief Assigns a movement destination and emits a MarchStarted event.
		 *
		 * Application command handling calls this method for a march order.
		 * This method does not move the unit. A movement ability performs the later steps.
		 * Unknown or dead units are ignored. Occupied destinations are allowed:
		 * the unit approaches the closest reachable cell and waits there while the march stays assigned.
		 * If the unit is already at the destination, the method also emits MarchEnded.
		 *
		 * @param id Unit that receives the order.
		 * @param destination Requested top-left cell of the unit footprint.
		 * @throws std::invalid_argument If the living unit would not fit at the destination.
		 */
		void march(EntityId id, Position destination);

		/**
		 * @brief Provides read-only access to a stored unit.
		 *
		 * Abilities, actions, and application code call this method when they need unit state.
		 * Dead units remain available. The reference stays valid until the World containing the unit is destroyed.
		 *
		 * @param id ID of the stored unit.
		 * @return A reference to the unit. Do not modify it or delete it.
		 * @throws std::out_of_range If the ID does not exist.
		 */
		const Entity& entity(EntityId id) const;

		/**
		 * @brief Checks whether an ID identifies a living unit.
		 *
		 * Features and Core call this method before work that requires a living unit.
		 *
		 * @param id Unit ID to check.
		 * @return True if the unit exists and has positive HP; false otherwise.
		 */
		bool isAlive(EntityId id) const;

		/**
		 * @brief Finds all units that match an ability target query.
		 *
		 * Abilities call this method when they check applicability or select a target.
		 * It excludes the actor, dead units, and targets rejected by TargetQuery and TargetProfile.
		 * It does not use random numbers.
		 *
		 * @param actor ID of the unit searching for targets.
		 * @param query Range and eligibility conditions to apply.
		 * @return Matching IDs in ascending order, or an empty vector for no matches or a dead actor.
		 * @throws std::out_of_range If actor does not exist.
		 */
		std::vector<EntityId> targets(EntityId actor, const TargetQuery& query) const;

		/**
		 * @brief Finds the next steps toward the assigned march destination.
		 *
		 * Movement abilities call this method while preparing an action.
		 * It finds a shortest route using eight neighboring cells and the unit's full footprint.
		 * If the destination is occupied or unreachable, the route leads to the reachable cell closest
		 * to it (Chebyshev distance), but only if that cell is closer than the current position.
		 * The returned path excludes the current position. The method does not move the unit.
		 *
		 * @param id Unit whose march route is requested.
		 * @param cells Maximum number of route steps to return.
		 * @return A route prefix, or an empty vector if the unit is dead, has no march order,
		 *  has reached its destination, cells is zero, or no reachable cell is closer to the destination.
		 * @throws std::out_of_range If id does not exist.
		 */
		std::vector<Position> movementPath(EntityId id, unsigned cells = 1) const;

		/**
		 * @brief Gets the last position in the requested movement path.
		 *
		 * Movement code can call this convenience method instead of inspecting movementPath().
		 * With cells greater than one, the result need not be adjacent to the unit.
		 *
		 * @param id Unit whose march route is requested.
		 * @param cells Maximum number of steps to look ahead.
		 * @return The last position returned by movementPath(), or std::nullopt for an empty path.
		 * @throws std::out_of_range If id does not exist.
		 */
		std::optional<Position> nextPosition(EntityId id, unsigned cells = 1) const;

		/**
		 * @brief Moves a unit directly to a valid position.
		 *
		 * An action calls this method when it needs direct relocation.
		 * It checks map bounds and occupied cells, but not adjacency or intermediate cells.
		 * Use moveAlong() for movement that must validate every step.
		 * Dead units and unavailable positions are ignored.
		 * It emits Moved if the position changes and MarchEnded if the march destination is reached.
		 *
		 * @param id Unit to move.
		 * @param position New top-left cell of the unit footprint.
		 * @throws std::out_of_range If id does not exist.
		 */
		void move(EntityId id, Position position);

		/**
		 * @brief Moves a unit along the valid part of a path.
		 *
		 * Movement actions call this method to execute their chosen route.
		 * It stops before the first invalid step or when the march destination is reached.
		 * Each step must be adjacent and available for the full footprint. Earlier valid steps remain applied.
		 * It emits one Moved event for the final position and MarchEnded if the destination is reached.
		 * An empty path or a dead unit causes no changes.
		 *
		 * @param id Unit to move.
		 * @param path Ordered positions to visit, excluding the starting position.
		 * @throws std::out_of_range If id does not exist.
		 */
		void moveAlong(EntityId id, std::span<const Position> path);

		/**
		 * @brief Emits an AbilityUsed event at the current tick.
		 *
		 * Feature actions call this method when their ability use must be visible in the output.
		 * It does not validate the unit ID or ability name.
		 *
		 * @param id Unit credited with using the ability.
		 * @param name Ability name to include in the event.
		 */
		void abilityUsed(EntityId id, const std::string& name);

		/**
		 * @brief Applies damage modifiers and reduces target HP.
		 *
		 * Actions and effects call this method to deal damage.
		 * It ignores nonpositive requests and dead or invulnerable targets.
		 * It limits modified damage to the target's remaining HP and changes HP immediately.
		 * It emits combined Attacked events and then Died events when the outer operation ends.
		 * The source is used for attribution and need not be alive.
		 *
		 * @param request Target, source, original damage amount, and modifier tags.
		 * @throws std::out_of_range If the target does not exist.
		 */
		void damage(DamageRequest request);

		/**
		 * @brief Attaches an effect or refreshes an existing effect with the same key.
		 *
		 * Actions and effect callbacks call this method to apply a status effect.
		 * It discards null effects, nonpositive durations, and effects on unknown or dead targets.
		 * For a matching stack key, it keeps the old instance and the larger duration.
		 * Otherwise, it stores the new instance and calls onApply() inside an operation.
		 *
		 * @param effect Effect to transfer to World. The caller must not use it after this call.
		 */
		void applyEffect(std::unique_ptr<IEffect> effect);

		/**
		 * @brief Checks for an active effect with a specified type.
		 *
		 * Abilities and effects call this method to test interactions with another effect.
		 * It does not distinguish sources or stack keys.
		 *
		 * @param id Target unit ID.
		 * @param type Effect type to find.
		 * @return True if the living unit has a matching positive-duration effect; false for no match or an
		 * unknown or dead unit.
		 */
		bool hasEffect(EntityId id, std::string_view type) const;

		/**
		 * @brief Processes one activation of the target effects.
		 *
		 * Activation calls this method before it considers the target's abilities.
		 * It calls onActivation() in insertion order and stops if the target dies.
		 * It then reduces the initially active timers if the target is alive.
		 * Dead targets are ignored. A direct call also consumes effect duration but does not advance tick().
		 *
		 * @param id Unit whose effects must be processed.
		 * @throws std::out_of_range If id does not exist.
		 */
		void activateEffects(EntityId id);

		/**
		 * @brief Starts a group of changes whose damage events are published together.
		 *
		 * Activation calls this before an action; effect and damage processing also use it.
		 * A Feature may call it to group several changes. Each call requires one endOperation().
		 * Nested operations are allowed. Only the outermost endOperation() publishes damage and death events.
		 * This mechanism does not roll back changes after an exception.
		 */
		void beginOperation();

		/**
		 * @brief Ends the current group of changes and publishes completed damage events.
		 *
		 * Code that called beginOperation() must call this method once for that call.
		 * The outermost call deletes expired effects and effects on dead units.
		 * It then emits one Attacked event per source/target pair with total damage and final HP,
		 * followed by death events. Other event kinds are not delayed by operations.
		 *
		 * @throws std::logic_error If there is no matching beginOperation().
		 */
		void endOperation();

		/**
		 * @brief Checks whether the simulation has no useful work left.
		 *
		 * Scheduler::run() calls this method before each activation. Application code may also inspect it.
		 * It calls ability canAct() and active effect hasPendingWork() for living units.
		 * It does not execute an activation or use random numbers.
		 *
		 * @return True if no ability or active effect reports useful work; false otherwise.
		 */
		bool finished() const;

		/**
		 * @brief Reads the number of stored units, including dead units.
		 *
		 * Scheduler calls this method to learn how many activation slots exist.
		 * The count grows when spawn() succeeds. It never shrinks.
		 *
		 * @return Number of stored units.
		 */
		std::size_t unitCount() const;

		/**
		 * @brief Reads the ID of a stored unit by its spawn order.
		 *
		 * Scheduler calls this method to walk units in spawn order.
		 * Dead units keep their slot.
		 *
		 * @param index Zero-based spawn position. It must be less than unitCount().
		 * @return ID of the unit at this position.
		 * @throws std::out_of_range If index is not less than unitCount().
		 */
		EntityId unitAt(std::size_t index) const;

		/**
		 * @brief Processes one activation of a unit.
		 *
		 * Scheduler calls this method for the unit that has the current turn.
		 * It advances tick(), applies effects, and selects at most one ability action.
		 * The action runs inside an operation. A unit killed by its effects does not act.
		 * It does not check whether the unit is alive before it starts. Callers skip dead units.
		 *
		 * @param id Unit to activate.
		 * @param random Random generator passed to abilities through ActivationContext.
		 * @throws std::out_of_range If id does not exist.
		 * @throws std::logic_error If a ready ability has no action.
		 */
		void activate(EntityId id, IRandom& random);

		/**
		 * @brief Encodes the complete simulation state for cycle detection.
		 *
		 * Scheduler::run() calls this method at round boundaries and after activations.
		 * Equal strings describe equal states. The encoding includes the map, units, marches,
		 * ability states, and effect states.
		 * There is no snapshot during an operation, with pending damage events, with an effect that has
		 * pending work, or when an ability or effect does not report its state.
		 * There is also no snapshot if the state is larger than maxSnapshotBytes.
		 *
		 * @return The encoded state, or std::nullopt if the state cannot be compared.
		 */
		std::optional<std::string> snapshot() const;

		/**
		 * @brief Reads and clears the flag that marks an effect as unsafe for cycle detection.
		 *
		 * Scheduler::run() calls this method to learn whether applyEffect() accepted an effect that
		 * has pending work or cannot describe its state since the last call.
		 * Such an effect makes later states impossible to compare.
		 *
		 * @return True if such an effect was applied since the previous call; false otherwise.
		 */
		bool takeCycleBlockedFlag();

		/**
		 * @brief Reads the number of unit activations started.
		 *
		 * Application code and Features can call this method when they need the current simulation time.
		 * An activation counts even if effects kill the unit before it can act.
		 * Map, spawn, and march commands do not advance this counter.
		 *
		 * @return Current activation counter, initially zero.
		 */
		std::uint64_t tick() const
		{
			return tick_;
		}

	private:
		Entity& mutableEntity(EntityId);
		bool free(Position, const Entity&) const;
		void finishMovement(Entity&, Position start);
		void emit(Event);
		void retireEffects();
		IEventSink& events_;
		std::uint32_t width_{}, height_{};
		std::vector<std::unique_ptr<Entity>> entities_;
		std::uint64_t tick_{};
		bool cycleBlockedByEffect_{};
		int operationDepth_{};
		std::vector<Event> damageEvents_;
		std::vector<EntityId> deaths_;
	};

}  // namespace sw::core
