#include "Core/AbilityManager.hpp"
#include "Core/Config.hpp"
#include "Core/Damage.hpp"
#include "Core/Effects.hpp"
#include "Core/World.hpp"
#include "Features/AbilityProperties.hpp"
#include "Features/Attacks/AttackAbility.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace sw::features
{
	namespace
	{

		constexpr auto abilityName = "rending";
		constexpr auto effectType = "rending_received";
		constexpr auto poisonType = "poison";
		constexpr int effectDuration = 1;

		class RendingEffect final : public core::IEffect
		{
		public:
			RendingEffect(core::EntityId source, core::EntityId target) :
					IEffect(effectType, source, target, effectDuration)
			{}

			// A wound only modifies incoming damage and cannot keep combat going on its own.
			bool hasPendingWork() const override
			{
				return false;
			}

			std::optional<std::string> cycleState() const override
			{
				return std::string{};
			}

			std::int64_t modifyDamage(const core::DamageRequest& request, std::int64_t amount) const override
			{
				if (amount <= 0 || std::ranges::find(request.tags, poisonType) == request.tags.end())
				{
					return amount;
				}
				constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
				return amount > maximum / 2 ? maximum : amount * 2;
			}
		};

		class RendingAction final : public core::IAction
		{
		public:
			RendingAction(core::EntityId source, core::EntityId target, std::int64_t damage) :
					source_(source),
					target_(target),
					damage_(damage)
			{}

			void execute(core::World& world) override
			{
				world.abilityUsed(source_, abilityName);
				world.damage({source_, target_, damage_, {}});
				if (world.isAlive(target_))
				{
					world.applyEffect(std::make_unique<RendingEffect>(source_, target_));
				}
			}

		private:
			core::EntityId source_, target_;
			std::int64_t damage_;
		};

		class RendingAbility final : public AttackAbility
		{
		public:
			std::optional<std::string> cycleState() const override
			{
				return std::string{};
			}

			explicit RendingAbility(AttackConfig config) :
					AttackAbility(std::move(config), true)
			{}

		private:
			bool canActWithoutDamage(const core::World& world, core::EntityId target) const override
			{
				// A zero-damage strike is useful only if the wound can amplify an existing poison.
				return world.hasEffect(target, poisonType);
			}

			std::unique_ptr<core::IAction> makeAction(
					core::EntityId source, core::EntityId target, std::int64_t amount) const override
			{
				return std::make_unique<RendingAction>(source, target, amount);
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
							auto attack = readAttackConfig(properties, unitProperties, "damageStat", true);
							properties.rejectUnknown();
							return std::make_unique<RendingAbility>(std::move(attack));
						});
			}
		};

		const Registrar registrar;

	}
}
