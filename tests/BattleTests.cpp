#include "Core/Abilities.hpp"
#include "Core/AbilityManager.hpp"
#include "Core/ActivationContext.hpp"
#include "Core/Damage.hpp"
#include "Core/Entity.hpp"
#include "Core/IRandom.hpp"
#include "Core/Scheduler.hpp"
#include "Core/World.hpp"
#include "IO/Config/UnitLoader.hpp"
#include "UnitsXml.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

void runFeatureTests();
void runTerminationTests();

namespace
{
	using namespace sw::core;

	void require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	template <class F>
	void rejects(F&& f, const std::string& message)
	{
		try
		{
			f();
		}
		catch (const std::exception&)
		{
			return;
		}
		throw std::runtime_error(message);
	}

	struct Sink : IEventSink
	{
		std::vector<Event> events;

		void emit(const Event& e) override
		{
			events.push_back(e);
		}
	};

	struct Random : IRandom
	{
		unsigned calls{};

		std::uint32_t uniform(std::uint32_t low, std::uint32_t) override
		{
			++calls;
			return low;
		}
	};

	struct Fixture
	{
		Sink sink;
		Random random;
		World world{sink};
		Scheduler scheduler{world, random};
		UnitTemplates units = sw::io::loadUnitTemplates(sw::app::unitsXml);

		Fixture()
		{
			world.createMap(8, 8);
		}

		void passive(EntityId id, Position position, std::int64_t hp = 20)
		{
			world.spawn(id, position, UnitTemplate{"passive", {{"hp", hp}}, {}});
		}
	};

	std::string replaceOnce(std::string text, const std::string& from, const std::string& to)
	{
		const auto offset = text.find(from);
		require(offset != std::string::npos, "test XML substitution not found: " + from);
		text.replace(offset, from.size(), to);
		return text;
	}

