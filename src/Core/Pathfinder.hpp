#pragma once

#include "Types.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace sw::core
{

	/**
	 * @brief Finds short routes on a rectangular map with eight-direction steps.
	 *
	 * World uses this class for march routes. Pathfinder does not know about units or World.
	 * The caller supplies the map size and a function that tells which cells can be entered.
	 */
	class Pathfinder
	{
	public:
		/// Tells whether a unit can stand with its top-left cell at the given position.
		using Passable = std::function<bool(Position)>;

		/**
		 * @brief Creates a path finder for one map size.
		 *
		 * @param width Map width in cells.
		 * @param height Map height in cells.
		 */
		Pathfinder(std::uint32_t width, std::uint32_t height);

		/**
		 * @brief Finds the first steps of a shortest route toward a destination.
		 *
		 * The search is breadth first. For equal routes, steps toward the destination come first.
		 * If the destination cannot be reached, the route leads to the reachable cell with the smallest
		 * Chebyshev distance to it, but only if that cell is closer than start.
		 * The footprint of the moving unit is handled by passable. Pathfinder checks only map bounds.
		 * The method does not call passable for start.
		 *
		 * @param start Current top-left cell.
		 * @param destination Requested top-left cell.
		 * @param maxCells Maximum number of route steps to return.
		 * @param passable Callback that accepts or rejects a cell.
		 * @return A route prefix without start, or an empty vector if the unit is already at the
		 * destination, maxCells is zero, or no reachable cell is closer to the destination.
		 */
		std::vector<Position> find(
				Position start, Position destination, unsigned maxCells, const Passable& passable) const;

	private:
		std::uint32_t width_;
		std::uint32_t height_;
	};

}  // namespace sw::core
