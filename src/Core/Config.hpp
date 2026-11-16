#pragma once

#include "Footprint.hpp"
#include "Properties.hpp"
#include "Targeting.hpp"

#include <map>
#include <string>
#include <vector>

namespace sw::core
{

	/**
	 * @brief Specifies which ability to create and which parameters it receives.
	 *
	 * The configuration loader fills this data from XML. AbilityManager passes it to a factory.
	 * Parameters apply to this ability instance, not to the whole unit. The factory validates them.
	 */
	struct AbilityConfig
	{
		std::string implementation;	 ///< Registered factory key.
		PropertyMap properties;		 ///< Parameters scoped to this ability instance.
		std::string name{};			 ///< Optional label for diagnostics.
	};

	/**
	 * @brief Defines the initial state and ability order for a type of unit.
	 *
	 * The configuration loader creates this template from XML. World::spawn() copies its values
	 * and creates separate abilities for each unit. World does not retain a reference to the template.
	 */
	struct UnitTemplate
	{
		std::string type;
		PropertyMap defaults;				   ///< Unit stats layered over the built-in defaults.
		std::vector<AbilityConfig> abilities;  ///< In activation priority order.
		Footprint footprint{};
		TargetProfile targetProfile{};
		std::string source{};  ///< Template filename for configuration diagnostics.
	};

	/**
	 * @brief Templates indexed by their unique unit type, for command/configuration lookup.
	 */
	using UnitTemplates = std::map<std::string, UnitTemplate>;

}  // namespace sw::core
