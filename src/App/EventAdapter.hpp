#pragma once
#include "Core/Events.hpp"
#include "IO/Events/MapCreated.hpp"
#include "IO/Events/MarchEnded.hpp"
#include "IO/Events/MarchStarted.hpp"
#include "IO/Events/UnitAbilityUsed.hpp"
#include "IO/Events/UnitAttacked.hpp"
#include "IO/Events/UnitDied.hpp"
#include "IO/Events/UnitMoved.hpp"
#include "IO/Events/UnitSpawned.hpp"
#include "IO/System/EventSystem.hpp"

namespace sw::app
{
	class EventAdapter final : public core::IEventSink
	{
	public:
		explicit EventAdapter(EventSystem& output) :
				output_(output)
		{}

		void emit(const core::Event& e) override
		{
			using Kind = core::Event::Kind;
			switch (e.kind)
			{
				case Kind::MapCreated: output_.event(e.tick, io::MapCreated{e.width, e.height}); break;
				case Kind::Spawned:
					output_.event(e.tick, io::UnitSpawned{e.source, e.name, e.position.x, e.position.y});
					break;
				case Kind::MarchStarted:
					output_.event(
							e.tick,
							io::MarchStarted{e.source, e.position.x, e.position.y, e.destination.x, e.destination.y});
					break;
				case Kind::MarchEnded:
					output_.event(e.tick, io::MarchEnded{e.source, e.position.x, e.position.y});
					break;
				case Kind::Moved: output_.event(e.tick, io::UnitMoved{e.source, e.position.x, e.position.y}); break;
				case Kind::AbilityUsed: output_.event(e.tick, io::UnitAbilityUsed{e.source, e.name}); break;
				case Kind::Attacked: output_.event(e.tick, io::UnitAttacked{e.source, e.target, e.damage, e.hp}); break;
				case Kind::Died: output_.event(e.tick, io::UnitDied{e.target}); break;
			}
		}

	private:
		EventSystem& output_;
	};
}
