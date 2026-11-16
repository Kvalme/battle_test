#include "IO/Config/UnitLoader.hpp"

#include "Core/Abilities.hpp"
#include "Core/AbilityManager.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <pugixml.hpp>
#include <set>
#include <stdexcept>
#include <utility>

namespace sw::io
{
	namespace
	{

		struct Element
		{
			pugi::xml_node node;
			std::string_view source;

			explicit operator bool() const
			{
				return static_cast<bool>(node);
			}
		};

		using AttributeNames = std::initializer_list<std::string_view>;

		std::string_view nameOf(Element element)
		{
			return element.node.name();
		}

		std::string location(std::string_view xml, std::size_t offset)
		{
			const auto before = xml.substr(0, offset);
			std::size_t line = 1;
			char previous{};
			for (const auto ch : before)
			{
				if (ch == '\r' || (ch == '\n' && previous != '\r'))
				{
					++line;
				}
				previous = ch;
			}
			return "Unit XML line " + std::to_string(line);
		}

		[[noreturn]]
		void invalid(Element element, const std::string& reason)
		{
			const auto offset = std::max<std::ptrdiff_t>(0, element.node.offset_debug());
			const auto name = nameOf(element);
			auto context = location(element.source, static_cast<std::size_t>(offset));
			if (!name.empty())
			{
				context += " <" + std::string(name) + ">";
			}
			for (auto node = element.node; node; node = node.parent())
			{
				const std::string_view tag = node.name();
				if (tag == "unit" && node != element.node && node.attribute("name"))
				{
					context += " in unit '" + std::string(node.attribute("name").value()) + "'";
				}
				if (tag == "ability")
				{
					if (const auto implementation = node.attribute("implementation"))
					{
						context += " for ability (" + std::string(implementation.value()) + ")";
					}
					if (const auto abilityName = node.attribute("name"))
					{
						context += " '" + std::string(abilityName.value()) + "'";
					}
				}
			}
			if (name == "property" && element.node.attribute("name"))
			{
				context += " property '" + std::string(element.node.attribute("name").value()) + "'";
			}
			throw std::runtime_error(context + ": " + reason);
		}

		void checkAttributes(Element element, AttributeNames allowed)
		{
			if (nameOf(element).find(':') != std::string_view::npos)
			{
				invalid(element, "namespaces are unsupported");
			}

			std::set<std::string_view> names;
			for (const auto attr : element.node.attributes())
			{
				const std::string_view name = attr.name();
				if (name == "xmlns" || name.find(':') != std::string_view::npos)
				{
					invalid(element, "namespaces are unsupported");
				}
				if (std::ranges::find(allowed, name) == allowed.end())
				{
					invalid(element, "unexpected attribute '" + std::string(name) + "'");
				}
				if (!names.insert(name).second)
				{
					invalid(element, "duplicate attribute '" + std::string(name) + "'");
				}
			}
		}

		std::optional<std::string> optionalAttribute(Element element, const char* name)
		{
			const auto attribute = element.node.attribute(name);
			if (!attribute)
			{
				return std::nullopt;
			}
			return std::string(attribute.value());
		}

		std::string identifier(Element element, const char* name)
		{
			auto value = optionalAttribute(element, name);
			if (!value || value->find_first_not_of(" \t\r\n") == std::string::npos)
			{
				invalid(element, std::string("expected nonempty '") + name + "' attribute");
			}
			return std::move(*value);
		}

		bool trivia(pugi::xml_node node)
		{
			return node.type() == pugi::node_comment
				   || (node.type() == pugi::node_pcdata
					   && std::string_view(node.value()).find_first_not_of(" \t\r\n") == std::string_view::npos);
		}

		std::vector<Element> children(Element element)
		{
			std::vector<Element> result;
			for (const auto child : element.node.children())
			{
				if (child.type() == pugi::node_element)
				{
					result.push_back({child, element.source});
				}
				else if (!trivia(child))
				{
					invalid({child, element.source}, "expected only child elements");
				}
			}
			return result;
		}

		void checkEmpty(Element element)
		{
			if (!children(element).empty())
			{
				invalid(element, "child elements are unsupported");
			}
		}

