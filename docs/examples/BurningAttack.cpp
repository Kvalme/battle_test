// Documentation example. Copy to src/Features/Attacks to enable it in the application.
#include "Core/AbilityManager.hpp"
#include "Core/Config.hpp"
#include "Core/Damage.hpp"
#include "Core/Effects.hpp"
#include "Core/World.hpp"
#include "Features/AbilityProperties.hpp"
#include "Features/Attacks/AttackAbility.hpp"

#include <memory>
#include <utility>

namespace sw::features
{
	namespace
	{
		/** @brief Deals damage on each of the target's next three activations. */
		class BurningEffect final : public core::IEffect
		{
		public:
			BurningEffect(core::EntityId source, core::EntityId target, std::int64_t damage) :
					IEffect("burning", source, target, 3),
					damage_(damage)
			{}

			core::EffectStackKey stackKey() const override
			{
				return {type, target, source};
			}

			void onActivation(core::World& world) override
			{
				world.damage({source, target, damage_, {"fire"}});
			}

		private:
			std::int64_t damage_;
		};

		/** @brief Attaches a burning effect without dealing immediate damage. */
		class BurningAction final : public core::IAction
		{
		public:
			BurningAction(core::EntityId source, core::EntityId target, std::int64_t damage) :
					source_(source),
					target_(target),
					damage_(damage)
			{}

			void execute(core::World& world) override
			{
				world.abilityUsed(source_, "burning_attack");
				world.applyEffect(std::make_unique<BurningEffect>(source_, target_, damage_));
			}

		private:
			core::EntityId source_, target_;
			std::int64_t damage_;
		};

		/** @brief Uses standard attack targeting to select a unit to burn. */
		class BurningAttack final : public AttackAbility
		{
		public:
			explicit BurningAttack(AttackConfig config) :
					AttackAbility(std::move(config))
			{}

		private:
			std::unique_ptr<core::IAction> makeAction(
					core::EntityId source, core::EntityId target, std::int64_t amount) const override
			{
				return std::make_unique<BurningAction>(source, target, amount);
			}
		};

		struct Registrar
		{
			Registrar()
			{
				core::AbilityManager::instance()->registerAbility(
						"burning_attack",
						[](const core::AbilityConfig& config, const core::PropertyMap& unitProperties)
						{
							AbilityProperties properties(config.properties);
							auto attack = readAttackConfig(properties, unitProperties, "damageStat");
							properties.rejectUnknown();
							return std::make_unique<BurningAttack>(std::move(attack));
						});
			}
		};

		const Registrar registrar;
	}
}
