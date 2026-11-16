#include "Core/AbilityManager.hpp"
#include "Core/Config.hpp"
#include "Core/Damage.hpp"
#include "Core/Effects.hpp"
#include "Core/World.hpp"
#include "Features/AbilityProperties.hpp"
#include "Features/Attacks/AttackAbility.hpp"

#include <utility>

namespace sw::features
{
	namespace
	{

		constexpr auto abilityName = "poison_arrows";
		constexpr auto effectType = "poison";
		constexpr int effectDuration = 5;

		class PoisonEffect final : public core::IEffect
		{
		public:
			PoisonEffect(core::EntityId source, core::EntityId target, std::int64_t totalDamage) :
					IEffect(effectType, source, target, effectDuration),
					portion_(totalDamage / effectDuration),
					extraPortions_(totalDamage % effectDuration)
			{}

			// Each hunter keeps a separate poison instance on the target for damage attribution.
			core::EffectStackKey stackKey() const override
			{
				return {type, target, source};
			}

			bool hasPendingWork() const override
			{
				return remaining > 0;
			}

			void onApply(core::World& world) override
			{
				// The application deals the first portion and consumes one of the five ticks.
				tick(world);
				--remaining;
			}

			void onActivation(core::World& world) override
			{
				tick(world);
			}

		private:
			void tick(core::World& world)
			{
				// Give the first portions one extra point each so integer division preserves total damage.
				const auto damage = portion_ + (phase_ < extraPortions_ ? 1 : 0);
				phase_ = (phase_ + 1) % effectDuration;
				world.damage({source, target, damage, {effectType}});
			}

			std::int64_t portion_, extraPortions_;
			int phase_{};
		};

		class PoisonArrowsAction final : public core::IAction
		{
		public:
			PoisonArrowsAction(core::EntityId source, core::EntityId target, std::int64_t damage) :
					source_(source),
					target_(target),
					totalDamage_(damage)
			{}

			void execute(core::World& world) override
			{
				world.abilityUsed(source_, abilityName);
				world.applyEffect(std::make_unique<PoisonEffect>(source_, target_, totalDamage_));
			}

		private:
			core::EntityId source_, target_;
			std::int64_t totalDamage_;
		};

		class PoisonArrowsAbility final : public AttackAbility
		{
		public:
			std::optional<std::string> cycleState() const override
			{
				return std::string{};
			}

			explicit PoisonArrowsAbility(AttackConfig config) :
					AttackAbility(std::move(config))
			{}

		private:
			std::unique_ptr<core::IAction> makeAction(
					core::EntityId source, core::EntityId target, std::int64_t amount) const override
			{
				return std::make_unique<PoisonArrowsAction>(source, target, amount);
			}
		};

		struct Registrar
		{
			Registrar()
			{
				core::AbilityManager::instance()->registerAbility(
						abilityName,
						[](const core::AbilityConfig& config, const core::PropertyMap& unitProperties)
						{
							AbilityProperties properties(config.properties);
							auto attack = readAttackConfig(properties, unitProperties, "totalDamageStat", true);
							properties.rejectUnknown();
							return std::make_unique<PoisonArrowsAbility>(std::move(attack));
						});
			}
		};

		const Registrar registrar;

	}
}
