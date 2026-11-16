#include "Core/Abilities.hpp"
#include "Core/AbilityManager.hpp"
#include "Core/ActivationContext.hpp"
#include "Core/Config.hpp"
#include "Core/Damage.hpp"
#include "Core/Entity.hpp"
#include "Core/IRandom.hpp"
#include "Core/Scheduler.hpp"
#include "Core/World.hpp"
#include "IO/Config/UnitLoader.hpp"
#include "UnitsXml.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace
{
	using namespace sw::core;

	void require(bool condition, const char* message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	struct Sink final : IEventSink
	{
		std::vector<Event> events;

		void emit(const Event& event) override
		{
			events.push_back(event);
		}
	};

	struct ScriptedRandom final : IRandom
	{
		unsigned targets{};
		unsigned chances{};
		unsigned misses{};

		std::uint32_t uniform(std::uint32_t min, std::uint32_t max) override
		{
			if (min == 1 && max == 1000)
			{
				return ++chances <= misses ? max : min;
			}
			++targets;
			return min;
		}
	};

	struct Arena
	{
		Sink sink;
		ScriptedRandom random;
		World world{sink};
		Scheduler scheduler{world, random};
		UnitTemplates units = sw::io::loadUnitTemplates(sw::app::unitsXml);

		Arena(std::uint32_t width = 4, std::uint32_t height = 1)
		{
			world.createMap(width, height);
		}

		void passive(EntityId id, Position position, std::int64_t hp = 20)
		{
			world.spawn(id, position, UnitTemplate{"passive", {{"hp", hp}}, {}});
		}

		void runner(EntityId id, Position position, std::string factory = "pacing")
		{
			AbilityConfig action;
			action.implementation = std::move(factory);
			world.spawn(id, position, UnitTemplate{"runner", {{"hp", std::int64_t(20)}}, {action}});
		}
	};

	class PacingAction final : public IAction
	{
	public:
		PacingAction(EntityId actor, Position destination) :
				actor_(actor),
				destination_(destination)
		{}

		void execute(World& world) override
		{
			world.move(actor_, destination_);
		}

	private:
		EntityId actor_;
		Position destination_;
	};

	class Pacing final : public IAbility
	{
	public:
		Pacing(std::optional<unsigned> steps, unsigned period, std::size_t padding, bool supportsCycle) :
				steps_(steps),
				period_(period),
				padding_(padding),
				supportsCycle_(supportsCycle)
		{}

		bool canAct(const World&, EntityId) const override
		{
			return steps_ != 0;
		}

		AbilityProposal propose(ActivationContext& context) const override
		{
			if (!canAct(context.world, context.actor))
			{
				return {};
			}
			if (steps_)
			{
				--*steps_;
			}
			phase_ = (phase_ + 1) % period_;
			const auto position = context.world.entity(context.actor).position;
			const Position destination{1 - position.x, position.y};
			return {AbilityProposal::Status::Ready, std::make_unique<PacingAction>(context.actor, destination)};
		}

		std::optional<std::string> cycleState() const override
		{
			if (!supportsCycle_)
			{
				return std::nullopt;
			}
			return std::to_string(steps_.value_or(0)) + ":" + std::to_string(phase_) + std::string(padding_, ' ');
		}

	private:
		mutable std::optional<unsigned> steps_;
		unsigned period_;
		mutable unsigned phase_{};
		std::size_t padding_;
		bool supportsCycle_;
	};

	const struct RegisterPacing
	{
		RegisterPacing()
		{
			auto& registry = *AbilityManager::instance();
			registry.registerAbility(
					"pacing",
					[](const AbilityConfig&, const PropertyMap&)
					{ return std::make_unique<Pacing>(std::nullopt, 1, 0, true); });
			registry.registerAbility(
					"countdown_pacing",
					[](const AbilityConfig&, const PropertyMap&) { return std::make_unique<Pacing>(3, 1, 0, true); });
			registry.registerAbility(
					"long_pacing",
					[](const AbilityConfig&, const PropertyMap&)
					{ return std::make_unique<Pacing>(std::nullopt, 300, 0, true); });
			registry.registerAbility(
					"wide_pacing",
					[](const AbilityConfig&, const PropertyMap&)
					{ return std::make_unique<Pacing>(std::nullopt, 32, 65'536, true); });
			registry.registerAbility(
					"unknown_pacing",
					[](const AbilityConfig&, const PropertyMap&)
					{ return std::make_unique<Pacing>(std::nullopt, 1, 0, false); });
		}
	} registerPacing;

	void lateRandomSuccess()
	{
		Arena arena;
		arena.random.misses = 9;
		arena.world.spawn(
				1,
				{0, 0},
				arena.units.at("swordsman"),
				{{"rending_chance", std::int64_t(100)}, {"strength", std::int64_t(0)}, {"rending", std::int64_t(4)}});
		arena.passive(2, {1, 0}, 4);
		arena.sink.events.clear();
		require(!arena.world.finished() && !arena.world.finished(), "repeated quiescence check missed viable chance");
		require(arena.random.chances == 0 && arena.random.targets == 0, "quiescence check used the RNG");
		require(arena.world.tick() == 0 && arena.world.entity(2).hp == 4 && arena.sink.events.empty(),
				"quiescence check changed simulation state");

		require(arena.scheduler.run(18) == RunResult::ActivationLimit && arena.world.tick() == 18,
				"failed random attacks were classified as a cycle");
		require(arena.random.chances == 9 && arena.random.targets == 9 && arena.sink.events.empty(),
				"chance or target draws changed during failed attacks");

		require(arena.scheduler.run(1) == RunResult::Quiescent && arena.world.tick() == 19,
				"success on the final permitted activation must stop normally");
		require(arena.random.chances == 10 && arena.random.targets == 10 && !arena.world.isAlive(2),
				"the tenth chance draw should have killed the target");
	}

	void exactCycles()
	{
		Arena loop(4, 2);
		loop.runner(1, {0, 0});
		loop.runner(2, {0, 1});
		loop.passive(3, {3, 0});
		loop.passive(4, {3, 1});
		loop.world.damage({1, 3, 20, {}});
		loop.world.damage({1, 4, 20, {}});
		loop.sink.events.clear();
		require(loop.scheduler.run(4) == RunResult::Cycle && loop.world.tick() == 4,
				"true cycle at the limit was missed when a round had trailing dead slots");
		require(loop.sink.events.size() == 4 && loop.sink.events.back().source == 2
						&& loop.world.entity(1).position == Position{0, 0}
						&& loop.world.entity(2).position == Position{0, 1},
				"the scheduler advanced past the repeated full-round state");

		Arena partial(4, 2);
		partial.runner(1, {0, 0});
		partial.runner(2, {0, 1});
		partial.scheduler.step();
		require(partial.scheduler.run(5) == RunResult::Cycle && partial.world.tick() == 6,
				"a partial-round snapshot was compared with a full-round boundary");

		Arena countdown;
		countdown.runner(1, {0, 0}, "countdown_pacing");
		require(countdown.scheduler.run(3) == RunResult::Quiescent && countdown.world.tick() == 3,
				"same positions with different private state were misclassified as a cycle");

		Arena unknown;
		unknown.runner(1, {0, 0}, "unknown_pacing");
		require(unknown.scheduler.run(4) == RunResult::ActivationLimit && unknown.world.tick() == 4,
				"an ability without complete cycle state was classified as a cycle");
	}

	void boundedHistory()
	{
		Arena manyStates;
		manyStates.runner(1, {0, 0}, "long_pacing");
		require(manyStates.scheduler.run(601) == RunResult::ActivationLimit && manyStates.world.tick() == 601,
				"cycle history retained more than 256 snapshots or stopped on overflow");

		Arena wideStates;
		wideStates.runner(1, {0, 0}, "wide_pacing");
		require(wideStates.scheduler.run(65) == RunResult::ActivationLimit && wideStates.world.tick() == 65,
				"cycle history retained more than one MiB of snapshot data or stopped on overflow");
	}

	void blockedMarches()
	{
		Arena blocked(2);
		auto mover = blocked.units.at("swordsman");
		mover.abilities = {mover.abilities.back()};
		blocked.world.spawn(1, {0, 0}, mover);
		blocked.world.spawn(2, {1, 0}, mover);
		blocked.world.march(1, {1, 0});
		blocked.world.march(2, {0, 0});
		require(blocked.scheduler.run(0) == RunResult::Quiescent && blocked.world.tick() == 0
						&& blocked.world.entity(1).march == Position{1, 0}
						&& blocked.world.entity(2).march == Position{0, 0},
				"mutually blocked marches should stop without dropping orders");

		Arena shifting;
		shifting.world.spawn(1, {0, 0}, mover);
		shifting.world.spawn(2, {1, 0}, mover);
		shifting.world.spawn(3, {2, 0}, mover);
		shifting.world.march(1, {1, 0});
		shifting.world.march(2, {2, 0});
		shifting.world.march(3, {3, 0});
		require(shifting.scheduler.run(7) == RunResult::Quiescent && shifting.world.tick() == 7
						&& shifting.world.entity(1).position == Position{1, 0}
						&& shifting.world.entity(2).position == Position{2, 0}
						&& shifting.world.entity(3).position == Position{3, 0},
				"should continue when a third unit can unblock the march");
	}

	class FatalEffect final : public IEffect
	{
	public:
		FatalEffect(EntityId target) :
				IEffect("fatal", target, target, 1)
		{}

		void onActivation(World& world) override
		{
			world.damage({source, target, 100, {}});
		}
	};

	void effectLifetimes()
	{
		Arena poisoned;
		poisoned.world.spawn(
				1,
				{0, 0},
				poisoned.units.at("hunter"),
				{{"poison_chance", std::int64_t(1000)}, {"poison", std::int64_t(3)}});
		poisoned.passive(2, {3, 0});
		poisoned.scheduler.step();
		poisoned.world.damage({2, 1, 100, {}});
		require(!poisoned.world.finished() && poisoned.world.hasEffect(2, "poison"),
				"poison from a dead source must keep the simulation active");
		require(poisoned.scheduler.run(4) == RunResult::Quiescent && poisoned.world.tick() == 5
						&& poisoned.world.entity(2).hp == 17 && !poisoned.world.hasEffect(2, "poison"),
				"zero-damage installments must still be processed before poisoning expires");

		Arena marked;
		marked.world.spawn(1, {0, 0}, marked.units.at("swordsman"), {{"rending_chance", std::int64_t(1000)}});
		marked.passive(2, {1, 0});
		marked.scheduler.step();
		marked.world.damage({2, 1, 100, {}});
		require(marked.scheduler.run(0) == RunResult::Quiescent && marked.world.tick() == 1
						&& marked.world.hasEffect(2, "rending_received"),
				"a wound marker alone must not extend combat");

		Arena fatal;
		fatal.runner(1, {0, 0});
		fatal.world.applyEffect(std::make_unique<FatalEffect>(1));
		fatal.sink.events.clear();
		require(fatal.scheduler.run(1) == RunResult::Quiescent && fatal.world.tick() == 1 && !fatal.world.isAlive(1),
				"fatal effect must use the activation and then end the world at the limit");
		require(std::ranges::none_of(
						fatal.sink.events, [](const Event& event) { return event.kind == Event::Kind::Moved; }),
				"a unit dying at the start of an activation must not act");
	}

}  // namespace

void runTerminationTests()
{
	lateRandomSuccess();
	exactCycles();
	boundedHistory();
	blockedMarches();
	effectLifetimes();
}
