#pragma once

#include <filesystem>
#include <map>
#include <optional>

#include <Windows.h>
#include <gdiplus.h>
#include <gdiplusheaders.h>

namespace SAV
{
	class ImageCachableCanvas
	{
	public:
		ImageCachableCanvas(HWND parent, const RECT& position);
		~ImageCachableCanvas() noexcept;

		void drawImage(const std::filesystem::path& imagePath);

	private:
		Gdiplus::Bitmap* getImage(const std::filesystem::path& imagePath);

	private:
		HWND m_handle;
		std::optional<Gdiplus::Graphics> m_graphics;
		int m_width;
		int m_height;

		std::map<std::wstring, std::unique_ptr<Gdiplus::Bitmap>> m_cache;
	};
}
