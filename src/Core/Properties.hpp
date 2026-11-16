#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>

namespace sw::core
{

	/**
	 * @brief Typed configuration values; no implicit numeric/string coercion is performed.
	 */
	using PropertyValue = std::variant<std::int64_t, double, bool, std::string>;

	/**
	 * @brief Named unit stats or ability-local parameters, depending on the owning config.
	 */
	using PropertyMap = std::map<std::string, PropertyValue>;

	/**
	 * @brief Combines built-in unit properties with template values.
	 *
	 * The configuration loader calls this function during validation; World calls it during spawning.
	 * It starts with integer defaults hp=10, agility=1, strength=1, and range=3.
	 * Template values replace defaults and can add new names. Range checks happen later.
	 *
	 * @param templateProperties Template values to add or use instead of defaults.
	 * @return A new map with the resolved values.
	 * @throws std::invalid_argument If a template changes the type of a built-in property.
	 */
	PropertyMap resolveUnitProperties(const PropertyMap& templateProperties);

	/**
	 * @brief Reads a property that must contain an integer.
	 *
	 * Core and Feature code call this function when a setting requires an exact integer type.
	 * The function does not convert strings, floating-point values, or booleans.
	 *
	 * @param properties Map to inspect.
	 * @param key Name of the required integer property.
	 * @return The stored int64_t value.
	 * @throws std::invalid_argument If the key is missing or its value is not an integer.
	 */
	std::int64_t integerProperty(const PropertyMap& properties, const std::string& key);

}  // namespace sw::core
