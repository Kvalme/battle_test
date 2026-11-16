#pragma once

#include "Types.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace sw::core
{

	struct ActivationContext;
	class World;

	/**
	 * @brief Defines an action that changes the simulation, such as moving a unit or dealing damage.
	 *
	 * An ability creates an object that implements this interface.
	 * The scheduler calls execute() if it selects that ability's action.
	 * Each implementation stores the data needed for its action and uses World methods to apply the changes.
	 */
	class IAction
	{
	public:
		/**
		 * @brief Destroys the action and releases its resources.
		 *
		 * The scheduler destroys the action after execution, when its AbilityProposal leaves scope.
		 */
		virtual ~IAction() = default;

		/**
		 * @brief Performs the selected action through World operations.
		 *
		 * The scheduler calls this method once after it selects a Ready proposal.
		 * Implementations must apply their changes here, for example by calling World::damage().
		 * The scheduler groups damage from this call into one operation.
		 * Exceptions propagate to the caller. Changes are not rolled back.
		 *
		 * @param world Simulation to change.
		 */
		virtual void execute(World& world) = 0;
	};

	/**
	 * @brief Reports whether an ability has an action for the current activation.
	 *
	 * IAbility::propose() returns this object to the scheduler.
	 * NotApplicable tells the scheduler to try the next ability.
	 * Ready tells the scheduler to execute action; action must not be null.
	 * The scheduler throws std::logic_error for Ready with a null action.
	 */
	struct AbilityProposal
	{
		enum class Status
		{
			NotApplicable,
			Ready
		} status{Status::NotApplicable};

		/**
         * @brief Stores the action created by the ability for this activation.
         *
         * The scheduler executes this action only when status is Ready.
         * The unique_ptr deletes the action when this AbilityProposal is destroyed.
         */
		std::unique_ptr<IAction> action;
	};

	/**
	 * @brief Defines when a unit can use a behavior and which action it will perform.
	 *
	 * A factory creates a separate ability object for each unit. Entity stores it.
	 * The scheduler uses canAct() to check whether the simulation still has work.
	 * During an activation, it calls propose() in the order defined by the unit template.
	 * Implement decisions in this interface and world changes in IAction::execute().
	 */
	class IAbility
	{
	public:
		/**
		 * @brief Destroys the ability and releases its resources.
		 *
		 * Entity destroys its abilities when it is destroyed. Temporary validation instances are also
		 * destroyed by their caller.
		 */
		virtual ~IAbility() = default;

		/**
		 * @brief Checks whether this ability can perform useful work in the current state.
		 *
		 * World::finished() calls this method when it checks whether the simulation can stop.
		 * The method can be called repeatedly. Do not change state or use random numbers.
		 * Check whether work is possible, not whether a random attempt will succeed.
		 * A false result must not hide work that this ability could perform.
		 *
		 * @param world Simulation to inspect without changes.
		 * @param actor ID of the unit that has this ability.
		 * @return True if useful work is possible; false if this ability has no work in this state.
		 */
		virtual bool canAct(const World& world, EntityId actor) const = 0;

		/**
		 * @brief Checks the current conditions and creates an action if this ability can act.
		 *
		 * The scheduler calls this method when it reaches this ability in template order.
		 * It does not call canAct() first. Check all required conditions here.
		 * Use context.random for random decisions and context.chooseTarget() for target selection.
		 * Do not change the world here; put those changes in the returned action.
		 * After a Ready result, the scheduler does not ask the remaining abilities.
		 *
		 * @param context World, actor, random generator, and saved targets for this activation.
		 * @return A Ready result with a non-null action, or NotApplicable to let the next ability try.
		 */
		virtual AbilityProposal propose(ActivationContext& context) const = 0;

		/**
		 * @brief Checks whether another ability can be used on the same unit.
		 *
		 * AbilityManager::createAll() calls this method for each pair in both directions.
		 * This happens during template validation and again when a unit is created.
		 * Reject incompatible settings, such as different target queries for a shared group.
		 * Do not change either ability or save a reference to other. The default accepts all pairs.
		 *
		 * @param other Another ability configured for the same unit.
		 * @throws std::invalid_argument If the pair is incompatible.
		 */
		virtual void validateCompatibility([[maybe_unused]] const IAbility& other) const {}

		/**
		 * @brief Describes ability state for detection of a repeated simulation cycle.
		 *
		 * The scheduler calls this method while Scheduler::run() checks simulation states for repetition.
		 * If you override it, encode every private value that can affect future behavior.
		 * Equal strings must describe equivalent ability states. Do not change state or use random numbers.
		 * Return std::nullopt if behavior depends on absolute ticks or external state.
		 * Use only the activation random generator for random decisions and do not call the scheduler recursively.
		 *
		 * @return A stable state string; an empty string for no private state; std::nullopt to disable cycle
		 * detection.
		 */
		virtual std::optional<std::string> cycleState() const
		{
			return std::nullopt;
		}
	};

}  // namespace sw::core