	void configurationTests()
	{
		const auto units = sw::io::loadUnitTemplates(sw::app::unitsXml);
		require(units.contains("swordsman") && units.contains("hunter"), "embedded XML must contain both units");
		require(units.at("swordsman").abilities.size() == 3, "swordsman priority list");
		require(units.at("hunter").abilities.size() == 4, "hunter priority list");
		rejects([] { sw::io::loadUnitTemplates("<units>"); }, "malformed XML accepted");
		rejects(
				[]
				{
					sw::io::loadUnitTemplates(
							"<!DOCTYPE units [<!ENTITY x SYSTEM 'file:///nonexistent'>]><units>&x;</units>");
				},
				"DTD accepted");
		rejects([] { sw::io::loadUnitTemplates("<units><unknown/></units>"); }, "unknown element accepted");
		rejects([] { sw::io::loadUnitTemplates("<units xmlns='urn:test'/>"); }, "XML namespace accepted");
		rejects([] { sw::io::loadUnitTemplates("<units/><units/>"); }, "multiple roots accepted");
		rejects([] { sw::io::loadUnitTemplates("<units/>trailing"); }, "trailing text accepted");

		const auto source = std::ranges::find(sw::app::unitsXml, "swordsman.xml", &sw::io::UnitXmlDocument::filename);
		require(source != sw::app::unitsXml.end(), "missing embedded swordsman filename");
		const std::string xml(source->xml);
		const auto unitStart = xml.find("<unit ");
		require(unitStart != std::string::npos, "missing standalone unit root");
		require(sw::io::loadUnitTemplates("<units>" + xml.substr(unitStart) + "</units>").contains("swordsman"),
				"legacy aggregate root with one unit should still load");
		rejects([&] { sw::io::loadUnitTemplates(xml + "<units/>"); }, "second root after valid templates accepted");
		rejects([&] { sw::io::loadUnitTemplates(xml + "trailing"); }, "trailing text after valid templates accepted");
		rejects([&] { sw::io::loadUnitTemplates(xml + std::string(1, '\0') + "trailing"); }, "embedded NUL accepted");
		rejects(
				[&]
				{
					sw::io::loadUnitTemplates(
							replaceOnce(xml, "<unit name=\"swordsman\">", "<unit name=\"swordsman\" name=\"other\">"));
				},
				"duplicate XML attribute accepted");
		rejects([&] { sw::io::loadUnitTemplates(replaceOnce(xml, "name=\"swordsman\"", "name=\"&unknown;\"")); },
				"unknown XML entity accepted");
		const auto escaped
				= sw::io::loadUnitTemplates(replaceOnce(xml, "name=\"swordsman\"", "name=\"sword&amp;man\""));
		require(escaped.contains("sword&man"), "standard XML entity was not decoded");

		const std::array duplicate{
				sw::io::UnitXmlDocument{"first.xml", xml}, sw::io::UnitXmlDocument{"second.xml", xml}};
		const auto diagnose = [](auto&& operation, std::initializer_list<std::string_view> fragments)
		{
			try
			{
				operation();
			}
			catch (const std::runtime_error& error)
			{
				require(std::ranges::all_of(
								fragments,
								[&](auto text)
								{ return std::string_view(error.what()).find(text) != std::string_view::npos; }),
						"incomplete diagnostic: " + std::string(error.what()));
				return std::string(error.what());
			}
			throw std::runtime_error("bad XML accepted");
		};
		diagnose([&] { sw::io::loadUnitTemplates(duplicate); }, {"first.xml", "second.xml", "swordsman"});
		const auto unknown = replaceOnce(xml, "implementation=\"rending\"", "implementation=\"missing\"");
		const std::array badFactory{sw::io::UnitXmlDocument{"unknown.xml", unknown}};
		diagnose([&] { sw::io::loadUnitTemplates(badFactory); }, {"unknown.xml", "missing"});
		const auto nameless = replaceOnce(xml, "name=\"swordsman\"", "name=\"  \"");
		const std::array badName{sw::io::UnitXmlDocument{"blank.xml", nameless}};
		diagnose([&] { sw::io::loadUnitTemplates(badName); }, {"blank.xml", "expected nonempty 'name'"});

		const std::string chance = R"(<property name="chanceStat" type="string" value="rending_chance"/>)";
		const auto badProperty = [&](const std::string& replacement, std::initializer_list<std::string_view> fragments)
		{
			const auto invalid = replaceOnce(xml, chance, replacement);
			const std::array documents{sw::io::UnitXmlDocument{"bad-property.xml", invalid}};
			const auto message = diagnose([&] { sw::io::loadUnitTemplates(documents); }, fragments);
			require(message.find("bad-property.xml") != std::string::npos
							&& message.find("unit 'swordsman'") != std::string::npos
							&& message.find("(rending)") != std::string::npos,
					"missing configuration context: " + message);
		};
		badProperty("", {"missing required property", "chanceStat"});
		badProperty(
				R"(<property name="chanceStat" type="int" value="100"/>)", {"chanceStat", "must have type 'string'"});
		badProperty(
				chance + R"(<property name="minRangeExtra" type="int" value="2"/>)",
				{"unknown property", "minRangeExtra"});
		badProperty(
				R"(<property name="chanceStat" type="string" value="missing-stat"/>)",
				{"chanceStat", "missing-stat", "unknown unit stat"});
		badProperty(R"(<property name="chanceStat" type="string" value="hp"/>)", {"chanceStat", "unknown unit stat"});
		badProperty(chance + chance, {"duplicate property", "chanceStat"});
		badProperty(
				chance + R"(<property name="noAdjacent" type="bool" value="maybe"/>)",
				{"noAdjacent", "invalid boolean"});
		const auto invalidCells
				= replaceOnce(xml, R"(name="cells" type="int" value="1")", R"(name="cells" type="int" value="0")");
		const std::array badValue{sw::io::UnitXmlDocument{"bad-value.xml", invalidCells}};
		diagnose(
				[&] { sw::io::loadUnitTemplates(badValue); },
				{"bad-value.xml", "swordsman", "(move)", "cells", "must be between"});
		rejects([]
				{ sw::io::loadUnitTemplates(R"(<unit name="old"><actions><action type="move"/></actions></unit>)"); },
				"old action schema accepted");
		rejects([&] { sw::io::loadUnitTemplates(replaceOnce(xml, "<unit name=", "<unit typo=\"true\" name=")); },
				"unknown attribute accepted");
		rejects([] { sw::io::loadUnitTemplatesFile("/nonexistent/units.xml"); }, "missing file accepted");
	}