		template <class Number>
		Number number(Element element, std::string_view value, const char* label)
		{
			Number result{};
			const auto end = value.data() + value.size();
			const auto parsed = std::from_chars(value.data(), end, result);
			if (parsed.ec != std::errc{} || parsed.ptr != end)
			{
				invalid(element, std::string("invalid number for '") + label + "'");
			}
			return result;
		}

		bool boolean(Element element, std::string_view value, const char* label)
		{
			if (value == "true" || value == "1")
			{
				return true;
			}
			if (value == "false" || value == "0")
			{
				return false;
			}
			invalid(element, std::string("invalid boolean for '") + label + "'");
		}

		core::PropertyValue propertyValue(Element element)
		{
			const auto type = identifier(element, "type");
			auto value = optionalAttribute(element, "value");
			if (!value)
			{
				invalid(element, "missing 'value' attribute");
			}

			if (type == "int")
			{
				return number<std::int64_t>(element, *value, "value");
			}
			if (type == "bool")
			{
				return boolean(element, *value, "value");
			}
			if (type == "string")
			{
				return std::move(*value);
			}
			if (type == "double")
			{
				const auto result = number<double>(element, *value, "value");
				if (!std::isfinite(result))
				{
					invalid(element, "'value' must be finite");
				}
				return result;
			}
			invalid(element, "unknown property type '" + type + "'");
		}

		core::PropertyMap parseProperties(Element element)
		{
			checkAttributes(element, {});
			core::PropertyMap result;
			for (const auto property : children(element))
			{
				if (nameOf(property) != "property")
				{
					invalid(property, "expected <property>");
				}
				checkAttributes(property, {"name", "type", "value"});
				checkEmpty(property);
				const auto name = identifier(property, "name");
				if (!result.emplace(name, propertyValue(property)).second)
				{
					invalid(property, "duplicate property '" + name + "'");
				}
			}
			return result;
		}

		core::AbilityConfig parseAbility(Element element)
		{
			checkAttributes(element, {"implementation", "name"});
			core::AbilityConfig ability;
			ability.implementation = identifier(element, "implementation");
			if (element.node.attribute("name"))
			{
				ability.name = identifier(element, "name");
			}

			const auto sections = children(element);
			if (!sections.empty())
			{
				if (sections.size() != 1 || nameOf(sections.front()) != "properties")
				{
					invalid(element, "expected at most one <properties> section");
				}
				ability.properties = parseProperties(sections.front());
			}
			return ability;
		}

		core::UnitTemplate parseUnit(Element element, std::string_view filename)
		{
			checkAttributes(element, {"name"});
			core::UnitTemplate unit;
			unit.type = identifier(element, "name");

			Element properties{};
			Element abilities{};
			for (const auto child : children(element))
			{
				const auto name = nameOf(child);
				if (name == "properties" && !properties)
				{
					properties = child;
				}
				else if (name == "abilities" && !abilities)
				{
					abilities = child;
				}
				else
				{
					invalid(child, "expected at most one <properties> and exactly one <abilities> section");
				}
			}
			if (!abilities)
			{
				invalid(element, "expected <abilities> section for unit '" + unit.type + "'");
			}
			if (properties)
			{
				unit.defaults = parseProperties(properties);
			}

			checkAttributes(abilities, {});
			std::set<std::string> names;
			for (const auto entry : children(abilities))
			{
				if (nameOf(entry) != "ability")
				{
					invalid(entry, "expected <ability>");
				}
				auto ability = parseAbility(entry);
				if (!ability.name.empty() && !names.insert(ability.name).second)
				{
					invalid(entry, "duplicate ability name");
				}
				unit.abilities.push_back(std::move(ability));
			}
			if (unit.abilities.empty())
			{
				invalid(abilities, "expected at least one ability");
			}

			auto effectiveProperties = [&]
			{
				try
				{
					return core::resolveUnitProperties(unit.defaults);
				}
				catch (const std::invalid_argument& error)
				{
					invalid(element, "unit '" + unit.type + "': " + error.what());
				}
			}();
			const auto hp = core::integerProperty(effectiveProperties, "hp");
			if (hp < 1 || hp > std::numeric_limits<std::uint32_t>::max())
			{
				invalid(element, "unit '" + unit.type + "': property 'hp' is out of range");
			}
			effectiveProperties.erase("hp");

			try
			{
				core::AbilityManager::instance()->createAll(unit, effectiveProperties);
			}
			catch (const std::invalid_argument& error)
			{
				invalid(element, error.what());
			}
			catch (const std::runtime_error& error)
			{
				invalid(element, error.what());
			}
			// The caller already reports the filename for load errors. Retain it for spawn errors.
			unit.source = filename;
			return unit;
		}

