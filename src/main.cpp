#include "App/EventAdapter.hpp"
#include "Core/AbilityManager.hpp"
#include "Core/MtRandom.hpp"
#include "Core/Scheduler.hpp"
#include "Core/World.hpp"
#include "IO/Commands/CreateMap.hpp"
#include "IO/Commands/March.hpp"
#include "IO/Commands/SpawnHunter.hpp"
#include "IO/Commands/SpawnSwordsman.hpp"
#include "IO/Config/UnitLoader.hpp"
#include "IO/System/CommandParser.hpp"
#include "UnitsXml.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
#ifdef SW_TEST_ACTIVATION_LIMIT
	constexpr std::uint64_t activationLimit = SW_TEST_ACTIVATION_LIMIT;
#else
	constexpr auto activationLimit = sw::core::Scheduler::defaultActivationLimit;
#endif
}

int main(int argc, char** argv)
{
	try
	{
		const auto& registrationErrors = sw::core::AbilityManager::instance()->validateAndFreeze();
		if (!registrationErrors.empty())
		{
			for (const auto& error : registrationErrors)
			{
				std::cerr << error << '\n';
			}
			return 1;
		}
		if (argc != 2)
		{
			throw std::runtime_error("Expected exactly one scenario file");
		}
		auto templates = sw::io::loadUnitTemplates(sw::app::unitsXml);
		std::ifstream file(argv[1]);
		if (!file)
		{
			throw std::runtime_error("Scenario file not found");
		}

		sw::EventSystem output;
		sw::app::EventAdapter events(output);
		sw::core::MtRandom random(0);
		sw::core::World world(events);
		sw::core::Scheduler scheduler(world, random);
		sw::io::CommandParser parser;
		parser.add<sw::io::CreateMap>([&](auto c) { world.createMap(c.width, c.height); });
		parser.add<sw::io::SpawnSwordsman>(
				[&](auto c)
				{
					world.spawn(
							c.unitId,
							{c.x, c.y},
							templates.at("swordsman"),
							{{"hp", std::int64_t(c.hp)},
							 {"strength", std::int64_t(c.strength)},
							 {"rending_chance", std::int64_t(c.chance)},
							 {"rending", std::int64_t(c.rending)}});
				});
		parser.add<sw::io::SpawnHunter>(
				[&](auto c)
				{
					world.spawn(
							c.unitId,
							{c.x, c.y},
							templates.at("hunter"),
							{{"hp", std::int64_t(c.hp)},
							 {"agility", std::int64_t(c.agility)},
							 {"strength", std::int64_t(c.strength)},
							 {"range", std::int64_t(c.range)},
							 {"poison_chance", std::int64_t(c.chance)},
							 {"poison", std::int64_t(c.poison)}});
				});
		parser.add<sw::io::March>([&](auto c) { world.march(c.unitId, {c.targetX, c.targetY}); });
		parser.parse(file);
		if (scheduler.run(activationLimit) == sw::core::RunResult::ActivationLimit)
		{
			std::cerr << "Simulation activation limit reached (" << activationLimit << ")\n";
			return 2;
		}
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "Error: " << error.what() << '\n';
		return 1;
	}
}
