#pragma once

#include <cstddef>
#include <cstdint>

namespace sw::core
{

	class IRandom;
	class World;

	/**
	 * @brief States why Scheduler::run() stopped.
	 *
	 * Quiescent means no unit or effect has useful work.
	 * Cycle means a complete simulation state repeated with deterministic transitions.
	 * ActivationLimit means the call used its allowed number of activations.
	 * The last two results do not imply that World::finished() is true.
	 */
	enum class RunResult
	{
		Quiescent,
		Cycle,
		ActivationLimit
	};

	/**
	 * @brief Decides which unit acts next and when the simulation stops.
	 *
	 * Application code creates one Scheduler for one World, then calls run() or step().
	 * Scheduler activates living units in spawn order and keeps its position between calls.
	 * World does the work of each activation. Scheduler only chooses the unit and detects repeated states.
	 * The World and the random generator are supplied by the application and must remain alive.
	 * Use one call at a time. Callbacks must not start another step() or run().
	 * Callback exceptions propagate without rollback; stop using the simulation after such a failure.
	 */
	class Scheduler
	{
	public:
		/// Activation limit that run() uses when the caller gives no limit.
		static constexpr std::uint64_t defaultActivationLimit = 10'000'000;

		/**
		 * @brief Creates a scheduler that starts before the first unit.
		 *
		 * Application code calls this constructor after it creates the World.
		 * Units may be spawned before or after this call.
		 *
		 * @param world Simulation to drive. It must remain alive until this Scheduler is destroyed.
		 * @param random Random generator for abilities. It must remain alive until this Scheduler is destroyed.
		 */
		Scheduler(World& world, IRandom& random);

		/**
		 * @brief Processes one activation of the next living unit.
		 *
		 * Application code and tests call this method to advance the simulation manually.
		 * It advances World::tick(), applies effects, and selects at most one ability action.
		 * It can activate a unit even when World::finished() is true. With no living units, it does nothing.
		 * It does not check for cycles or enforce an activation limit.
		 */
		void step();

		/**
		 * @brief Processes activations until the simulation stops or reaches a limit.
		 *
		 * Application code calls this method after it loads scenario commands.
		 * It continues from the current scheduler position and checks for useful work before each activation.
		 * Cycle detection requires complete state descriptions and deterministic transitions.
		 *
		 * @param activationLimit Maximum activations for this call. Zero permits no activations.
		 * @return Quiescent for no work, Cycle for a proven repeated state, or ActivationLimit for an
		 * exhausted budget.
		 */
		RunResult run(std::uint64_t activationLimit = defaultActivationLimit);

	private:
		void skipDeadSlots();
		void activateNext(IRandom& random);

		World& world_;
		IRandom& random_;
		std::size_t cursor_{};
	};

}  // namespace sw::core
