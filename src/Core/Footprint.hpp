#pragma once

#include "Types.hpp"

#include <algorithm>

namespace sw::core
{

	/**
	 * @brief Describes the rectangular area occupied by a unit.
	 *
	 * World uses this geometry to check map bounds, occupied ground cells, and attack distance.
	 * The origin is the top-left cell. Airborne units use the same geometry for bounds and distance,
	 * but do not block ground cells.
	 */
	struct Footprint
	{
		std::uint32_t width{1};
		std::uint32_t height{1};

		/**
		 * @brief Checks whether this rectangle fits entirely inside a map.
		 *
		 * World calls this method during map creation, spawning, movement, and march validation.
		 *
		 * @param origin Top-left cell of this rectangle.
		 * @param mapWidth Map width in cells.
		 * @param mapHeight Map height in cells.
		 * @return True if both footprint dimensions are nonzero and every cell is inside the map.
		 */
		bool fitsAt(Position origin, std::uint32_t mapWidth, std::uint32_t mapHeight) const
		{
			return width != 0 && height != 0 && static_cast<std::uint64_t>(origin.x) + width <= mapWidth
				   && static_cast<std::uint64_t>(origin.y) + height <= mapHeight;
		}

		/**
		 * @brief Calculates the minimum number of eight-direction steps between two rectangles.
		 *
		 * World calls this method for occupancy, adjacency, and target range checks.
		 * Both rectangles must have nonzero width and height. Diagonal and straight steps both count as one.
		 *
		 * @param origin Top-left cell of this rectangle.
		 * @param other Rectangle to compare with this one.
		 * @param otherOrigin Top-left cell of the other rectangle.
		 * @return Zero for overlapping rectangles, one for adjacent rectangles, or their minimum Chebyshev
		 * distance.
		 */
		std::int64_t distanceTo(Position origin, const Footprint& other, Position otherOrigin) const
		{
			return std::max(
					axisDistance(origin.x, width, otherOrigin.x, other.width),
					axisDistance(origin.y, height, otherOrigin.y, other.height));
		}

	private:
		static std::int64_t axisDistance(
				std::int64_t first, std::int64_t firstSize, std::int64_t second, std::int64_t secondSize)
		{
			const auto firstEnd = first + firstSize - 1;
			const auto secondEnd = second + secondSize - 1;
			return std::max<std::int64_t>({0, second - firstEnd, first - secondEnd});
		}
	};

}  // namespace sw::core
