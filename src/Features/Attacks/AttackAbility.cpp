#include "Features/Attacks/AttackAbility.hpp"

#include "Core/Entity.hpp"
#include "Core/IRandom.hpp"
#include "Core/World.hpp"
#include "Features/AbilityProperties.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace sw::features
{
	namespace
	{

		std::int64_t stat(
				const core::PropertyMap& properties,
				const std::string& parameter,
				const std::string& reference,
				std::int64_t min,
				std::int64_t max)
		{
			const auto label = "property '" + parameter + "' referencing unit stat '" + reference + "'";
			const auto found = properties.find(reference);
			if (found == properties.end())
			{
				throw std::invalid_argument(label + ": unknown unit stat");
			}
			const auto* value = std::get_if<std::int64_t>(&found->second);
			if (!value)
			{
				throw std::invalid_argument(label + ": unit stat must have type 'int'");
			}
			return inRange(label, *value, min, max);
		}

	}  // namespace

	AttackConfig readAttackConfig(
			AbilityProperties& properties,
			const core::PropertyMap& unitProperties,
			const std::string& amountProperty,
			bool usesChance)
	{
		AttackConfig result;
		result.amountStat = properties.required<std::string>(amountProperty);
		stat(unitProperties, amountProperty, result.amountStat, 0, std::numeric_limits<std::uint32_t>::max());
		if (usesChance)
		{
			result.chanceStat = properties.required<std::string>("chanceStat");
			stat(unitProperties, "chanceStat", result.chanceStat, 0, 1000);
		}

		const auto& kind = properties.required<std::string>("kind");
		if (kind == "melee")
		{
			result.target.kind = core::AttackKind::Melee;
		}
		else if (kind == "ranged")
		{
			result.target.kind = core::AttackKind::Ranged;
		}
		else
		{
			throw std::invalid_argument("property 'kind' must be 'melee' or 'ranged'");
		}

		constexpr auto rangeLimit = std::numeric_limits<int>::max();
		if (const auto* minimum = properties.optional<std::int64_t>("minRange"))
		{
			result.target.minRange = static_cast<int>(inRange("property 'minRange'", *minimum, 1, rangeLimit));
		}
		const auto* maximum = properties.optional<std::int64_t>("maxRange");
		if (const auto* reference = properties.optional<std::string>("maxRangeStat"))
		{
			if (maximum)
			{
				throw std::invalid_argument("properties 'maxRange' and 'maxRangeStat' are mutually exclusive");
			}
			// A short range stat can disable this attack and leave the unit's fallback available.
			result.maxRangeStat = *reference;
			result.target.maxRange = static_cast<int>(stat(unitProperties, "maxRangeStat", *reference, 0, rangeLimit));
		}
		else
		{
			const auto value = maximum ? *maximum : result.target.maxRange;
			result.target.maxRange
					= static_cast<int>(inRange("property 'maxRange'", value, result.target.minRange, rangeLimit));
		}
		if (const auto* noAdjacent = properties.optional<bool>("noAdjacent"))
		{
			result.target.noAdjacent = *noAdjacent;
		}
		if (const auto* group = properties.optional<std::string>("targetGroup"))
		{
			if (group->find_first_not_of(" \t\r\n") == std::string::npos)
			{
				throw std::invalid_argument("property 'targetGroup' must be nonempty");
			}
			result.targetGroup = *group;
		}
		return result;
	}

	AttackAbility::AttackAbility(AttackConfig config, bool appliesEffectAtZero) :
			config_(std::move(config)),
			appliesEffectAtZero_(appliesEffectAtZero)
	{}

	void AttackAbility::validateCompatibility(const core::IAbility& other) const
	{
		const auto* attack = dynamic_cast<const AttackAbility*>(&other);
		if (!attack || config_.targetGroup.empty() || config_.targetGroup != attack->config_.targetGroup)
		{
			return;
		}

		const auto& first = config_.target;
		const auto& second = attack->config_.target;
		const auto mismatch = [&](const char* parameter)
		{
			throw std::invalid_argument(
					"incompatible queries for property 'targetGroup' = '" + config_.targetGroup + "': different '"
					+ parameter + "'");
		};
		if (first.kind != second.kind)
		{
			mismatch("kind");
		}
		if (first.minRange != second.minRange)
		{
			mismatch("minRange");
		}
		// Equal resolved values are insufficient when the maximums come from different sources.
		if (config_.maxRangeStat != attack->config_.maxRangeStat)
		{
			mismatch("maxRangeStat");
		}
		if (first.maxRange != second.maxRange)
		{
			mismatch("maxRange");
		}
		if (first.noAdjacent != second.noAdjacent)
		{
			mismatch("noAdjacent");
		}
	}

	std::int64_t AttackAbility::chance(const core::Entity& attacker) const
	{
		return config_.chanceStat.empty() ? 1000 : core::integerProperty(attacker.properties, config_.chanceStat);
	}

	core::TargetQuery AttackAbility::query(const core::Entity& attacker) const
	{
		auto result = config_.target;
		if (!config_.maxRangeStat.empty())
		{
			const auto range = core::integerProperty(attacker.properties, config_.maxRangeStat);
			result.maxRange = static_cast<int>(std::clamp<std::int64_t>(range, 0, std::numeric_limits<int>::max()));
		}
		return result;
	}

	bool AttackAbility::canAct(const core::World& world, core::EntityId actor) const
	{
		const auto& attacker = world.entity(actor);
		if (!attacker.alive() || chance(attacker) <= 0)
		{
			return false;
		}
		const auto amount = core::integerProperty(attacker.properties, config_.amountStat);
		if (amount < 0 || (amount == 0 && !appliesEffectAtZero_))
		{
			return false;
		}
		const auto targets = world.targets(actor, query(attacker));
		return amount > 0
					   ? !targets.empty()
					   : std::ranges::any_of(
								 targets,
								 [this, &world](core::EntityId target) { return canActWithoutDamage(world, target); });
	}

	core::AbilityProposal AttackAbility::propose(core::ActivationContext& context) const
	{
		const auto& attacker = context.world.entity(context.actor);
		if (!attacker.alive())
		{
			return {};
		}
		const auto amount = core::integerProperty(attacker.properties, config_.amountStat);
		if (amount < 0 || (amount == 0 && !appliesEffectAtZero_))
		{
			return {};
		}
		// Choose before rolling so the ordinary fallback can reuse the same target on a miss.
		const auto target = context.chooseTarget(query(attacker), config_.targetGroup);
		if (!target || (amount == 0 && !canActWithoutDamage(context.world, *target))
			|| (!config_.chanceStat.empty() && context.random.uniform(1, 1000) > chance(attacker)))
		{
			return {};
		}
		return {core::AbilityProposal::Status::Ready, makeAction(context.actor, *target, amount)};
	}

}  // namespace sw::features
