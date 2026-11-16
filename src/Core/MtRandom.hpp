#pragma once

#include "IRandom.hpp"

#include <cstdint>
#include <random>

namespace sw::core
{

	/**
	 * @brief Implements simulation randomness with the mt19937 engine.
	 *
	 * Application code uses this implementation for normal simulation runs.
	 * A fixed seed repeats a sequence with the same standard-library implementation.
	 * Different standard libraries can produce different distribution results.
	 */
	class MtRandom final : public IRandom
	{
	public:
		/**
		 * @brief Starts a random generator with a specified seed.
		 *
		 * The application calls this constructor before passing the generator to World.
		 *
		 * @param seed Initial engine seed. The default is zero for repeatable runs.
		 */
		explicit MtRandom(std::uint32_t seed = 0);

		/**
		 * @brief Draws an integer with the standard uniform distribution.
		 *
		 * World target selection and abilities call this through IRandom.
		 * The method advances the engine as required by the standard-library distribution.
		 *
		 * @param min Smallest allowed value.
		 * @param max Largest allowed value.
		 * @return A random integer in the inclusive interval [min, max].
		 * @throws std::invalid_argument If min is greater than max.
		 */
		std::uint32_t uniform(std::uint32_t min, std::uint32_t max) override;

	private:
		std::mt19937 engine_;
	};

}  // namespace sw::core
