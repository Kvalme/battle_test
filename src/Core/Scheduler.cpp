#include "Scheduler.hpp"

#include "IRandom.hpp"
#include "World.hpp"

#include <algorithm>
#include <deque>
#include <string>
#include <utility>

namespace sw::core
{
	namespace
	{

		constexpr std::size_t maxCycleStates = 256;
		constexpr std::size_t maxCycleBytes = World::maxSnapshotBytes;

		class TrackingRandom final : public IRandom
		{
		public:
			explicit TrackingRandom(IRandom& random) :
					random_(random)
			{}

			std::uint32_t uniform(std::uint32_t min, std::uint32_t max) override
			{
				used = true;
				return random_.uniform(min, max);
			}

			bool used{};

		private:
			IRandom& random_;
		};

		class CycleHistory
		{
		public:
			bool repeats(std::string state)
			{
				if (std::ranges::find(states_, state) != states_.end())
				{
					return true;
				}
				const auto bytes = state.capacity();
				if (bytes > maxCycleBytes)
				{
					return false;
				}
				while (states_.size() == maxCycleStates || storedBytes_ + bytes > maxCycleBytes)
				{
					storedBytes_ -= states_.front().capacity();
					states_.pop_front();
				}
				states_.push_back(std::move(state));
				storedBytes_ += states_.back().capacity();
				return false;
			}

			void clear()
			{
				states_.clear();
				storedBytes_ = 0;
			}

		private:
			std::deque<std::string> states_;
			std::size_t storedBytes_{};
		};

	}  // namespace

	Scheduler::Scheduler(World& world, IRandom& random) :
			world_(world),
			random_(random)
	{}

	void Scheduler::skipDeadSlots()
	{
		while (cursor_ < world_.unitCount() && !world_.isAlive(world_.unitAt(cursor_)))
		{
			++cursor_;
		}
	}

	void Scheduler::activateNext(IRandom& random)
	{
		world_.activate(world_.unitAt(cursor_++), random);
	}

	void Scheduler::step()
	{
		skipDeadSlots();
		if (cursor_ == world_.unitCount())
		{
			cursor_ = 0;
			skipDeadSlots();
		}
		if (cursor_ < world_.unitCount())
		{
			activateNext(random_);
		}
		if (cursor_ == world_.unitCount())
		{
			cursor_ = 0;
		}
	}

	RunResult Scheduler::run(std::uint64_t activationLimit)
	{
		TrackingRandom random(random_);
		CycleHistory history;
		std::uint64_t activations = 0;
		bool atInitialBoundary = cursor_ == 0;
		bool trackingRound = false;
		while (!world_.finished())
		{
			skipDeadSlots();
			if (atInitialBoundary || cursor_ == world_.unitCount())
			{
				// Closing a round includes traversing any trailing dead slots.
				// All compared states share the same scheduler phase.
				cursor_ = 0;
				auto state = world_.snapshot();
				trackingRound = state.has_value();
				if (!trackingRound)
				{
					history.clear();
				}
				else if (history.repeats(std::move(*state)))
				{
					return RunResult::Cycle;
				}
				random.used = false;
				world_.takeCycleBlockedFlag();
				atInitialBoundary = false;
				skipDeadSlots();
			}
			if (activations == activationLimit)
			{
				return RunResult::ActivationLimit;
			}
			activateNext(random);
			++activations;
			// A repeated state proves a cycle only across rounds with trackable, deterministic transitions.
			if (trackingRound && (random.used || world_.takeCycleBlockedFlag() || !world_.snapshot()))
			{
				history.clear();
				trackingRound = false;
			}
		}
		if (cursor_ == world_.unitCount())
		{
			cursor_ = 0;
		}
		return RunResult::Quiescent;
	}

}  // namespace sw::core
