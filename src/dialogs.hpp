#pragma once

#include <optional>
#include <string>
#include <shtypes.h>

namespace SAV
{
	struct SaveFileData
	{
		std::array<COMDLG_FILTERSPEC, 1> filter;
		std::wstring_view extension;
	};

	inline constexpr SaveFileData program_save_data = { {L"Simple Animation Viwer data (*.sav)", L"*.sav"}, L"sav" };
	inline constexpr SaveFileData program_save_video = { {L"mp4 video file (*.mp4)", L"*.mp4"}, L"mp4" };

	//inline constexpr std::array<COMDLG_FILTERSPEC, 1> program_data_filter = { {L"Simple Animation Viwer data (*.sav)", L"*.sav"} };
	//inline constexpr std::array<COMDLG_FILTERSPEC, 1> program_video_filter = { {L"mp4 video file (*.mp4)", L"*.mp4"} };
	
	std::optional<std::wstring> saveFileDialog( const SaveFileData& options );
	std::optional<std::wstring> loadFileDialog( const SaveFileData& options );
	std::optional<std::wstring> selectFolderDialog();
}
