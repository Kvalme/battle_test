#pragma once

#include "Core/Abilities.hpp"
#include "Core/ActivationContext.hpp"
#include "Core/Properties.hpp"

#include <string>

namespace sw::core
{
	struct Entity;
}

namespace sw::features
{

	class AbilityProperties;

	// Validated attack settings. The stat keys resolve against the acting unit.
	struct AttackConfig
	{
		std::string amountStat;
		std::string chanceStat;
		std::string maxRangeStat;
		core::TargetQuery target;
		std::string targetGroup;
	};

	AttackConfig readAttackConfig(
			AbilityProperties&,
			const core::PropertyMap& unitProperties,
			const std::string& amountProperty,
			bool usesChance = false);

	// Share targeting and chance handling. Derived classes supply the action for a successful attack.
	class AttackAbility : public core::IAbility
	{
	public:
		bool canAct(const core::World&, core::EntityId) const final;
		core::AbilityProposal propose(core::ActivationContext&) const final;
		void validateCompatibility(const core::IAbility&) const final;

	protected:
		explicit AttackAbility(AttackConfig, bool appliesEffectAtZero = false);

	private:
		virtual std::unique_ptr<core::IAction> makeAction(
				core::EntityId source, core::EntityId target, std::int64_t amount) const
				= 0;

		virtual bool canActWithoutDamage(const core::World&, core::EntityId) const
		{
			return false;
		}

		std::int64_t chance(const core::Entity&) const;
		core::TargetQuery query(const core::Entity&) const;

		AttackConfig config_;
		bool appliesEffectAtZero_;
	};

}