	void basePropertyTests()
	{
		Fixture f;
		const auto& hunter = f.units.at("hunter");
		const auto& swordsman = f.units.at("swordsman");
		require(hunter.defaults.size() == 2 && hunter.defaults.contains("poison_chance")
						&& hunter.defaults.contains("poison"),
				"hunter template stores only its local properties");
		require(swordsman.defaults.size() == 2 && swordsman.defaults.contains("rending_chance")
						&& swordsman.defaults.contains("rending"),
				"swordsman template stores only its local properties");

		const std::string xml = R"xml(<unit name="guard"><abilities><ability implementation="attack">
      <properties>
        <property name="kind" type="string" value="melee"/>
        <property name="damageStat" type="string" value="strength"/>
      </properties>
    </ability></abilities></unit>)xml";
		const auto guard = sw::io::loadUnitTemplates(xml).at("guard");
		f.world.spawn(1, {0, 0}, guard, {{"hp", std::int64_t(15)}, {"strength", std::int64_t(3)}});
		f.world.spawn(2, {1, 0}, hunter);
		f.world.spawn(3, {2, 0}, swordsman);
		const auto& first = f.world.entity(1);
		const auto& second = f.world.entity(2);
		const auto& third = f.world.entity(3);
		require(first.hp == 15 && integerProperty(first.properties, "strength") == 3,
				"spawn overrides apply to inherited properties");
		for (const auto* unit : {&first, &second, &third})
		{
			require(integerProperty(unit->properties, "agility") == 1
							&& integerProperty(unit->properties, "range") == 3,
					"all units inherit agility and range");
		}
		require(second.hp == 10 && integerProperty(second.properties, "strength") == 1
						&& !third.properties.contains("poison") && !first.properties.contains("poison_chance")
						&& !first.properties.contains("rending_chance"),
				"defaults are independent and local properties do not leak");

