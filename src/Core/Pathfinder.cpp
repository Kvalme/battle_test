#include "Pathfinder.hpp"

#include "Footprint.hpp"

#include <array>
#include <unordered_set>

namespace sw::core
{

	namespace
	{

		struct PathNode
		{
			Position position;
			std::size_t previous;
			std::size_t steps;
		};

		std::uint64_t positionKey(Position position)
		{
			return (static_cast<std::uint64_t>(position.x) << 32) | position.y;
		}

		std::array<int, 3> offsets(std::uint32_t from, std::uint32_t toward)
		{
			if (from == toward)
			{
				return {0, -1, 1};
			}
			const int direction = from < toward ? 1 : -1;
			return {direction, 0, -direction};
		}

	}  // namespace

	Pathfinder::Pathfinder(std::uint32_t width, std::uint32_t height) :
			width_(width),
			height_(height)
	{}

	std::vector<Position> Pathfinder::find(
			Position start, Position destination, unsigned maxCells, const Passable& passable) const
	{
		if (maxCells == 0 || start == destination)
		{
			return {};
		}

		// BFS over reachable cells. For equal-length paths, prefer steps toward the destination.
		// If the destination is occupied or unreachable, the unit approaches the closest reachable cell.
		const Footprint cell;
		const auto remaining = [&](Position position)
		{
			return cell.distanceTo(position, cell, destination);
		};
		std::vector<PathNode> frontier{{start, 0, 0}};
		std::unordered_set<std::uint64_t> visited{positionKey(start)};
		std::size_t best = 0;
		auto bestDistance = remaining(start);
		for (std::size_t index = 0; index < frontier.size() && bestDistance > 0; ++index)
		{
			const auto node = frontier[index];
			for (int dy : offsets(node.position.y, destination.y))
			{
				for (int dx : offsets(node.position.x, destination.x))
				{
					const auto x = static_cast<std::int64_t>(node.position.x) + dx;
					const auto y = static_cast<std::int64_t>(node.position.y) + dy;
					if (x < 0 || y < 0 || x >= width_ || y >= height_)
					{
						continue;
					}
					const Position candidate{static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)};
					if (!visited.insert(positionKey(candidate)).second || !passable(candidate))
					{
						continue;
					}
					frontier.push_back({candidate, index, node.steps + 1});
					// BFS order keeps the shortest route to the first cell found at each distance.
					if (const auto distance = remaining(candidate); distance < bestDistance)
					{
						best = frontier.size() - 1;
						bestDistance = distance;
						if (distance == 0)
						{
							break;
						}
					}
				}
				if (bestDistance == 0)
				{
					break;
				}
			}
		}
		// Only a strictly closer cell is progress. This keeps blocked units from moving back and forth.
		if (best == 0)
		{
			return {};
		}

		// Walk the parent chain to the last step of this activation, then reconstruct its prefix.
		auto result = frontier[best];
		while (result.steps > maxCells)
		{
			result = frontier[result.previous];
		}
		std::vector<Position> path(result.steps);
		while (result.steps > 0)
		{
			path[result.steps - 1] = result.position;
			result = frontier[result.previous];
		}
		return path;
	}

}  // namespace sw::core
