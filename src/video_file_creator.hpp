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
#include <ratio>

#include "program_data.hpp"
#include "time_line.hpp"

namespace SAV
{
	class Bitrate
	{
	public:
		using KBPS = std::kilo;
		using MBPS = std::mega;
		using BPS = std::ratio<1, 1>;

	public:
		template<std::uint32_t N, std::uint32_t D>
		Bitrate(std::uint32_t value, const std::ratio<N, D>& ratio) : 
			bps{ value * static_cast<std::uint32_t>(ratio.num) / static_cast<std::uint32_t>(ratio.den) }
		{};

		std::uint32_t value() const { return bps; }

	private:
		std::uint32_t bps;
	};

	class VideoFileCreator
	{
	public:
		VideoFileCreator(std::wstring_view filename, std::uint32_t width, std::uint32_t height, const Bitrate& bitrate) :
			VideoFileCreator(filename, width, height, bitrate.value())
		{}

		HRESULT write(const std::vector<AnimationDescription>& data, std::function<void()> progressCallback = nullptr);

		void cancel() { m_isCanceled = true; };
	private:
		VideoFileCreator(std::wstring_view filename, std::uint32_t width, std::uint32_t height, std::uint32_t bitrate);

	private:
		HRESULT writeFrame(std::unique_ptr<Gdiplus::Bitmap>& frame, std::uint32_t frameDuration);
		HRESULT initializeSinkWriter();

	private:
		std::uint32_t m_width;
		std::uint32_t m_height;
		std::uint32_t m_bitrate;
		std::uint32_t m_fps;
		std::wstring m_filename;
		float m_frameDuration;
		std::uint64_t m_frameTimestamp;
		DWORD m_videoStreamIndex;
		winrt::com_ptr<IMFSinkWriter> m_sinkWriter;
		bool m_isCanceled;
	};
}
