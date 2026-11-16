#include "Core/Abilities.hpp"
#include "Core/AbilityManager.hpp"

namespace
{

	const bool registered = sw::core::AbilityManager::instance()->registerAbility(
			"rending",
			[](const sw::core::AbilityConfig&, const sw::core::PropertyMap&) -> std::unique_ptr<sw::core::IAbility>
			{ return {}; });

}
