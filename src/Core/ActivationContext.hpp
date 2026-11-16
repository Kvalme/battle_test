#pragma once

#include "Targeting.hpp"
#include "Types.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace sw::core
{

	class IRandom;
	class World;

	/**
	 * @brief Saves a target ID or a missing target for each group during one activation.
	 */
	using TargetSelection = std::map<std::string, std::optional<EntityId>, std::less<>>;

	/**
	 * @brief Provides the inputs shared by abilities during one unit activation.
	 *
	 * World::activate() creates this object after it processes the unit's effects.
	 * It passes the same object to each ability considered during this activation.
	 * Use random for random decisions and chooseTarget() to select targets.
	 * Do not store a reference to this object after propose() returns.
	 */
	struct ActivationContext
	{
		const World& world;				   ///< Simulation to inspect without changes.
		EntityId actor;					   ///< Unit whose activation is being processed.
		IRandom& random;				   ///< Generator to use for all random decisions in this activation.
		TargetSelection targetSelections;  ///< Results saved by chooseTarget() for shared groups.

		/**
		 * @brief Selects a random target or reuses the target saved for a group.
		 *
		 * An ability calls this method from propose() when it needs a target.
		 * With an empty group, each call searches again and makes a new random selection.
		 * With a nonempty group, the first call saves its result, including a missing target.
		 * Later calls with that group return the saved result without checking query or using random.
		 * Abilities that share a group must check query compatibility in validateCompatibility().
		 *
		 * @param query Conditions passed to World::targets() for a new selection.
		 * @param group Name of a shared selection, or an empty string for an independent selection.
		 * @return The selected entity ID, or std::nullopt if no target was found.
		 */
		std::optional<EntityId> chooseTarget(const TargetQuery& query, std::string_view group = {});
	};

}  // namespace sw::core
