#pragma once

#include <charconv>
#include <cstdint>
#include <istream>
#include <stdexcept>
#include <string>

namespace sw
{
	class CommandParserVisitor
	{
	public:
		explicit CommandParserVisitor(std::istream& stream) :
				_stream(stream)
		{}

		void visit(const char* name, std::uint32_t& field)
		{
			std::string token;
			if (!(_stream >> token) || token.starts_with("//"))
			{
				throw std::runtime_error(std::string("Missing field '") + name + "'");
			}
			const auto* first = token.data() + (token.front() == '+');
			const auto* last = token.data() + token.size();
			const auto parsed = std::from_chars(first, last, field);
			if (parsed.ptr != last || parsed.ec == std::errc::invalid_argument)
			{
				throw std::runtime_error(std::string("Invalid unsigned integer for field '") + name + "': " + token);
			}
			if (parsed.ec == std::errc::result_out_of_range)
			{
				throw std::runtime_error(std::string("Value out of range for field '") + name + "': " + token);
			}
		}

	private:
		std::istream& _stream;
	};
}
