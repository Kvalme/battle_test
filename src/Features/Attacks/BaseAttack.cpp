#include "Core/AbilityManager.hpp"
#include "Core/Config.hpp"
#include "Core/Damage.hpp"
#include "Core/World.hpp"
#include "Features/AbilityProperties.hpp"
#include "Features/Attacks/AttackAbility.hpp"

#include <utility>

namespace sw::features
{

	namespace
	{

		class AttackAction final : public core::IAction
		{
		public:
			AttackAction(core::EntityId source, core::EntityId target, std::int64_t damage) :
					request_{source, target, damage, {}}
			{}

			void execute(core::World& world) override
			{
				// World publishes the hit from actual HP loss, including any damage modifiers.
				world.damage(request_);
			}

		private:
			core::DamageRequest request_;
		};

		class BaseAttack final : public AttackAbility
		{
		public:
			std::optional<std::string> cycleState() const override
			{
				return std::string{};
			}

			explicit BaseAttack(AttackConfig config) :
					AttackAbility(std::move(config))
			{}

		private:
			std::unique_ptr<core::IAction> makeAction(
					core::EntityId source, core::EntityId target, std::int64_t amount) const override
			{
				return std::make_unique<AttackAction>(source, target, amount);
			}
		};

		struct Registrar
		{
			Registrar()
			{
				core::AbilityManager::instance()->registerAbility(
						"attack",
						[](const core::AbilityConfig& config, const core::PropertyMap& unitProperties)
						{
							AbilityProperties properties(config.properties);
							// Ordinary attacks have no chance stat, so a selected target costs no second RNG draw.
							auto attack = readAttackConfig(properties, unitProperties, "damageStat");
							properties.rejectUnknown();
							return std::make_unique<BaseAttack>(std::move(attack));
						});
			}
		};

		const Registrar registrar;

	}
}
