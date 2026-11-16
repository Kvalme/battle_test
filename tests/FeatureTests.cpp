#include "Core/ActivationContext.hpp"
#include "Core/Damage.hpp"
#include "Core/Entity.hpp"
#include "Core/IRandom.hpp"
#include "Core/Scheduler.hpp"
#include "Core/World.hpp"
#include "IO/Config/UnitLoader.hpp"
#include "UnitsXml.hpp"

#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace
{

	using namespace sw::core;
	using Kind = Event::Kind;
	using Status = AbilityProposal::Status;

	static_assert(std::is_same_v<decltype(std::declval<const Entity&>().effect(0)), const IEffect&>);

	void check(bool condition, const char* message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	class RecordingSink final : public IEventSink
	{
	public:
		void emit(const Event& event) override
		{
			events.push_back(event);
		}

		std::vector<Event> events;
	};

	class ScriptedRandom final : public IRandom
	{
	public:
		std::uint32_t uniform(std::uint32_t min, std::uint32_t max) override
		{
			const bool chance = min == 1 && max == 1000;
			calls.emplace_back(min, max);
			return chance ? roll : (pickLastTarget ? max : min);
		}

		std::uint32_t roll{1000};
		bool pickLastTarget{};
		std::vector<std::pair<std::uint32_t, std::uint32_t>> calls;
	};

	class Battle
	{
	public:
		explicit Battle(const UnitTemplates& templates) :
				templates_(templates)
		{
			world.createMap(12, 12);
		}

		void hunter(
				EntityId id,
				Position position,
				std::int64_t poisonChance = 1000,
				std::int64_t poison = 12,
				std::int64_t hp = 100)
		{
			world.spawn(
					id,
					position,
					templates_.at("hunter"),
					{{"hp", hp},
					 {"poison_chance", poisonChance},
					 {"poison", poison},
					 {"strength", std::int64_t{6}},
					 {"agility", std::int64_t{7}}});
		}

		void swordsman(EntityId id, Position position, std::int64_t rending = 5, std::int64_t strength = 1)
		{
			world.spawn(
					id,
					position,
					templates_.at("swordsman"),
					{{"hp", std::int64_t{100}},
					 {"rending_chance", std::int64_t{1000}},
					 {"rending", rending},
					 {"strength", strength}});
		}

		void target(EntityId id, Position position, std::int64_t hp = 100)
		{
			world.spawn(id, position, {"target", {{"hp", hp}}, {}});
		}

		const IAbility& ability(EntityId id, std::string_view name) const
		{
			const auto& unit = world.entity(id);
			const auto& abilities = templates_.at(unit.type).abilities;
			const auto config = std::ranges::find_if(
					abilities,
					[name](const AbilityConfig& entry)
					{ return (entry.name.empty() ? entry.implementation : entry.name) == name; });
			check(config != abilities.end(), "Missing ability in test template");
			return unit.ability(static_cast<std::size_t>(config - abilities.begin()));
		}

		void use(EntityId id, std::string_view name)
		{
			ActivationContext context{world, id, random, {}};
			execute(ability(id, name).propose(context));
		}

		void execute(AbilityProposal proposal)
		{
			check(proposal.status == Status::Ready && proposal.action, "Expected a ready attack");
			world.beginOperation();
			proposal.action->execute(world);
			world.endOperation();
		}

		void expectDamage(EntityId source, EntityId target, std::uint32_t damage, std::uint32_t hp)
		{
			const auto hit = std::ranges::find(sink.events, Kind::Attacked, &Event::kind);
			check(hit != sink.events.end() && hit->source == source && hit->target == target && hit->damage == damage
						  && hit->hp == hp,
				  "Unexpected attack event or damage attribution");
			check(std::ranges::count(sink.events, Kind::Attacked, &Event::kind) == 1,
				  "Expected one attack event per operation");
		}

		void expectPoisonTick(EntityId source, EntityId target, std::uint32_t damage, std::uint32_t hp)
		{
			sink.events.clear();
			world.activateEffects(target);
			expectDamage(source, target, damage, hp);
		}

		void expectTwoPoisonTicks(
				EntityId first,
				std::uint32_t firstDamage,
				EntityId second,
				std::uint32_t secondDamage,
				EntityId target,
				std::uint32_t hp)
		{
			sink.events.clear();
			world.activateEffects(target);
			check(sink.events.size() == 2, "Expected one attack from each poison source");
			const auto& firstHit = sink.events[0];
			const auto& secondHit = sink.events[1];
			check(firstHit.kind == Kind::Attacked && firstHit.source == first && firstHit.target == target
						  && firstHit.damage == firstDamage && firstHit.hp == hp,
				  "Unexpected first poison attack");
			check(secondHit.kind == Kind::Attacked && secondHit.source == second && secondHit.target == target
						  && secondHit.damage == secondDamage && secondHit.hp == hp,
				  "Unexpected second poison attack");
		}

		RecordingSink sink;
		ScriptedRandom random;
		World world{sink};
		Scheduler scheduler{world, random};

	private:
		const UnitTemplates& templates_;
	};

	void poisonPortions(const UnitTemplates& templates)
	{
		Battle battle(templates);
		battle.hunter(1, {0, 1});
		battle.target(2, {3, 1});
		battle.sink.events.clear();
		battle.use(1, "poison_arrows");
		check(battle.sink.events.size() == 2 && battle.sink.events[0].kind == Kind::AbilityUsed
					  && battle.sink.events[0].name == "poison_arrows",
			  "Poison ability must precede immediate damage");
		battle.expectDamage(1, 2, 3, 97);
		battle.expectPoisonTick(1, 2, 3, 94);
		battle.expectPoisonTick(1, 2, 2, 92);
		battle.expectPoisonTick(1, 2, 2, 90);
		battle.expectPoisonTick(1, 2, 2, 88);
		check(!battle.world.hasEffect(2, "poison"), "Poison must expire after five portions");
		battle.sink.events.clear();
		battle.world.activateEffects(2);
		check(battle.sink.events.empty(), "Expired poison must not deal damage");
		battle.use(1, "poison_arrows");
		battle.expectDamage(1, 2, 3, 85);
	}

	void lowPoisonAmounts(const UnitTemplates& templates)
	{
		{
			Battle battle(templates);
			battle.hunter(1, {0, 1}, 1000, 3);
			battle.target(2, {3, 1});
			battle.use(1, "poison_arrows");
			battle.expectDamage(1, 2, 1, 99);
			battle.expectPoisonTick(1, 2, 1, 98);
			battle.expectPoisonTick(1, 2, 1, 97);
			battle.sink.events.clear();
			battle.world.activateEffects(2);
			check(battle.sink.events.empty() && battle.world.hasEffect(2, "poison"),
				  "The first zero portion must spend one activation without an attack");
			battle.world.activateEffects(2);
			check(battle.sink.events.empty() && !battle.world.hasEffect(2, "poison"),
				  "Poison must expire after the second zero portion");
		}
		{
			Battle battle(templates);
			battle.hunter(1, {0, 1}, 1000, 0);
			battle.target(2, {3, 1});
			const auto& poison = battle.ability(1, "poison_arrows");
			ActivationContext context{battle.world, 1, battle.random, {}};
			battle.sink.events.clear();
			check(!poison.canAct(battle.world, 1) && poison.propose(context).status == Status::NotApplicable
						  && battle.random.calls.empty() && battle.sink.events.empty(),
				  "Zero poison must be inapplicable without consuming RNG or emitting events");
			battle.scheduler.step();
			battle.expectDamage(1, 2, 7, 93);
		}
	}

	void rendingInteraction(const UnitTemplates& templates)
	{
		{
			Battle battle(templates);
			battle.hunter(1, {0, 1});
			battle.target(2, {3, 1});
			battle.swordsman(3, {4, 1});
			battle.sink.events.clear();
			battle.use(3, "rending");
			check(battle.sink.events.size() == 2 && battle.sink.events[0].kind == Kind::AbilityUsed
						  && battle.sink.events[0].name == "rending",
				  "Rending ability must precede its direct damage");
			battle.expectDamage(3, 2, 5, 95);
			battle.use(3, "rending");
			battle.sink.events.clear();
			battle.use(1, "poison_arrows");
			battle.expectDamage(1, 2, 6, 84);
			battle.expectPoisonTick(1, 2, 6, 78);
			check(!battle.world.hasEffect(2, "rending_received"), "Rending must expire after target activation");
			battle.expectPoisonTick(1, 2, 2, 76);
		}
		{
			Battle battle(templates);
			battle.hunter(1, {0, 1});
			battle.target(2, {3, 1});
			battle.swordsman(3, {4, 1});
			battle.use(1, "poison_arrows");
			battle.sink.events.clear();
			battle.use(3, "rending");
			battle.expectDamage(3, 2, 5, 92);
			battle.expectPoisonTick(1, 2, 6, 86);
			battle.expectPoisonTick(1, 2, 2, 84);
		}
	}

	void poisonRefresh(const UnitTemplates& templates)
	{
		Battle battle(templates);
		battle.hunter(9, {0, 1}, 1000, 12, 1);
		battle.target(2, {3, 1});
		battle.hunter(1, {6, 1}, 1000, 30);
		battle.use(9, "poison_arrows");
		battle.expectDamage(9, 2, 3, 97);
		battle.sink.events.clear();
		battle.use(1, "poison_arrows");
		battle.expectDamage(1, 2, 6, 91);
		battle.expectTwoPoisonTicks(9, 3, 1, 6, 2, 82);

		for (int attempt = 0; attempt < 2; ++attempt)
		{
			battle.sink.events.clear();
			battle.use(9, "poison_arrows");
			check(battle.sink.events.size() == 1 && battle.sink.events[0].kind == Kind::AbilityUsed
						  && battle.sink.events[0].source == 9 && battle.sink.events[0].name == "poison_arrows",
				  "Refreshing poison must emit only the original owner's ability use");
		}
		battle.world.damage({2, 9, 1, {}});
		check(!battle.world.isAlive(9), "Expected original poison source to die");
		battle.expectTwoPoisonTicks(9, 2, 1, 6, 2, 74);
		battle.expectTwoPoisonTicks(9, 2, 1, 6, 2, 66);
		battle.expectTwoPoisonTicks(9, 2, 1, 6, 2, 58);
		battle.expectPoisonTick(9, 2, 3, 55);
		battle.expectPoisonTick(9, 2, 3, 52);
		check(!battle.world.hasEffect(2, "poison"), "Refresh must leave exactly five upcoming portions");
	}

	void multipleSourcesWithRending(const UnitTemplates& templates)
	{
		Battle battle(templates);
		battle.hunter(9, {0, 1});
		battle.target(2, {3, 1});
		battle.hunter(1, {6, 1}, 1000, 15);
		battle.swordsman(3, {3, 2}, 0);
		battle.swordsman(4, {4, 2}, 0);
		battle.use(9, "poison_arrows");
		battle.use(1, "poison_arrows");
		battle.use(3, "rending");
		battle.use(4, "rending");
		battle.expectTwoPoisonTicks(9, 6, 1, 6, 2, 82);
		check(!battle.world.hasEffect(2, "rending_received"),
			  "Rending from different attackers must apply once and expire after both ticks");
		battle.expectTwoPoisonTicks(9, 2, 1, 3, 2, 77);
	}

	void zeroDamageRending(const UnitTemplates& templates)
	{
		{
			Battle battle(templates);
			battle.hunter(1, {0, 1});
			battle.target(2, {3, 1});
			battle.swordsman(3, {4, 1}, 0, 0);
			const auto& rending = battle.ability(3, "rending");
			check(!rending.canAct(battle.world, 3), "Wounds alone do not count as combat progress");
			battle.use(1, "poison_arrows");
			check(rending.canAct(battle.world, 3), "Zero-damage Rending can amplify active poison");
			battle.sink.events.clear();
			battle.use(3, "rending");
			check(battle.world.hasEffect(2, "rending_received") && battle.sink.events.size() == 1
						  && battle.sink.events[0].kind == Kind::AbilityUsed,
				  "Zero-damage Rending must apply wounds and publish only ability use");
			battle.expectPoisonTick(1, 2, 6, 91);
		}
		{
			Battle battle(templates);
			battle.target(2, {3, 1});
			battle.swordsman(3, {4, 1}, 0, 0);
			check(battle.world.finished(), "Zero-damage attacks alone must let combat end");
		}
		{
			Battle battle(templates);
			battle.target(2, {3, 1});
			battle.swordsman(3, {4, 1});
			battle.use(3, "rending");
			battle.world.damage({2, 3, 100, {}});
			const auto& target = battle.world.entity(2);
			check(target.effectCount() == 1 && target.effect(0).type == "rending_received" && battle.world.finished(),
				  "Outstanding wounds alone must let combat end");
		}
		{
			Battle battle(templates);
			battle.swordsman(3, {4, 1}, 0);
			battle.target(2, {3, 1}, 1);
			battle.sink.events.clear();
			battle.scheduler.step();
			battle.expectDamage(3, 2, 1, 0);
			check(battle.world.finished(), "Zero-damage Rending must allow a useful melee fallback");
		}
	}

	void poisonDeaths(const UnitTemplates& templates)
	{
		for (const std::int64_t hp : {2, 5})
		{
			Battle battle(templates);
			battle.hunter(1, {0, 1});
			battle.target(2, {3, 1}, hp);
			battle.sink.events.clear();
			battle.use(1, "poison_arrows");
			if (hp == 5)
			{
				check(battle.world.isAlive(2), "First portion must leave this target alive");
				battle.expectPoisonTick(1, 2, 2, 0);
			}
			else
			{
				battle.expectDamage(1, 2, 2, 0);
			}
			check(!battle.world.isAlive(2) && battle.world.entity(2).effectCount() == 0,
				  "Lethal poison must clear all target effects");
			check(std::ranges::count(battle.sink.events, Kind::Died, &Event::kind) == 1
						  && battle.sink.events.back().kind == Kind::Died,
				  "Poison must publish one death after damage");
			battle.sink.events.clear();
			battle.world.activateEffects(2);
			check(battle.sink.events.empty(), "A dead target must not receive further poison");
		}
	}

	void deathFromFirstPoisonSource(const UnitTemplates& templates)
	{
		Battle battle(templates);
		battle.hunter(9, {0, 1});
		battle.target(2, {3, 1}, 8);
		battle.hunter(1, {6, 1}, 1000, 10);
		battle.use(9, "poison_arrows");
		battle.use(1, "poison_arrows");
		battle.expectPoisonTick(9, 2, 3, 0);
		check(battle.sink.events.size() == 2 && battle.sink.events.back().kind == Kind::Died
					  && battle.sink.events.back().target == 2 && battle.world.entity(2).effectCount() == 0,
			  "Death from the first poison must skip the second and clear both effects");
	}

	void targetingAndChance(const UnitTemplates& templates)
	{
		{
			Battle battle(templates);
			battle.hunter(1, {0, 1}, 500);
			battle.target(2, {2, 1});
			battle.target(3, {3, 1});
			battle.random.pickLastTarget = true;
			battle.sink.events.clear();
			const auto& poison = battle.ability(1, "poison_arrows");
			check(poison.canAct(battle.world, 1) && battle.random.calls.empty(),
				  "Checking attack availability must not consume RNG");
			ActivationContext context{battle.world, 1, battle.random, {}};
			const auto special = poison.propose(context);
			check(special.status == Status::NotApplicable && battle.sink.events.empty(),
				  "Failed special chance must leave the world unchanged");
			battle.execute(battle.ability(1, "ranged").propose(context));
			battle.expectDamage(1, 3, 7, 93);
			check(battle.random.calls.size() == 2 && battle.random.calls[0].second - battle.random.calls[0].first == 1
						  && battle.random.calls[1] == std::pair{1u, 1000u},
				  "Choose the target before chance and reuse it for fallback");
		}
		{
			Battle battle(templates);
			battle.hunter(1, {1, 1});
			battle.target(2, {4, 1});
			battle.target(3, {2, 2});
			battle.sink.events.clear();
			battle.scheduler.step();
			battle.expectDamage(1, 3, 6, 94);
			check(std::ranges::count(battle.sink.events, Kind::AbilityUsed, &Event::kind) == 0,
				  "A diagonal neighbor must block all ranged attacks");
		}
		{
			Battle battle(templates);
			battle.hunter(1, {0, 1}, 0);
			battle.target(2, {3, 1});
			battle.random.roll = 1;
			const auto& poison = battle.ability(1, "poison_arrows");
			ActivationContext context{battle.world, 1, battle.random, {}};
			check(!poison.canAct(battle.world, 1) && battle.random.calls.empty(),
				  "Zero chance must not count as progress");
			check(poison.propose(context).status == Status::NotApplicable,
				  "A zero chance special must fail even on the minimum roll");
			battle.sink.events.clear();
			battle.execute(battle.ability(1, "ranged").propose(context));
			battle.expectDamage(1, 2, 7, 93);
			check(battle.random.calls == std::vector{std::pair{0u, 0u}, std::pair{1u, 1000u}},
				  "At zero chance choose the target before rolling and reuse it in fallback");
		}
	}

	void targetGroupSelection(const UnitTemplates& templates)
	{
		{
			Battle battle(templates);
			battle.hunter(1, {0, 1});
			battle.target(2, {1, 1});
			ActivationContext context{battle.world, 1, battle.random, {}};
			check(battle.ability(1, "poison_arrows").propose(context).status == Status::NotApplicable
						  && battle.ability(1, "ranged").propose(context).status == Status::NotApplicable
						  && battle.random.calls.empty(),
				  "Group without candidates must not consume RNG");
			check(context.targetSelections.contains("ranged") && !context.targetSelections.at("ranged"),
				  "The group must remember a search with no candidates");
		}
		{
			auto ungrouped = templates;
			for (auto& ability : ungrouped.at("hunter").abilities)
			{
				ability.properties.erase("targetGroup");
			}
			Battle battle(ungrouped);
			battle.hunter(1, {0, 1}, 500);
			battle.target(2, {2, 1});
			battle.target(3, {3, 1});
			ActivationContext context{battle.world, 1, battle.random, {}};
			check(battle.ability(1, "poison_arrows").propose(context).status == Status::NotApplicable,
				  "Special attack should fail its chance roll");
			battle.random.pickLastTarget = true;
			battle.sink.events.clear();
			battle.execute(battle.ability(1, "ranged").propose(context));
			battle.expectDamage(1, 3, 7, 93);
			check(battle.random.calls == std::vector{std::pair{0u, 1u}, std::pair{1u, 1000u}, std::pair{0u, 1u}},
				  "Ungrouped matching queries must independently select a target");
		}
	}

}

void runFeatureTests()
{
	const auto templates = sw::io::loadUnitTemplates(sw::app::unitsXml);
	poisonPortions(templates);
	lowPoisonAmounts(templates);
	rendingInteraction(templates);
	zeroDamageRending(templates);
	poisonRefresh(templates);
	multipleSourcesWithRending(templates);
	poisonDeaths(templates);
	deathFromFirstPoisonSource(templates);
	targetingAndChance(templates);
	targetGroupSelection(templates);
}