		bool xmlCharacter(std::uint32_t value)
		{
			return value == 0x9 || value == 0xA || value == 0xD || (value >= 0x20 && value <= 0xD7FF)
				   || (value >= 0xE000 && value <= 0xFFFD) || (value >= 0x10000 && value <= 0x10FFFF);
		}

		bool validEntity(std::string_view name)
		{
			if (name == "amp" || name == "lt" || name == "gt" || name == "apos" || name == "quot")
			{
				return true;
			}
			if (!name.starts_with('#'))
			{
				return false;
			}

			name.remove_prefix(1);
			const bool hex = name.starts_with('x');
			if (hex)
			{
				name.remove_prefix(1);
			}
			std::uint32_t value{};
			const auto end = name.data() + name.size();
			const auto parsed = std::from_chars(name.data(), end, value, hex ? 16 : 10);
			return parsed.ec == std::errc{} && parsed.ptr == end && xmlCharacter(value);
		}

		void checkSource(std::string_view xml)
		{
			for (std::size_t i = 0; i < xml.size(); ++i)
			{
				if (!xmlCharacter(static_cast<unsigned char>(xml[i])))
				{
					throw std::runtime_error(location(xml, i) + ": invalid XML character");
				}
			}

			// pugixml leaves unrecognized entity references unchanged.
			for (auto offset = xml.find_first_of("<&"); offset != xml.npos;
				 offset = xml.find_first_of("<&", offset + 1))
			{
				const auto rest = xml.substr(offset);
				if (rest.starts_with("<!DOCTYPE"))
				{
					throw std::runtime_error(location(xml, offset) + ": DTDs and custom entities are unsupported");
				}

				std::string_view terminator;
				if (rest.starts_with("<!--"))
				{
					terminator = "-->";
				}
				else if (rest.starts_with("<![CDATA["))
				{
					terminator = "]]>";
				}
				else if (rest.starts_with("<?"))
				{
					terminator = "?>";
				}
				if (!terminator.empty())
				{
					const auto end = xml.find(terminator, offset + 2);
					if (end == xml.npos)
					{
						throw std::runtime_error(location(xml, offset) + ": unterminated XML markup");
					}
					offset = end + terminator.size() - 1;
				}
				else if (rest.starts_with('&'))
				{
					const auto end = rest.find_first_of(";<& \t\r\n", 1);
					if (end == rest.npos || rest[end] != ';' || !validEntity(rest.substr(1, end - 1)))
					{
						throw std::runtime_error(location(xml, offset) + ": invalid XML entity reference");
					}
					offset += end;
				}
			}
		}

