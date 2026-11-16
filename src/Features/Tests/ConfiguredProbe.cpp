#include "Core/Abilities.hpp"
#include "Core/AbilityManager.hpp"
#include "Core/ActivationContext.hpp"
#include "Core/Config.hpp"
#include "Core/World.hpp"
#include "Features/AbilityProperties.hpp"

#include <utility>

namespace sw::features
{
	namespace
	{

		class ReportMarker final : public core::IAction
		{
		public:
			ReportMarker(core::EntityId actor, std::string marker) :
					actor_(actor),
					marker_(std::move(marker))
			{}

			void execute(core::World& world) override
			{
				world.abilityUsed(actor_, marker_);
			}

		private:
			core::EntityId actor_;
			std::string marker_;
		};

		// Test-only ability. Its action reports a per-instance XML value through the normal event sink.
		class ConfiguredProbe final : public core::IAbility
		{
		public:
			explicit ConfiguredProbe(std::string marker) :
					marker_(std::move(marker))
			{}

			bool canAct(const core::World&, core::EntityId) const override
			{
				return true;
			}

			core::AbilityProposal propose(core::ActivationContext& context) const override
			{
				return {core::AbilityProposal::Status::Ready, std::make_unique<ReportMarker>(context.actor, marker_)};
			}

		private:
			std::string marker_;
		};

		const bool registered = core::AbilityManager::instance()->registerAbility(
				"configured_probe",
				[](const core::AbilityConfig& config, const core::PropertyMap&)
				{
					AbilityProperties properties(config.properties);
					const auto& marker = properties.required<std::string>("markerText");
					if (marker.empty())
					{
						throw std::invalid_argument("property 'markerText' must be nonempty");
					}
					properties.rejectUnknown();
					return std::make_unique<ConfiguredProbe>(marker);
				});

	}  // namespace
}  // namespace sw::features
