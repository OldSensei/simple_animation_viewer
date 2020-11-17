#include <Windows.h>
#include <CommCtrl.h>

#include <algorithm>
#include <iterator>

#include "time_line.hpp"

namespace
{
	LRESULT CALLBACK timelineSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
	{
		switch (message)
		{
			case WM_TIMER:
				if (auto tl = reinterpret_cast<SAV::TimeLine*>(dwRefData); tl && tl->hasTimer(wParam))
				{
					tl->advance();
				}
				return 1;
		}

		return ::DefSubclassProc(hwnd, message, wParam, lParam);
	}
}

namespace SAV
{
	TimeLine::TimeLine(HWND window, const OnFrameChanged& onFrameChanged) :
		m_parentHwnd(window),
		m_onFrameChanged(onFrameChanged)
	{
		SetWindowSubclass(m_parentHwnd, timelineSubclassProc, 1, (DWORD_PTR)this);
	}

	TimeLine::~TimeLine()
	{
		if (m_animationTimer != 0)
		{
			::KillTimer(m_parentHwnd, m_animationTimer);
			m_animationTimer = 0;
		}

		::RemoveWindowSubclass(m_parentHwnd, timelineSubclassProc, 1);
	}

	void TimeLine::add(const std::wstring& frameName, std::chrono::milliseconds interval)
	{
		m_frames.emplace_back(frameName, interval);
	}

	bool TimeLine::advance()
	{
		if (m_current == m_frames.size())
		{
			if (m_isLooped)
			{
				m_current = 0;
			}
			else
			{
				return false;
			}
		}

		auto& [frameName, timerCount] = m_frames.at(m_current++);

		m_onFrameChanged(frameName);

		if (m_animationTimer == 0)
		{
			m_animationTimer = 0x1;
		}
		m_animationTimer = SetTimer(m_parentHwnd, m_animationTimer, static_cast<UINT>(timerCount.count()), nullptr);
		return m_animationTimer != 0;
	}

	void TimeLine::play(bool isLooped, bool isReverInEnd)
	{
		m_isLooped = isLooped;
		if (isReverInEnd)
		{
			addInvertFrames();
		}
		m_current = 0;
		advance();
	}

	void TimeLine::addInvertFrames()
	{
		std::vector<std::pair<std::wstring, std::chrono::milliseconds>> newFrames;
		newFrames.reserve(m_frames.size() * 2);
		std::copy(m_frames.begin(), m_frames.end(), std::back_inserter(newFrames));
		std::copy(m_frames.rbegin(), m_frames.rend(), std::back_inserter(newFrames));
		m_frames = std::move(newFrames);
	}

	void TimeLine::reset()
	{
		if (m_animationTimer != 0)
		{
			::KillTimer(m_parentHwnd, m_animationTimer);
			m_animationTimer = 0;
		}

		m_current = 0;
		m_isLooped = false;
		m_frames.clear();
	}
}