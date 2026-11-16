#pragma once

#include "Details/CommandParserVisitor.hpp"

#include <functional>
#include <istream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace sw::io
{
	class CommandParser
	{
	public:
		template <class TCommandData>
		void add(std::function<void(TCommandData)> handler)
		{
			std::string commandName = TCommandData::Name;
			auto [it, inserted] = _commands.emplace(
					commandName,
					[handler = std::move(handler)](std::istream& stream)
					{
						TCommandData data{};
						CommandParserVisitor visitor(stream);
						data.visit(visitor);
						std::string extra;
						if (stream >> extra && !extra.starts_with("//"))
						{
							throw std::runtime_error("Unexpected argument: " + extra);
						}
						handler(std::move(data));
					});
			if (!inserted)
			{
				throw std::runtime_error("Command already exists: " + commandName);
			}
		}

		void parse(std::istream& stream);

	private:
		std::unordered_map<std::string, std::function<void(std::istream&)>> _commands;
	};
}