		const auto mistyped = replaceOnce(
				xml,
				"<abilities>",
				"<properties><property name=\"strength\" type=\"string\" value=\"1\"/></properties><abilities>");
		rejects([&] { sw::io::loadUnitTemplates(mistyped); }, "base property type change accepted");
	}

	void targetGroupConfigurationTests()
	{
		const std::string xml = R"xml(<unit name="test">
      <properties>
        <property name="hp" type="int" value="10"/>
        <property name="power" type="int" value="3"/>
        <property name="range" type="int" value="2"/>
        <property name="otherRange" type="int" value="2"/>
      </properties>
      <abilities>
        <ability name="first" implementation="attack">
          <properties>
            <property name="kind" type="string" value="ranged"/>
            <property name="minRange" type="int" value="1"/>
            <property name="maxRangeStat" type="string" value="range"/>
            <property name="targetGroup" type="string" value="shared"/>
            <property name="damageStat" type="string" value="power"/>
          </properties>
        </ability>
        <ability name="second" implementation="attack">
          <properties>
            <property name="kind" type="string" value="ranged"/>
            <property name="minRange" type="int" value="1"/>
            <property name="maxRangeStat" type="string" value="range"/>
            <property name="targetGroup" type="string" value="shared"/>
            <property name="damageStat" type="string" value="power"/>
          </properties>
        </ability>
      </abilities>
    </unit>)xml";
		const auto units = sw::io::loadUnitTemplates(xml);
		require(units.at("test").abilities.size() == 2, "matching target group rejected");
		const auto checkMismatch = [](const std::string& badXml, std::string_view parameter)
		{
			const std::array documents{sw::io::UnitXmlDocument{"group.xml", badXml}};
			try
			{
				sw::io::loadUnitTemplates(documents);
			}
			catch (const std::runtime_error& error)
			{
				const std::string_view message(error.what());
				const std::array<std::string_view, 6> fragments{
						"group.xml", "unit 'test'", "'targetGroup' = 'shared'", "'first'", "'second'", parameter};
				require(std::ranges::all_of(
								fragments,
								[&](auto fragment) { return message.find(fragment) != std::string_view::npos; }),
						"incomplete group diagnostic: " + std::string(message));
				return;
			}
			throw std::runtime_error("incompatible target group accepted");
		};
		const std::string maximum = R"(<property name="maxRangeStat" type="string" value="range"/>)";
		checkMismatch(replaceOnce(xml, "value=\"ranged\"", "value=\"melee\""), "'kind'");
		checkMismatch(
				replaceOnce(xml, R"(name="minRange" type="int" value="1")", R"(name="minRange" type="int" value="2")"),
				"'minRange'");
		checkMismatch(
				replaceOnce(xml, maximum, R"(<property name="maxRangeStat" type="string" value="otherRange"/>)"),
				"'maxRangeStat'");
		checkMismatch(
				replaceOnce(xml, maximum, R"(<property name="maxRange" type="int" value="2"/>)"), "'maxRangeStat'");
		checkMismatch(
				replaceOnce(xml, maximum, maximum + R"(<property name="noAdjacent" type="bool" value="true"/>)"),
				"'noAdjacent'");
		const auto staticRange = replaceOnce(
				replaceOnce(xml, maximum, R"(<property name="maxRange" type="int" value="2"/>)"),
				maximum,
				R"(<property name="maxRange" type="int" value="3"/>)");
		checkMismatch(staticRange, "'maxRange'");
		rejects([&] { sw::io::loadUnitTemplates(replaceOnce(xml, "value=\"shared\"", "value=\"  \"")); },
				"explicit empty group accepted");
	}

	void configuredProbeTests()
	{
		Fixture f;
		{
			const auto templates = sw::io::loadUnitTemplatesFile(SW_TEST_UNIT_DIR "/configured_probe.xml");
			const auto& probe = templates.at("probe");
			require(probe.abilities.size() == 2 && probe.defaults.empty(), "probe template structure");
			f.world.spawn(1, {0, 0}, probe);
		}

		const auto& probe = f.world.entity(1);
		require(!probe.properties.contains("markerText"), "ability property leaked to the unit");
		f.sink.events.clear();
		f.scheduler.step();
		require(f.sink.events.size() == 1 && f.sink.events.front().kind == Event::Kind::AbilityUsed
						&& f.sink.events.front().name == "first marker",
				"the first configured ability did not take priority");
		ActivationContext context{f.world, 1, f.random, {}};
		const auto second = probe.ability(1).propose(context);
		require(second.status == AbilityProposal::Status::Ready && second.action, "second probe ability unavailable");
		second.action->execute(f.world);
		require(f.sink.events.size() == 2 && f.sink.events.back().kind == Event::Kind::AbilityUsed
						&& f.sink.events.back().name == "second marker",
				"the second probe did not keep its own marker");
	}

	void creationAndSchedulerTests()
	{
		Fixture f;
		f.world.spawn(
				90,
				{1, 1},
				f.units.at("swordsman"),
				{{"rending_chance", std::int64_t(0)}, {"strength", std::int64_t(3)}});
		f.world.spawn(
				1,
				{2, 1},
				f.units.at("swordsman"),
				{{"rending_chance", std::int64_t(0)}, {"strength", std::int64_t(2)}});
		require(!f.world.entity(90).properties.contains("hp"), "duplicate health storage");
		auto before = f.sink.events.size();
		const auto rngBefore = f.random.calls;
		require(!f.world.finished() && !f.world.finished(), "live combat finished");
		require(f.random.calls == rngBefore && f.sink.events.size() == before, "finished changes state/RNG");
		f.scheduler.step();
		require(f.world.tick() == 1 && f.world.entity(1).hp == 7 && f.world.entity(90).hp == 10,
				"first step must execute exactly first-created actor");
		f.scheduler.step();
		require(f.world.tick() == 2 && f.world.entity(90).hp == 8, "second actor did not act");
		rejects([&] { f.passive(90, {4, 4}); }, "duplicate ID accepted");
		rejects([&] { f.passive(3, {1, 1}); }, "occupied cell accepted");
		rejects([&] { f.passive(4, {8, 0}); }, "outside cell accepted");
		rejects([&] { f.passive(5, {4, 4}, 0); }, "zero health accepted");
		rejects([&] { f.world.spawn(6, {4, 4}, f.units.at("swordsman"), {{"rending_chance", std::int64_t(1001)}}); },
				"invalid override chance accepted");
		require(!f.world.isAlive(6), "failed ability validation left a unit in the world");
		f.world.spawn(6, {4, 4}, f.units.at("hunter"), {{"range", std::int64_t{1}}});
		require(f.world.isAlive(6), "short ranged hunter override must still allow spawning");
	}

	void movementTests()
	{
		Fixture f;
		auto mover = f.units.at("swordsman");
		mover.abilities.erase(mover.abilities.begin(), mover.abilities.end() - 1);
		f.world.spawn(1, {0, 0}, mover);
		f.passive(2, {1, 0});
		f.passive(3, {1, 1});
		f.world.march(1, {3, 0});
		require(f.world.nextPosition(1) == Position{0, 1}, "BFS must route around occupied diagonal");
		for (int i = 0; i < 30 && !f.world.finished(); ++i)
		{
			f.scheduler.step();
		}
		require(f.world.finished() && f.world.entity(1).position == Position{3, 0}, "march failed to finish");
		require(std::ranges::count_if(f.sink.events, [](const auto& e) { return e.kind == Event::Kind::MarchEnded; })
						== 1,
				"march completion must emit once");
		f.world.march(1, {1, 0});
		require(!f.world.finished() && f.world.nextPosition(1) == Position{2, 0},
				"march to an occupied cell must approach the closest reachable cell");
		require(f.scheduler.run() == RunResult::Quiescent && f.world.entity(1).position == Position{2, 0}
						&& f.world.entity(1).march == Position{1, 0},
				"blocked march must stop next to the occupied destination and keep the order");
		f.world.damage({1, 2, 20, {}});
		require(!f.world.finished(), "freed destination should enable pending march");
		f.scheduler.run();
		require(f.world.entity(1).position == Position{1, 0}, "dead target did not release cell");
		Fixture diagonal;
		diagonal.world.spawn(1, {0, 0}, mover);
		diagonal.world.march(1, {1, 1});
		diagonal.scheduler.step();
		require(diagonal.world.entity(1).position == Position{1, 1}, "diagonal one-cell move failed");
		diagonal.world.march(1, {1, 1});
		require(diagonal.world.finished(), "already-at-destination march loops");

		Fixture charge;
		charge.world.spawn(1, {0, 0}, charge.units.at("swordsman"), {{"rending_chance", std::int64_t(0)}});
		charge.world.spawn(2, {7, 7}, charge.units.at("swordsman"), {{"rending_chance", std::int64_t(0)}});
		charge.world.march(1, {7, 7});
		require(charge.scheduler.run() == RunResult::Quiescent && charge.world.isAlive(1) != charge.world.isAlive(2),
				"a unit marching onto an enemy cell must approach it and fight");
	}

	void spatialTargetingTests()
	{
		Fixture f;
		f.world.spawn(1, {0, 0}, f.units.at("hunter"), {{"poison_chance", std::int64_t(0)}});
		UnitTemplate crow{"crow", {{"hp", std::int64_t(20)}}, {}};
		crow.targetProfile = {Layer::Air, false, false, 1};
		f.world.spawn(2, {1, 0}, crow);

		const TargetQuery adjacentRanged{2, 2, true, AttackKind::Ranged};
		require(f.world.targets(1, adjacentRanged) == std::vector<EntityId>{2},
				"a ranged query reduced to one cell must still hit a crow");
		require(f.world.targets(1, TargetQuery{}).empty(), "melee selected a crow");
		require(f.world.targets(1, TargetQuery{1, 1, false, AttackKind::Ranged}).empty(),
				"an empty adjusted range must not be widened");
		f.scheduler.step();
		require(f.world.entity(2).hp == 19, "hunter could not shoot the adjacent crow");

		f.world.spawn(3, {0, 0}, crow);
		f.passive(4, {4, 0});
		const TargetQuery ranged{2, 5, true, AttackKind::Ranged};
		require(f.world.targets(1, ranged) == std::vector<EntityId>{2, 4},
				"a crow in the shooter cell must neither be selected nor block the shot");
		f.passive(5, {1, 1});
		require(f.world.targets(1, ranged).empty(), "a living ground neighbor did not block all shots");
		f.world.damage({1, 5, 100, {}});
		require(f.world.targets(1, ranged) == std::vector<EntityId>{2, 4}, "a dead neighbor blocked shots");

		UnitTemplate invulnerable{"invulnerable", {{"hp", std::int64_t(20)}}, {}};
		invulnerable.targetProfile.invulnerable = true;
		f.world.spawn(6, {1, 0}, invulnerable);
		require(f.world.targets(1, ranged).empty(), "an invulnerable ground neighbor did not block shots");
	}

	void footprintAndInvulnerabilityTests()
	{
		Fixture f;
		UnitTemplate tower{"tower", {{"hp", std::int64_t(20)}}, {}};
		tower.footprint = {2, 2};
		tower.targetProfile.invulnerable = true;
		rejects([&] { f.world.spawn(2, {7, 6}, tower); }, "tower crosses right edge");
		rejects([&] { f.world.spawn(2, {6, 7}, tower); }, "tower crosses bottom edge");
		f.world.spawn(2, {6, 6}, tower);
		rejects([&] { f.passive(3, {7, 7}); }, "tower only blocks its anchor");
		rejects([&] { f.world.createMap(7, 8); }, "resized map cuts through tower");

		f.world.spawn(1, {5, 7}, f.units.at("swordsman"));
		const auto& sword = f.world.entity(1);
		const auto& builtTower = f.world.entity(2);
		require(sword.footprint.distanceTo(sword.position, builtTower.footprint, builtTower.position) == 1
						&& builtTower.footprint.distanceTo(builtTower.position, sword.footprint, sword.position) == 1,
				"distance must be symmetric and use the closest footprint cells");
		require(f.world.targets(1, TargetQuery{}).empty()
						&& f.world.targets(1, TargetQuery{1, 1, false, AttackKind::Ranged}).empty(),
				"invulnerable tower selected for attack");
		f.sink.events.clear();
		f.world.damage({1, 2, 100, {}});
		require(builtTower.hp == 20 && f.sink.events.empty(), "damage or events reached an invulnerable target");
		require(f.world.finished(), "invulnerable opponent kept battle alive");
		f.scheduler.run();
		require(f.world.tick() == 0, "finished battle executed a turn");
	}

	void spatialMovementTests()
	{
		Fixture f;
		f.world.createMap(5, 1);
		auto crow = f.units.at("swordsman");
		crow.abilities.erase(crow.abilities.begin(), crow.abilities.end() - 1);
		crow.abilities.front().properties.at("cells") = std::int64_t{2};
		crow.targetProfile.layer = Layer::Air;
		f.world.spawn(1, {0, 0}, crow);
		f.world.spawn(2, {2, 0}, crow);
		f.passive(3, {1, 0});
		f.passive(4, {2, 0});
		f.world.march(1, {2, 0});
		f.sink.events.clear();
		require(f.world.movementPath(1, 2) == std::vector<Position>{{1, 0}, {2, 0}},
				"air path must cross other layers and air neighbors");
		f.scheduler.step();
		require(f.world.tick() == 1 && f.world.entity(1).position == Position{2, 0} && !f.world.entity(1).march,
				"two air steps must finish the march in one turn");
		require(f.sink.events.size() == 2 && f.sink.events[0].kind == Event::Kind::Moved
						&& f.sink.events[0].position == Position{2, 0} && f.sink.events[0].tick == 1
						&& f.sink.events[1].kind == Event::Kind::MarchEnded && f.sink.events[1].tick == 1,
				"two steps must emit only final movement and one march completion");

		Fixture ground;
		ground.world.createMap(5, 2);
		auto large = crow;
		large.targetProfile.layer = Layer::Ground;
		large.footprint = {2, 2};
		ground.world.spawn(1, {0, 0}, large);
		ground.passive(2, {2, 1});
		ground.world.march(1, {3, 0});
		require(ground.world.finished() && ground.world.movementPath(1, 2).empty(),
				"non-anchor part of footprint must block the path");
		ground.world.damage({1, 2, 100, {}});
		ground.scheduler.step();
		require(ground.world.entity(1).position == Position{2, 0} && ground.world.entity(1).march == Position{3, 0},
				"large mover must overlap its old footprint and stop after two steps");
		ground.scheduler.step();
		require(ground.world.entity(1).position == Position{3, 0} && ground.world.finished(),
				"large mover must stop at the last valid anchor and finish");
	}

	void damageTests()
	{
		Fixture f;
		f.passive(1, {0, 0});
		f.passive(2, {3, 0}, 10);
		f.sink.events.clear();
		f.world.beginOperation();
		f.world.damage({1, 2, 3, {}});
		f.world.damage({1, 2, 100, {}});
		f.world.damage({1, 2, 1, {}});
		f.world.endOperation();
		require(f.sink.events.size() == 2, "damage must aggregate and death be unique");
		require(f.sink.events[0].kind == Event::Kind::Attacked && f.sink.events[0].damage == 10
						&& f.sink.events[0].hp == 0,
				"aggregated damage is actual HP loss");
		require(f.sink.events[1].kind == Event::Kind::Died, "death must follow attack");
		f.passive(3, {3, 0});
		require(f.world.entity(3).alive(), "dead entity still occupies cell");
	}

	void registryTests()
	{
		const auto first = [](const AbilityConfig&, const PropertyMap&) -> std::unique_ptr<IAbility>
		{
			return {};
		};
		const auto duplicate = [](const AbilityConfig&, const PropertyMap&) -> std::unique_ptr<IAbility>
		{
			throw std::runtime_error("duplicate factory used");
		};
		AbilityManager local;
		const auto firstLocation = std::source_location::current();
		require(local.registerAbility("example", first, firstLocation), "first registration rejected");
		const auto duplicateLocation = std::source_location::current();
		require(!local.registerAbility("example", duplicate, duplicateLocation), "duplicate registration accepted");
		require(!local.registerAbility("", first), "empty name accepted");
		require(!local.registerAbility("null", nullptr), "empty factory accepted");
		const auto& errors = local.validateAndFreeze();
		require(errors.size() == 3, "expected exactly one diagnostic per failed registration");
		require(errors[0].find("Duplicate ability registration 'example'") != std::string::npos
						&& errors[0].find(
								   std::string(firstLocation.file_name()) + ":" + std::to_string(firstLocation.line()))
								   != std::string::npos
						&& errors[0].find(
								   std::string(duplicateLocation.file_name()) + ":"
								   + std::to_string(duplicateLocation.line()))
								   != std::string::npos,
				"duplicate diagnostic must contain both source locations");
		require(errors[1].find("Empty ability name") != std::string::npos
						&& errors[2].find("Empty ability factory for 'null'") != std::string::npos,
				"wrong registry diagnostics");
		require(!local.registerAbility("late", first) && errors.size() == 4
						&& errors.back().find("Late ability registration") != std::string::npos,
				"registry stayed writable after validation");
		AbilityConfig example;
		example.implementation = "example";
		std::string nullResultError;
		try
		{
			local.create(example, {});
		}
		catch (const std::runtime_error& error)
		{
			nullResultError = error.what();
		}
		require(nullResultError.find("Ability factory for 'example' returned nullptr") != std::string::npos,
				"missing diagnostic for a factory returning nullptr");
		AbilityConfig unknown;
		unknown.implementation = "late";
		rejects([&] { local.create(unknown, {}); }, "late factory was installed");
		unknown.implementation = "missing";
		rejects([&] { AbilityManager::instance()->create(unknown, {}); }, "unknown action silently accepted");
	}
}

int main()
{
	try
	{
		require(AbilityManager::instance()->validateAndFreeze().empty(), "linked registry contains errors");
		configurationTests();
		basePropertyTests();
		targetGroupConfigurationTests();
		configuredProbeTests();
		creationAndSchedulerTests();
		movementTests();
		spatialTargetingTests();
		footprintAndInvulnerabilityTests();
		spatialMovementTests();
		damageTests();
		registryTests();
		runFeatureTests();
		runTerminationTests();
		std::cout << "All battle contracts passed\n";
	}
	catch (const std::exception& error)
	{
		std::cerr << "Test failed: " << error.what() << '\n';
		return 1;
	}
}
