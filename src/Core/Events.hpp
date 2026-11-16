#pragma once

#include "Types.hpp"

#include <string>

namespace sw::core
{

	/**
	 * @brief Describes a simulation change for an output adapter.
	 *
	 * World creates this value and passes it to IEventSink::emit().
	 * tick is the activation counter when the event is published.
	 * The event kind specifies which fields the adapter must read:
	 * - MapCreated: width and height.
	 * - Spawned: source, name (unit type), and position.
	 * - MarchStarted: source, position, and destination.
	 * - MarchEnded and Moved: source and position.
	 * - AbilityUsed: source and name.
	 * - Attacked: source, target, damage, and hp.
	 * - Died: target.
	 *
	 * Attacked reports total HP lost for one source/target pair during an operation.
	 * hp is the target's final health at the end of that operation. Other fields are unused.
	 */
	struct Event
	{
		enum class Kind
		{
			MapCreated,
			Spawned,
			MarchStarted,
			MarchEnded,
			Moved,
			AbilityUsed,
			Attacked,
			Died
		} kind;
		std::uint64_t tick{};
		EntityId source{}, target{};
		Position position{}, destination{};
		std::uint32_t width{}, height{}, damage{}, hp{};
		std::string name;
	};

	/**
	 * @brief Receives simulation events and sends them to an output system.
	 *
	 * An application adapter implements this interface to format events or record them.
	 * The application passes the adapter to World and keeps it alive until World is destroyed.
	 */
	class IEventSink
	{
	public:
		/**
		 * @brief Destroys the event receiver through the interface.
		 *
		 * The application destroys its receiver after all Worlds that use it are gone.
		 */
		virtual ~IEventSink() = default;

		/**
		 * @brief Processes one simulation event before returning to World.
		 *
		 * World calls this method synchronously whenever it publishes an event.
		 * An implementation must write, record, or copy the event before returning.
		 * Do not retain the reference or change the World that emitted it.
		 * Exceptions propagate and can stop publication of later events; changes are not rolled back.
		 *
		 * @param event Event valid only for the duration of this call.
		 */
		virtual void emit(const Event& event) = 0;
	};

}  // namespace sw::core
