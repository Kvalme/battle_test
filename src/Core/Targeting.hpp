#pragma once

namespace sw::core
{

	/**
	 * @brief Selects melee eligibility or ranged distance adjustments in World::targets.
	 */
	enum class AttackKind
	{
		Melee,
		Ranged
	};

	/**
	 * @brief Ground units block occupancy/adjacency; Air units do neither.
	 */
	enum class Layer
	{
		Ground,
		Air
	};

	/**
	 * @brief Defines whether and how other units can target this unit.
	 *
	 * World copies these settings from the template when it creates a unit.
	 * Invulnerable units are excluded from targeting and ignore World::damage;
	 * meleeTargetable affects only melee selection, not direct damage requests.
	 */
	struct TargetProfile
	{
		Layer layer{Layer::Ground};
		bool invulnerable{false};
		bool meleeTargetable{true};
		int rangeDelta{0};	///< Added to effective distance for ranged attacks against this target.
	};

	/**
	 * @brief Defines which targets an ability can select.
	 *
	 * An ability passes this filter to World::targets(). The filter does not select a random target.
	 * Range is inclusive Chebyshev distance between footprints. For ranged attacks,
	 * effective bounds are max(1, minRange - target.rangeDelta) and
	 * maxRange - target.rangeDelta. An inverted interval selects nothing.
	 * No team filter exists: every other living, eligible unit can be a target.
	 */
	struct TargetQuery
	{
		int minRange{1};
		int maxRange{1};
		bool noAdjacent{false};	 ///< Reject the entire query when a living ground unit is adjacent.
		AttackKind kind{AttackKind::Melee};

		/**
		 * @brief Checks whether all query fields are equal.
		 *
		 * Code that compares targeting settings can use this operator during validation.
		 *
		 * @param other Value to compare with this value.
		 * @return True if all query fields are equal; false otherwise.
		 */
		bool operator==(const TargetQuery& other) const = default;
	};

}  // namespace sw::core
