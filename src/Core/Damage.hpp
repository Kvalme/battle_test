#pragma once

#include "Types.hpp"

#include <string>
#include <vector>

namespace sw::core
{

	/**
	 * @brief Specifies damage that an action or effect asks World to apply.
	 *
	 * source identifies the unit credited with damage; target identifies the unit to damage.
	 * amount is damage before modifiers; nonpositive amounts are
	 * ignored. The target must exist; the source need not be alive. World does not
	 * interpret tags, leaving interactions such as poison amplification to effects.
	 */
	struct DamageRequest
	{
		EntityId source{}, target{};
		std::int64_t amount{};

		/**
		 * @brief Effects can use these tags to decide which damage they modify.
		 */
		std::vector<std::string> tags;
	};

}  // namespace sw::core
