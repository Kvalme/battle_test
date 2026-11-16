#pragma once

#include "Types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace sw::core
{

	struct DamageRequest;
	class World;

	/**
	 * @brief Identifies effects that must share one instance on a target.
	 *
	 * World::applyEffect() compares this key with the keys of active effects.
	 * Equal keys extend the existing effect's duration instead of adding a second instance.
	 * The type string must remain valid during comparison; normally it refers to IEffect::type.
	 */
	struct EffectStackKey
	{
		std::string_view type;
		EntityId target;
		std::optional<EntityId> source;

		/**
		 * @brief Compares all fields of two effect keys.
		 *
		 * World::applyEffect() calls this comparison when it checks for an existing effect.
		 *
		 * @param other Effect key to compare.
		 * @return True if type, target, and source are equal.
		 */
		bool operator==(const EffectStackKey& other) const = default;
	};

	/**
	 * @brief Defines a temporary behavior attached to a unit, such as poison.
	 *
	 * An action creates the effect and passes it to World::applyEffect().
	 * World stores the accepted effect on its target and calls it before the target acts.
	 * An effect can apply changes during activation or modify incoming damage.
	 * World deletes expired effects and effects on dead units after the current operation ends.
	 */
	class IEffect
	{
	public:
		/**
		 * @brief Initializes an effect before it is passed to the world.
		 *
		 * A derived effect calls this constructor when an action creates it. This call does not attach the
		 * effect to a unit.
		 *
		 * @param type Effect name used for lookup and the default stack key.
		 * @param source ID of the unit credited for this effect; it may later die.
		 * @param target ID of the unit that receives the effect.
		 * @param duration Number of target activations to keep the effect; nonpositive values are discarded
		 * by applyEffect().
		 */
		IEffect(std::string type, EntityId source, EntityId target, int duration) :
				type(std::move(type)),
				source(source),
				target(target),
				remaining(duration)
		{}

		/**
		 * @brief Destroys the effect and releases its resources.
		 *
		 * World destroys the effect after expiry or target death, once the operation ends.
		 * World::applyEffect() also destroys incoming effects that it discards or merges with an existing effect.
		 */
		virtual ~IEffect() = default;
		const std::string type;
		const EntityId source, target;
		int remaining;	///< Counts down on the target's activations.

		/**
		 * @brief Specifies which active effect a new application should refresh.
		 *
		 * World::applyEffect() calls this method before adding an effect.
		 * The default combines effects with the same type and target, regardless of source.
		 * Include source in an override to keep applications from different sources separate.
		 * A match keeps the existing instance and sets its duration to the larger of the two durations.
		 *
		 * @return The key used to compare this effect with an existing effect.
		 */
		virtual EffectStackKey stackKey() const
		{
			return {type, target, std::nullopt};
		}

		/**
		 * @brief Checks whether this effect needs more unit activations.
		 *
		 * World::finished() calls this method for active effects on living targets.
		 * The scheduler also uses it to exclude pending effects from cycle detection.
		 * Return false for a passive modifier that must not keep the simulation running by itself.
		 * Do not change state or use random numbers. The default returns true.
		 *
		 * @return True if the effect has useful work left; false if it alone does not require another activation.
		 */
		virtual bool hasPendingWork() const
		{
			return true;
		}

		/**
		 * @brief Describes effect state for detection of a repeated simulation cycle.
		 *
		 * The scheduler calls this method during cycle checks; applyEffect() also calls it.
		 * World already records type, source, target, and remaining.
		 * An override must encode all other state that affects future behavior.
		 * Include an implementation identifier if different effect classes use the same type.
		 * Do not change state. Return std::nullopt if behavior depends on absolute ticks or external state.
		 *
		 * @return A stable string for additional state, an empty string for none, or std::nullopt to disable
		 * cycle detection.
		 */
		virtual std::optional<std::string> cycleState() const
		{
			return std::nullopt;
		}

		/**
		 * @brief Applies changes that must happen when the effect is first attached.
		 *
		 * World::applyEffect() calls this method after it inserts a new effect.
		 * It does not call this method when it refreshes an existing effect.
		 * Use World operations for changes. The default does nothing.
		 * Exceptions propagate without rolling back changes.
		 *
		 * @param world Simulation containing the newly attached effect.
		 */
		virtual void onApply([[maybe_unused]] World& world) {}

		/**
		 * @brief Performs the effect work for one activation of its target.
		 *
		 * World::activateEffects() calls this method before the target's ability is selected.
		 * It calls effects in insertion order and stops if the target dies.
		 * Effects added during a callback first receive this call on the next activation.
		 * World reduces the initially active timers after all callbacks if the target is alive.
		 * Use World operations for changes.
		 * The default does nothing. Exceptions propagate without rolling back changes.
		 *
		 * @param world Simulation containing the target and its effects.
		 */
		virtual void onActivation([[maybe_unused]] World& world) {}

		/**
		 * @brief Calculates how this effect changes incoming damage.
		 *
		 * World::damage() calls this method on active target effects in insertion order.
		 * Do not change state. Return the adjusted amount; the default leaves it unchanged.
		 * World limits the final amount to the range from zero to the target's current HP.
		 *
		 * @param request Original damage request, including its original amount and tags.
		 * @param amount Damage amount after all preceding modifiers.
		 * @return Damage amount to pass to the next modifier, or to apply if this is the last modifier.
		 */
		virtual std::int64_t modifyDamage([[maybe_unused]] const DamageRequest& request, std::int64_t amount) const
		{
			return amount;
		}
	};

}  // namespace sw::core
