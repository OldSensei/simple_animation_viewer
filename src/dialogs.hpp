#pragma once

#include <optional>
#include <string>

namespace SAV
{
	std::optional<std::wstring> saveFileDialog();
	std::optional<std::wstring> loadFileDialog();
	std::optional<std::wstring> selectFolderDialog();
}
