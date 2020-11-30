#pragma once
#include <mfidl.h>
#include <mfreadwrite.h>
#include <winrt/base.h>
#include <gdiplus.h>
#include <gdiplusheaders.h>

#include <filesystem>
#include <cstdint>
#include <chrono>
#include <memory>


#include "time_line.hpp"

namespace SAV
{
	class VideoFileCreator
	{
	public:
		VideoFileCreator(std::uint32_t width, std::uint32_t height, std::uint32_t bitrate);
		HRESULT write(const std::vector<std::pair<std::filesystem::path, std::chrono::milliseconds>>& data);

	private:
		HRESULT writeFrame(std::unique_ptr<Gdiplus::Bitmap>& frame, std::uint32_t frameDuration);
		HRESULT initializeSinkWriter();

	private:
		std::uint32_t m_width;
		std::uint32_t m_height;
		std::uint32_t m_bitrate;
		std::uint32_t m_fps;
		float m_frameDuration;
		std::uint64_t m_frameTimestamp;
		DWORD m_videoStreamIndex;
		winrt::com_ptr<IMFSinkWriter> m_sinkWriter;
	};
}
