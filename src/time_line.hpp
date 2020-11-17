#pragma once
#include <cstdint>
#include <chrono>
#include <string>
#include <vector>
#include <functional>

#include <Windows.h>

namespace SAV
{
	class TimeLine
	{
	public:
		using OnFrameChanged = std::function<void(const std::wstring&)>;

	public:
		TimeLine(HWND window, const OnFrameChanged& onFrameChanged);
		~TimeLine();

		TimeLine(const TimeLine&) = delete;

		void add(const std::wstring& frameName, std::chrono::milliseconds interval);
		void setLooped(bool value) { m_isLooped = value; }
		void play(bool isLooped, bool isReverInEnd);
		bool advance();
		void addInvertFrames();
		void reset();
		bool hasTimer(WPARAM timerID) const { return timerID == m_animationTimer; }
		std::uint32_t getFrame() { return m_frameIndex; }

	private:
		HWND m_parentHwnd;
		std::uint32_t m_current = 0;
		std::uint32_t m_frameIndex = 0;
		UINT_PTR m_animationTimer = 0;
		bool m_isLooped = false;
		std::vector<std::pair<std::wstring, std::chrono::milliseconds>> m_frames;
		OnFrameChanged m_onFrameChanged;
	};
}