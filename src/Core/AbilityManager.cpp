#include "AbilityManager.hpp"

#include "Abilities.hpp"
#include "Config.hpp"

#include <stdexcept>
#include <utility>

namespace sw::core
{

	AbilityManager* AbilityManager::instance()
	{
		// Construct on demand because Features register from static initializers.
		static AbilityManager manager;
		return &manager;
	}

	namespace
	{

		std::string describe(std::source_location source)
		{
			return std::string(source.file_name()) + ":" + std::to_string(source.line());
		}

		std::string describe(const AbilityConfig& config, std::size_t index)
		{
			auto label = "ability #" + std::to_string(index + 1) + " (" + config.implementation + ")";
			if (!config.name.empty())
			{
				label += " '" + config.name + "'";
			}
			return label;
		}

	}  // namespace

	bool AbilityManager::registerAbility(std::string name, Factory factory, std::source_location source)
	{
		if (frozen_)
		{
			errors_.push_back("Late ability registration '" + name + "' at " + describe(source));
		}
		else if (name.empty())
		{
			errors_.push_back("Empty ability name at " + describe(source));
		}
		else if (!factory)
		{
			errors_.push_back("Empty ability factory for '" + name + "' at " + describe(source));
		}
		else
		{
			auto [registration, inserted] = factories_.try_emplace(name, Registration{std::move(factory), source});
			if (inserted)
			{
				return true;
			}
			errors_.push_back(
					"Duplicate ability registration '" + name + "' at " + describe(source) + " (first registered at "
					+ describe(registration->second.source) + ")");
		}
		return false;
	}

	const std::vector<std::string>& AbilityManager::validateAndFreeze()
	{
		frozen_ = true;
		return errors_;
	}

	std::unique_ptr<IAbility> AbilityManager::create(const AbilityConfig& config, const PropertyMap& properties) const
	{
		const auto& key = config.implementation;
		const auto factory = factories_.find(key);
		if (factory == factories_.end())
		{
			throw std::invalid_argument("Unknown ability implementation: " + key);
		}
		auto ability = factory->second.factory(config, properties);
		if (!ability)
		{
			throw std::runtime_error(
					"Ability factory for '" + key + "' returned nullptr at " + describe(factory->second.source));
		}
		return ability;
	}

	std::vector<std::unique_ptr<IAbility>> AbilityManager::createAll(
			const UnitTemplate& unit, const PropertyMap& properties) const
	{
		std::vector<std::unique_ptr<IAbility>> abilities;
		abilities.reserve(unit.abilities.size());
		const auto unitLabel
				= (unit.source.empty() ? std::string{} : unit.source + ": ") + "unit '" + unit.type + "', ";
		for (std::size_t index = 0; index < unit.abilities.size(); ++index)
		{
			const auto& config = unit.abilities[index];
			std::unique_ptr<IAbility> ability;
			try
			{
				ability = create(config, properties);
			}
			catch (const std::invalid_argument& error)
			{
				throw std::invalid_argument(unitLabel + describe(config, index) + ": " + error.what());
			}
			catch (const std::runtime_error& error)
			{
				throw std::runtime_error(unitLabel + describe(config, index) + ": " + error.what());
			}
			for (std::size_t previous = 0; previous < abilities.size(); ++previous)
			{
				try
				{
					// Either feature can impose a constraint on a pair regardless of template order.
					ability->validateCompatibility(*abilities[previous]);
					abilities[previous]->validateCompatibility(*ability);
				}
				catch (const std::invalid_argument& error)
				{
					throw std::invalid_argument(
							unitLabel + describe(config, index) + " and " + describe(unit.abilities[previous], previous)
							+ ": " + error.what());
				}
			}
			abilities.push_back(std::move(ability));
		}
		return abilities;
	}

}  // namespace sw::core
