#include "Core/Abilities.hpp"
#include "Core/AbilityManager.hpp"
#include "Core/ActivationContext.hpp"
#include "Core/Config.hpp"
#include "Core/World.hpp"
#include "Features/AbilityProperties.hpp"

#include <limits>
#include <utility>
#include <vector>

namespace sw::features
{
	namespace
	{

		class MoveAction final : public core::IAction
		{
		public:
			MoveAction(core::EntityId actor, std::vector<core::Position> path) :
					actor_(actor),
					path_(std::move(path))
			{}

			void execute(core::World& world) override
			{
				world.moveAlong(actor_, path_);
			}

		private:
			core::EntityId actor_;
			std::vector<core::Position> path_;
		};

		class MoveAbility final : public core::IAbility
		{
		public:
			explicit MoveAbility(unsigned cells) :
					cells_(cells)
			{}

			bool canAct(const core::World& world, core::EntityId actor) const override
			{
				return world.nextPosition(actor, cells_).has_value();
			}

			std::optional<std::string> cycleState() const override
			{
				return std::string{};
			}

			core::AbilityProposal propose(core::ActivationContext& context) const override
			{
				// The route is limited to this activation's step budget. World checks each step again on execution.
				auto path = context.world.movementPath(context.actor, cells_);
				if (path.empty())
				{
					return {};
				}
				return {core::AbilityProposal::Status::Ready,
						std::make_unique<MoveAction>(context.actor, std::move(path))};
			}

		private:
			unsigned cells_;
		};

		struct Registrar
		{
			Registrar()
			{
				core::AbilityManager::instance()->registerAbility(
						"move",
						[](const core::AbilityConfig& config, const core::PropertyMap&)
						{
							AbilityProperties properties(config.properties);
							const auto* cells = properties.optional<std::int64_t>("cells");
							properties.rejectUnknown();
							const auto count = inRange(
									"property 'cells'", cells ? *cells : 1, 1, std::numeric_limits<unsigned>::max());
							return std::make_unique<MoveAbility>(static_cast<unsigned>(count));
						});
			}
		};

		const Registrar registrar;

	}
}