		core::UnitTemplates parseTemplates(std::string_view xml, bool standaloneOnly, std::string_view filename = {})
		{
			if (xml.empty() || xml.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
			{
				throw std::runtime_error("Unit XML is empty or too large");
			}
			checkSource(xml);

			pugi::xml_document doc;
			// Keep top-level text so it can be rejected alongside extra root elements.
			const auto parsed = doc.load_buffer(
					xml.data(),
					xml.size(),
					pugi::parse_full | pugi::parse_ws_pcdata | pugi::parse_fragment,
					pugi::encoding_utf8);
			if (!parsed)
			{
				throw std::runtime_error(
						location(xml, static_cast<std::size_t>(std::max<std::ptrdiff_t>(0, parsed.offset))) + ": "
						+ parsed.description());
			}

			Element root{};
			for (const auto node : doc.children())
			{
				const Element element{node, xml};
				if (node.type() == pugi::node_declaration && node == doc.first_child())
				{
					checkAttributes(element, {"version", "encoding", "standalone"});
				}
				else if (node.type() == pugi::node_element && !root)
				{
					root = element;
				}
				else if (!trivia(node))
				{
					invalid(element, "expected a single XML root");
				}
			}
			if (!root)
			{
				throw std::runtime_error("Unit XML requires a root element");
			}

			core::UnitTemplates units;
			if (nameOf(root) == "unit")
			{
				auto unit = parseUnit(root, filename);
				auto name = unit.type;
				units.emplace(std::move(name), std::move(unit));
				return units;
			}
			if (standaloneOnly || nameOf(root) != "units")
			{
				invalid(root, standaloneOnly ? "expected <unit> root" : "expected <unit> or <units> root");
			}
			checkAttributes(root, {});
			for (const auto element : children(root))
			{
				if (nameOf(element) != "unit")
				{
					invalid(element, "expected <unit>");
				}
				auto unit = parseUnit(element, filename);
				const auto name = unit.type;
				if (!units.emplace(name, std::move(unit)).second)
				{
					invalid(element, "duplicate unit '" + name + "'");
				}
			}
			if (units.empty())
			{
				invalid(root, "expected at least one unit");
			}
			return units;
		}

		std::string readUnitXml(const std::filesystem::path& path)
		{
			std::ifstream file(path, std::ios::binary);
			if (!file)
			{
				throw std::runtime_error("Could not open unit XML file '" + path.string() + "'");
			}
			std::string xml;
			char buffer[4096];
			while (file.read(buffer, sizeof(buffer)) || file.gcount() != 0)
			{
				xml.append(buffer, static_cast<std::size_t>(file.gcount()));
			}
			if (file.bad())
			{
				throw std::runtime_error("Could not read unit XML file '" + path.string() + "'");
			}
			return xml;
		}

		void appendDocument(
				core::UnitTemplates& units, std::map<std::string, std::string>& origins, UnitXmlDocument document)
		{
			core::UnitTemplates parsed;
			try
			{
				parsed = parseTemplates(document.xml, true, document.filename);
			}
			catch (const std::runtime_error& error)
			{
				throw std::runtime_error(std::string(document.filename) + ": " + error.what());
			}

			auto unit = parsed.extract(parsed.begin());
			const auto [origin, inserted] = origins.try_emplace(unit.key(), document.filename);
			if (!inserted)
			{
				throw std::runtime_error(
						"Duplicate unit '" + unit.key() + "' in '" + origin->second + "' and '"
						+ std::string(document.filename) + "'");
			}
			units.insert(std::move(unit));
		}

	}  // namespace

	core::UnitTemplates loadUnitTemplates(std::string_view xml)
	{
		return parseTemplates(xml, false);
	}

	core::UnitTemplates loadUnitTemplates(std::span<const UnitXmlDocument> documents)
	{
		if (documents.empty())
		{
			throw std::runtime_error("No embedded unit XML documents");
		}
		core::UnitTemplates units;
		std::map<std::string, std::string> origins;
		for (const auto& document : documents)
		{
			appendDocument(units, origins, document);
		}
		return units;
	}

	core::UnitTemplates loadUnitTemplatesFile(const std::filesystem::path& path)
	{
		const auto xml = readUnitXml(path);
		try
		{
			return parseTemplates(xml, false, path.string());
		}
		catch (const std::runtime_error& error)
		{
			throw std::runtime_error(path.string() + ": " + error.what());
		}
	}

	core::UnitTemplates loadUnitTemplatesDirectory(const std::filesystem::path& directory)
	{
		std::vector<std::filesystem::path> paths;
		for (const auto& entry : std::filesystem::directory_iterator(directory))
		{
			if (entry.path().extension() == ".xml" && entry.is_regular_file())
			{
				paths.push_back(entry.path());
			}
		}
		std::ranges::sort(paths);
		if (paths.empty())
		{
			throw std::runtime_error("No unit XML documents in '" + directory.string() + "'");
		}

		core::UnitTemplates units;
		std::map<std::string, std::string> origins;
		for (const auto& path : paths)
		{
			const auto filename = path.string();
			const auto xml = readUnitXml(path);
			appendDocument(units, origins, {filename, xml});
		}
		return units;
	}

}  // namespace sw::io
