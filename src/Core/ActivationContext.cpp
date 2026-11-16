#include "ActivationContext.hpp"

#include "IRandom.hpp"
#include "World.hpp"

namespace sw::core
{

	std::optional<EntityId> ActivationContext::chooseTarget(const TargetQuery& query, std::string_view group)
	{
		if (!group.empty())
		{
			const auto cached = targetSelections.find(group);
			if (cached != targetSelections.end())
			{
				return cached->second;
			}
		}

		const auto candidates = world.targets(actor, query);
		std::optional<EntityId> selected;
		if (!candidates.empty())
		{
			selected = candidates[random.uniform(0, static_cast<std::uint32_t>(candidates.size() - 1))];
		}
		// Cache an empty result as well so a fallback does not repeat the search.
		if (!group.empty())
		{
			targetSelections.emplace(group, selected);
		}
		return selected;
	}

}  // namespace sw::core
