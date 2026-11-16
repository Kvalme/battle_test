#pragma once

#include "Core/Properties.hpp"

#include <set>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace sw::features
{

	// A factory reads its schema from a borrowed property map. Each successful read
	// marks a key as recognized, so rejectUnknown() can catch typos after parsing.
	class AbilityProperties
	{
	public:
		explicit AbilityProperties(const core::PropertyMap& properties) :
				properties_(properties)
		{}

		// An absent key allows a default. A present key must match the declared type.
		template <class T>
		const T* optional(const std::string& name)
		{
			const auto found = properties_.find(name);
			if (found == properties_.end())
			{
				return nullptr;
			}
			const auto* value = std::get_if<T>(&found->second);
			if (!value)
			{
				throw std::invalid_argument("property '" + name + "' must have type '" + typeName<T>() + "'");
			}
			read_.insert(found->first);
			return value;
		}

		template <class T>
		const T& required(const std::string& name)
		{
			if (const auto* value = optional<T>(name))
			{
				return *value;
			}
			throw std::invalid_argument("missing required property '" + name + "'");
		}

		void rejectUnknown() const
		{
			for (const auto& [name, value] : properties_)
			{
				if (!read_.contains(name))
				{
					throw std::invalid_argument("unknown property '" + name + "'");
				}
			}
		}

	private:
		template <class T>
		static constexpr const char* typeName()
		{
			if constexpr (std::is_same_v<T, std::int64_t>)
			{
				return "int";
			}
			else if constexpr (std::is_same_v<T, double>)
			{
				return "double";
			}
			else if constexpr (std::is_same_v<T, bool>)
			{
				return "bool";
			}
			else
			{
				static_assert(std::is_same_v<T, std::string>);
				return "string";
			}
		}

		const core::PropertyMap& properties_;
		std::set<std::string_view> read_;
	};

	inline std::int64_t inRange(const std::string& label, std::int64_t value, std::int64_t min, std::int64_t max)
	{
		if (value < min || value > max)
		{
			throw std::invalid_argument(
					label + " must be between " + std::to_string(min) + " and " + std::to_string(max) + " (got "
					+ std::to_string(value) + ")");
		}
		return value;
	}

}  // namespace sw::features
