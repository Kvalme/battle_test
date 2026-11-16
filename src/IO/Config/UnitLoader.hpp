#pragma once

#include "Core/Config.hpp"

#include <filesystem>
#include <span>
#include <string_view>

namespace sw::io
{

	struct UnitXmlDocument
	{
		std::string_view filename;
		std::string_view xml;
	};

	// Loads unit definitions and validates them against the linked ability registry.
	// Invalid XML or configuration is reported as std::runtime_error.
	core::UnitTemplates loadUnitTemplates(std::string_view xml);
	core::UnitTemplates loadUnitTemplates(std::span<const UnitXmlDocument> documents);
	core::UnitTemplates loadUnitTemplatesDirectory(const std::filesystem::path& directory);
	core::UnitTemplates loadUnitTemplatesFile(const std::filesystem::path& path);

}
