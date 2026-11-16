#include "MtRandom.hpp"

#include <stdexcept>

namespace sw::core
{

	MtRandom::MtRandom(std::uint32_t seed) :
			engine_(seed)
	{}

	std::uint32_t MtRandom::uniform(std::uint32_t min, std::uint32_t max)
	{
		if (min > max)
		{
			throw std::invalid_argument("Invalid random range");
		}
		// Use the same distribution path for all valid ranges, including a single possible result.
		return std::uniform_int_distribution<std::uint32_t>(min, max)(engine_);
	}

}  // namespace sw::core
