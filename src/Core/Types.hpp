#pragma once

#include <cstdint>

namespace sw::core
{

	/**
	 * @brief IDs remain reserved after death so events and effects can still identify their source.
	 */
	using EntityId = std::uint32_t;

	/**
	 * @brief Identifies a map cell with zero-based coordinates.
	 * Multi-cell units use this position as the top-left cell of their footprint.
	 */
	struct Position
	{
		std::uint32_t x{}, y{};

		/**
		 * @brief Checks whether both coordinates are equal.
		 *
		 * Core geometry code and Features use this comparison to test positions.
		 *
		 * @param other Value to compare with this value.
		 * @return True if both coordinates are equal; false otherwise.
		 */
		bool operator==(const Position& other) const = default;
	};

}  // namespace sw::core
