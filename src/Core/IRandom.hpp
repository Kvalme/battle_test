#pragma once

#include <cstdint>

namespace sw::core
{

	/**
	 * @brief Supplies random numbers to simulation decisions.
	 *
	 * The application supplies an implementation to Scheduler. Tests can supply controlled values.
	 * Abilities use ActivationContext::random so Scheduler can detect random transitions.
	 */
	class IRandom
	{
	public:
		/**
		 * @brief Destroys a random generator through the interface.
		 *
		 * The application or test that created the generator destroys it after all Schedulers that use it are gone.
		 */
		virtual ~IRandom() = default;

		/**
		 * @brief Draws an integer from an inclusive interval.
		 *
		 * Target selection and abilities call this method when they need a random choice.
		 * Implementations must give each value in the interval equal probability.
		 * A call can advance generator state even when min equals max.
		 *
		 * @param min Smallest allowed value; must not exceed max.
		 * @param max Largest allowed value.
		 * @return A random integer greater than or equal to min and less than or equal to max.
		 */
		virtual std::uint32_t uniform(std::uint32_t min, std::uint32_t max) = 0;
	};

}  // namespace sw::core
