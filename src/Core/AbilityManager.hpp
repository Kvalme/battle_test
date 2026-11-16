#pragma once

#include "Properties.hpp"

#include <functional>
#include <map>
#include <memory>
#include <source_location>
#include <string>
#include <vector>

namespace sw::core
{

	struct AbilityConfig;
	struct UnitTemplate;
	class IAbility;

	/**
	 * @brief Creates abilities from the implementation names in unit configuration.
	 *
	 * Features register factory functions before application startup.
	 * The application closes registration and checks errors before it loads unit templates.
	 * The loader and World then use this registry to create ability instances.
	 * The registry stores factories, not the created abilities. Use it from one thread at a time.
	 */
	class AbilityManager
	{
	public:
		/**
		 * @brief Defines the function that creates and validates one ability.
		 *
		 * AbilityManager calls a factory during template validation and unit creation.
		 * The first argument contains ability parameters. The second contains resolved unit properties without hp.
		 * A factory must return a new non-null ability and throw std::invalid_argument for invalid settings.
		 * Do not save references to either argument.
		 */
		using Factory = std::function<std::unique_ptr<IAbility>(const AbilityConfig&, const PropertyMap&)>;

		/**
		 * @brief Provides access to the single ability registry.
		 *
		 * Feature registration code, configuration loading, and World call this method when they need the registry.
		 *
		 * @return A non-null pointer to the process-wide registry. The caller must not delete it.
		 */
		static AbilityManager* instance();

		/**
		 * @brief Registers the factory for an ability implementation name.
		 *
		 * Features call this method during static initialization, before validateAndFreeze().
		 * The method records an error for empty names, empty factories, duplicates, or late calls.
		 * A failed registration does not replace an existing factory.
		 *
		 * @param name Unique implementation name used in AbilityConfig.
		 * @param factory Function that creates and validates an ability.
		 * @param source Registration location used in error messages.
		 * @return True if registration succeeded; false if an error was recorded.
		 */
		bool registerAbility(
				std::string name, Factory factory, std::source_location source = std::source_location::current());

		/**
		 * @brief Closes registration and provides all registration errors.
		 *
		 * The application calls this method before loading templates and refuses startup if errors exist.
		 * Further registrations are rejected. Repeated calls are allowed.
		 * Registration errors are returned rather than thrown.
		 *
		 * @return A reference to the error list stored in the registry; an empty list means no recorded errors.
		 */
		const std::vector<std::string>& validateAndFreeze();

		/**
		 * @brief Creates one ability through its registered factory.
		 *
		 * createAll() calls this method for each ability configuration.
		 * Direct callers must supply resolved properties. This method does not check ability pairs or close
		 * registration.
		 * Factory exceptions propagate to the caller.
		 *
		 * @param config Implementation name and parameters for the ability.
		 * @param properties Resolved unit properties, excluding hp.
		 * @return A new ability whose lifetime is managed by the returned unique_ptr.
		 * @throws std::invalid_argument If the implementation name is unknown.
		 * @throws std::runtime_error If the factory returns nullptr.
		 */
		std::unique_ptr<IAbility> create(const AbilityConfig& config, const PropertyMap& properties) const;

		/**
		 * @brief Creates and checks all abilities configured for a unit.
		 *
		 * The configuration loader calls this method to validate a template.
		 * World::spawn() calls it again with the final unit properties.
		 * It creates abilities in template order and checks each pair in both directions.
		 * On failure, it destroys partial results and adds unit and ability context to configuration errors.
		 *
		 * @param unit Template containing ordered ability configurations.
		 * @param properties Resolved unit properties, excluding hp.
		 * @return New abilities in activation priority order.
		 * @throws std::invalid_argument If configuration or pair compatibility is invalid.
		 * @throws std::runtime_error If a factory fails with a runtime error.
		 */
		std::vector<std::unique_ptr<IAbility>> createAll(const UnitTemplate& unit, const PropertyMap& properties) const;

	private:
		struct Registration
		{
			Factory factory;
			std::source_location source;
		};

		std::map<std::string, Registration> factories_;
		bool frozen_{false};
		std::vector<std::string> errors_;
	};

}  // namespace sw::core
