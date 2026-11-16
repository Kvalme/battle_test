#pragma once

#include "Abilities.hpp"
#include "Effects.hpp"
#include "Footprint.hpp"
#include "Properties.hpp"
#include "Targeting.hpp"
#include "Types.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sw::core
{

	/**
	 * @brief Stores the current state, abilities, and effects of one unit.
	 *
	 * World creates this object during spawn() and keeps it until World is destroyed.
	 * Features receive const references so they can inspect the unit.
	 * Use World methods to change the simulation. Do not cast away const.
	 * Dead units remain stored so events and effects can still refer to their IDs.
	 */
	struct Entity
	{
		EntityId id{};
		std::string type;
		Position position;
		Footprint footprint{};
		TargetProfile targetProfile{};
		std::uint32_t hp{};				///< Current health. The spawn-time hp property initializes this field.
		PropertyMap properties;			///< Resolved spawn-time stats, excluding hp; stable during simulation.
		std::optional<Position> march;	///< Assigned destination; cleared on arrival or death.

		/**
		 * @brief Checks whether the unit has positive health.
		 *
		 * Core and Features call this method before work that requires a living unit.
		 *
		 * @return True if hp is not zero; false if the unit is dead.
		 */
		bool alive() const
		{
			return hp != 0;
		}

		/**
		 * @brief Provides read-only access to one configured ability.
		 *
		 * Inspection code and tests call this method when they need an ability instance.
		 *
		 * @param index Zero-based index in template priority order.
		 * @return A reference valid until this Entity is destroyed.
		 * @throws std::out_of_range If index is outside the ability list.
		 */
		const IAbility& ability(std::size_t index) const
		{
			return *abilities_.at(index);
		}

		/**
		 * @brief Provides read-only access to one stored effect.
		 *
		 * Inspection code and tests call this method to examine effect state.
		 * Do not retain the reference across World changes: effect removal can destroy it and change indices.
		 *
		 * @param index Zero-based index in effect insertion order.
		 * @return A reference to the stored effect.
		 * @throws std::out_of_range If index is outside the effect list.
		 */
		const IEffect& effect(std::size_t index) const
		{
			return *effects_.at(index);
		}

		/**
		 * @brief Counts the effects currently stored on this unit.
		 *
		 * Inspection code and tests call this method before indexing effects.
		 * Expired effects are included until the outermost operation removes them.
		 *
		 * @return Number of stored effects.
		 */
		std::size_t effectCount() const
		{
			return effects_.size();
		}

	private:
		friend class World;

		/**
		 * @brief Template order is preserved for the scheduler's priority scan.
		 */
		std::vector<std::unique_ptr<IAbility>> abilities_;
		std::vector<std::unique_ptr<IEffect>> effects_;
	};

}  // namespace sw::core
