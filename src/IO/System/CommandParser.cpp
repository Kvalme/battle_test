#include "CommandParser.hpp"

#include <cstddef>
#include <exception>
#include <sstream>

namespace sw::io
{
	void CommandParser::parse(std::istream& stream)
	{
		std::string line;
		std::size_t lineNumber = 0;
		while (std::getline(stream, line))
		{
			++lineNumber;
			std::istringstream commandStream(line);
			std::string commandName;
			if (!(commandStream >> commandName) || commandName.starts_with("//"))
			{
				continue;
			}

			auto command = _commands.find(commandName);
			const auto location = "Line " + std::to_string(lineNumber) + ": ";
			if (command == _commands.end())
			{
				throw std::runtime_error(location + "Unknown command: " + commandName);
			}

			try
			{
				command->second(commandStream);
			}
			catch (const std::exception& error)
			{
				throw std::runtime_error(location + commandName + ": " + error.what());
			}
		}
	}
}
