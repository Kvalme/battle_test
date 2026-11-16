#include "Properties.hpp"

#include <stdexcept>

namespace sw::core
{

	PropertyMap resolveUnitProperties(const PropertyMap& templateProperties)
	{
		PropertyMap properties{
				{"hp", std::int64_t{10}},
				{"agility", std::int64_t{1}},
				{"strength", std::int64_t{1}},
				{"range", std::int64_t{3}},
		};
		// A template may add new stats and replace base values while keeping the base types.
		for (const auto& [name, value] : templateProperties)
		{
			const auto base = properties.find(name);
			if (base != properties.end() && base->second.index() != value.index())
			{
				throw std::invalid_argument("Wrong type for base property: " + name);
			}
			properties.insert_or_assign(name, value);
		}
		return properties;
	}

	std::int64_t integerProperty(const PropertyMap& properties, const std::string& key)
	{
		const auto found = properties.find(key);
		if (found == properties.end())
		{
			throw std::invalid_argument("Unknown property: " + key);
		}
		if (const auto* value = std::get_if<std::int64_t>(&found->second))
		{
			return *value;
		}
		throw std::invalid_argument("Expected integer property: " + key);
	}

}  // namespace sw::core
