#include <Windows.h>
#include <gdiplus.h>
#include <gdiplusheaders.h>
#include <CommCtrl.h>

#include <array>
#include <chrono>
#include <charconv>
#include <vector>
#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <string_view>

#include "resource.h"

#include "dialogs.hpp"
#include "editable_list_view.hpp"
#include "image_cachable_canvas.hpp"
#include "program_data.hpp"
#include "time_line.hpp"
#include "video_file_creator.hpp"

#pragma comment (lib,"Gdiplus.lib")
#pragma comment (lib,"Comctl32.lib")

namespace
{
	constexpr int WINDOW_WIDTH = 1480;
	constexpr int WINDOW_HEIGHT = 768;

	constexpr std::uint64_t IDC_TIMER_EDIT = 0x2;
	constexpr std::uint64_t IDC_LOOP_BOX = 0x3;
	constexpr std::uint64_t IDC_INVERSE_END_BOX = 0x4;
	constexpr std::uint64_t IDC_PLAY = 0x5;
	constexpr std::uint64_t IDC_STOP = 0x6;

	constexpr std::wstring_view APP_STATE_PROP = L"AppState";

	struct ApplicationState
	{
		SAV::AnimationData animationData;
		bool isExit = false;
		std::mutex vfc_mutex;
		std::unique_ptr<SAV::VideoFileCreator> vfc;
		std::optional<std::future<HRESULT>> conversionTask; 

		struct
		{
			HWND appHandle = nullptr;
			HWND canvas = nullptr;
			HWND playButton = nullptr;
			HWND stopButton = nullptr;
			HWND loopBox = nullptr;
			HWND invEndBox = nullptr;
			HWND timerEdit = nullptr;
			std::optional<SAV::EditableListView> nfileList;
			std::optional<SAV::ImageCachableCanvas> imageCanvas;
			std::optional<SAV::TimeLine> timeline;
		} appHandles;

		::RECT windowRect;
	};

	std::optional<std::uint64_t> fromWstring(const std::wstring& data)
	{
		std::uint64_t value = 0;
		char buf[10] = { 0 };

		int len = WideCharToMultiByte(CP_UTF8, 0, data.c_str(), static_cast<int>(data.length()), buf, 10, nullptr, nullptr);
		auto result = std::from_chars(buf, buf + len, value);
		if (result.ec == std::errc())
		{
			return std::optional<std::uint64_t>(value);
		}
		return std::nullopt;
	}

	std::optional<std::chrono::milliseconds> msFromWstring(const std::wstring& data)
	{
		auto ms = fromWstring(data);
		if (ms)
		{
			return std::optional<std::chrono::milliseconds>(ms);
		}
		return std::nullopt;
	}

	bool updateFileListView(SAV::AnimationData::Animations&& animations, SAV::EditableListView& listView)
	{
		std::vector<std::vector<std::wstring>> listViewData;

		for (auto&& animation : animations)
		{
			auto duration = std::to_wstring(animation.duration().count());
			listViewData.push_back({ animation.name(), std::move(duration) });
		}
		listView.updateData(listViewData);

		return true;
	}

	HRESULT doVideoConversion(const std::wstring& filename, std::uint32_t width, std::uint32_t height, ApplicationState* appState, HWND dlg)
	{
		auto data = appState->appHandles.nfileList->getListViewData();
		std::vector<SAV::AnimationDescription> videoData;
		for (const auto& row : data)
		{
			auto filepath = appState->animationData.getAnimationFilePath(row[0]);
			if (filepath)
			{
				videoData.push_back(SAV::AnimationDescription{ filepath->wstring(), row[1] });
			}
		}

		auto progressHWND = GetDlgItem(dlg, IDC_CREATION_PROGRESS);
		SendMessage(progressHWND, PBM_SETRANGE, 0, MAKELPARAM(0, videoData.size()));
		SendMessage(progressHWND, PBM_SETSTEP, (WPARAM)1, 0);
		
		std::unique_lock vfcLock(appState->vfc_mutex, std::defer_lock);

		vfcLock.lock();
		//L"output.mp4"
		appState->vfc = std::make_unique<SAV::VideoFileCreator>(filename, width, height, 14000000);
		vfcLock.unlock();

		auto result = appState->vfc->write(videoData,
								[progressHWND]()
								{
									PostMessage(progressHWND, PBM_STEPIT, 0, 0);
								});

		vfcLock.lock();
		appState->vfc.reset();
		vfcLock.unlock();

		PostMessage(dlg, WM_USER + 1, 0, 0);

		return result;
	}

	void makeVideo(HWND dlgHWND, ApplicationState& appState)
	{
		std::array<wchar_t, MAX_PATH> buffer = { 0 };

		GetDlgItemText(dlgHWND, IDC_W_EDIT, buffer.data(), buffer.size());
		auto width = fromWstring(buffer.data());
		GetDlgItemText(dlgHWND, IDC_H_EDIT, buffer.data(), buffer.size());
		auto height = fromWstring(buffer.data());
		GetDlgItemText(dlgHWND, IDC_FILE_NAME_EDIT, buffer.data(), buffer.size());
		auto filename = std::wstring(buffer.data());

		if (width && height)
		{
			appState.conversionTask.emplace( std::async( std::launch::async, doVideoConversion,
				filename,
				static_cast<std::uint32_t>(*width), static_cast<std::uint32_t>(*height),
				&appState, dlgHWND ) );
		}
	}

	void initDefaultVideoDlgOptions()
	{

	}

	bool processSaveVideoDlgCommand(WORD commandID, HWND dlgBoxhwnd, ApplicationState& appState)
	{
		switch(commandID)
		{
			case ID_START:
				makeVideo(dlgBoxhwnd, appState);
				return true;

			case ID_CANCEL:
				return true;

			case ID_SELECT:
			{
				auto filepath = SAV::saveFileDialog(SAV::program_save_video);
				if (filepath)
				{
					SetDlgItemText(dlgBoxhwnd, IDC_FILE_NAME_EDIT, filepath->c_str());
				}
			}
			return true;

			default:
				return false;
		}

		return false;
	}

	INT_PTR CALLBACK SaveVideoDlgProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		static ApplicationState* appState = nullptr;

		switch (message)
		{
			case WM_INITDIALOG:
				appState = reinterpret_cast<ApplicationState*>(lParam);
			return TRUE;

			case WM_COMMAND:
			{
				if (!processSaveVideoDlgCommand(LOWORD(wParam), hwnd, *appState))
				{
					return FALSE;
				}
			}
			return TRUE;

			case WM_USER+1:
				EndDialog(hwnd, appState->conversionTask->get() == S_OK ? 0 : 1);
				return TRUE;

			default:
				return FALSE;
		}
		return TRUE;
	}

	bool processPlayButton(ApplicationState& appState)
	{
		appState.appHandles.timeline->reset();
		auto data = appState.appHandles.nfileList->getListViewData();
		for (const auto& row : data)
		{
			const auto& name = row[0];
			const auto& timerString = row[1];

			auto value = msFromWstring(timerString);
			if (value)
			{
				appState.appHandles.timeline->add(name, *value);
			}
		}

		bool isLooped = SendMessage(appState.appHandles.loopBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
		bool isInvert = SendMessage(appState.appHandles.invEndBox, BM_GETCHECK, 0, 0) == BST_CHECKED;

		appState.appHandles.timeline->play(isLooped, isInvert);

		return true;
	}

	bool processStopButton(ApplicationState& appState)
	{
		appState.appHandles.timeline->reset();
		return true;
	}

	bool processChild(WPARAM wp, ApplicationState& appState)
	{
		if (LOWORD(wp) == ID_IMAGE_ADDFOLDER)
		{
			auto folder = SAV::selectFolderDialog();
			if (folder)
			{
				auto&& animations = appState.animationData.loadFromFolder(std::filesystem::path(*folder));
				updateFileListView(std::move(animations), *appState.appHandles.nfileList);
			}
			return true;
		}

		if (LOWORD(wp) == IDC_PLAY)
		{
			processPlayButton(appState);
			return true;
		}

		if (LOWORD(wp) == IDC_STOP)
		{
			processStopButton(appState);
			return true;
		}

		if (LOWORD(wp) == ID_IMAGES_WRITEVIDEO)
		{
			DialogBoxParam(nullptr,
							MAKEINTRESOURCE(IDD_SAVE_VIDEO_DIALOG),
							appState.appHandles.appHandle,
							SaveVideoDlgProc, reinterpret_cast<LPARAM>(&appState));
			return true;
		}

		if (LOWORD(wp) == ID_PROGRAMM_SAVE)
		{
			auto filepath = SAV::saveFileDialog(SAV::program_save_data);
			if (filepath)
			{
				auto data = appState.appHandles.nfileList->getListViewData();
				appState.animationData.saveToFile( *filepath, data );
			}
			return true;
		}

		if (LOWORD(wp) == ID_PROGRAMM_LOAD)
		{
			auto filepath = SAV::loadFileDialog(SAV::program_save_data);
			if (filepath)
			{
				auto&& animations = appState.animationData.loadFromFile(*filepath);
				updateFileListView(std::move(animations), *appState.appHandles.nfileList);
			}
			return true;
		}
		return false;
	}

	LRESULT CALLBACK mainWindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
	{
		if (msg == WM_CREATE)
		{
			LPCREATESTRUCT cs = reinterpret_cast<LPCREATESTRUCT>(lp);
			auto state = reinterpret_cast<ApplicationState*>(cs->lpCreateParams);
			SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
		}
		else
		{
			auto appState = reinterpret_cast<ApplicationState*>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));
			switch (msg)
			{
				case WM_NOTIFY:
				{
					auto [isProcessed, returnedCode] = appState->appHandles.nfileList->processNotify(wp, lp);
					if (isProcessed)
					{
						return returnedCode;
					}
					break;
				}

				case WM_COMMAND:
					processChild(wp, *appState);
					return 0;

				case WM_MOUSEMOVE:
					if (appState->appHandles.nfileList->processMouseMoving(lp))
					{
						return 0;
					}
					break;

				case WM_LBUTTONUP:
					appState->appHandles.nfileList->processEndDragAndDrop(lp);
					break;

				case WM_DESTROY:
					PostQuitMessage(0);
					appState->isExit = true;
					return 0;
			}
		}
		return DefWindowProc(hwnd, msg, wp, lp);
	}

	bool createAppWindows(HINSTANCE hInstance, ApplicationState& appState)
	{
		appState.appHandles.imageCanvas.emplace(appState.appHandles.appHandle, ::RECT{10, 10, 1110, 710});

		int buttonWidth = 350;
		int buttonHeight = 20;

		int left = appState.windowRect.right - buttonWidth - 10;

		int listHeight = 300;
		int listTop = 10 ;
		::RECT listViewRect{left, listTop, left + buttonWidth, listTop + listHeight };
		appState.appHandles.nfileList.emplace(appState.appHandles.appHandle, listViewRect);
		appState.appHandles.nfileList->setOnSelectHandler(
			[&appState](const std::vector<std::wstring>& data)
			{
				if (!data.empty())
				{
					const auto& name = data[0];
					auto animationFilePath = appState.animationData.getAnimationFilePath(name);
					if (animationFilePath)
					{
						appState.appHandles.imageCanvas->drawImage(*animationFilePath);
					}
				}
			});

		appState.appHandles.nfileList->createHeaders(std::initializer_list<SAV::HeaderDescription>{ {L"Pictures", 70}, {L"Time", 30} });

		appState.appHandles.timeline.emplace(appState.appHandles.appHandle,
			[&appState](const std::wstring& name)
			{
				auto aimationFilePath = appState.animationData.getAnimationFilePath(name);
				if (aimationFilePath)
				{
					appState.appHandles.imageCanvas->drawImage(*aimationFilePath);
				}
			});

		int boxesTop = listViewRect.bottom + 10;
		appState.appHandles.loopBox = ::CreateWindow(
			WC_BUTTON,
			L"loop",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
			left, boxesTop,
			buttonWidth - 200, buttonHeight,
			appState.appHandles.appHandle,
			reinterpret_cast<HMENU>(IDC_LOOP_BOX),
			hInstance,
			NULL);

		appState.appHandles.invEndBox = ::CreateWindow(
			WC_BUTTON,
			L"invert in the end",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTOCHECKBOX,
			left + 200, boxesTop,
			buttonWidth - 200, buttonHeight,
			appState.appHandles.appHandle,
			reinterpret_cast<HMENU>(IDC_INVERSE_END_BOX),
			hInstance,
			NULL);

		int playButtonTop = boxesTop + buttonHeight + 10;
		appState.appHandles.playButton = ::CreateWindow(
			WC_BUTTON,
			L"play",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
			left, playButtonTop,
			buttonWidth - 200, buttonHeight,
			appState.appHandles.appHandle,
			reinterpret_cast<HMENU>(IDC_PLAY),
			hInstance,
			NULL);

		appState.appHandles.stopButton = ::CreateWindow(
			WC_BUTTON,
			L"stop",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
			left + 200, playButtonTop,
			buttonWidth - 200, buttonHeight,
			appState.appHandles.appHandle,
			reinterpret_cast<HMENU>(IDC_STOP),
			hInstance,
			NULL);

		return true;
	}

	struct GdiPlusDeleter
	{
		using pointer = ULONG_PTR;
		void operator()(pointer& p)
		{
			if (p)
			{
				Gdiplus::GdiplusShutdown(p);
			}
		}
	};
}


int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	ULONG_PTR           gdiplusToken;
	Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
	std::unique_ptr<ULONG_PTR, GdiPlusDeleter> gdiplusHandle(gdiplusToken, GdiPlusDeleter());

	constexpr const  wchar_t* wndClsName = L"Simple.Animation.Viewer.window";

	WNDCLASSEX wndclass;
	ZeroMemory(&wndclass, sizeof(WNDCLASSEX));

	wndclass.cbSize = sizeof(WNDCLASSEX);
	wndclass.style = CS_HREDRAW | CS_VREDRAW;
	wndclass.lpfnWndProc = mainWindowProc;
	wndclass.lpszClassName = wndClsName;
	wndclass.hInstance = hInstance;
	wndclass.hbrBackground = static_cast<HBRUSH>(::GetStockObject(GRAY_BRUSH));
	wndclass.hCursor = static_cast<HCURSOR>(::LoadCursor(0, IDC_ARROW));
	wndclass.hIcon = 0;
	wndclass.hIconSm = 0;
	wndclass.lpszMenuName = MAKEINTRESOURCE(IDR_IMAGE_MENU);
	wndclass.cbClsExtra = 0;
	wndclass.cbWndExtra = 0;

	::RegisterClassEx(&wndclass);

	ApplicationState appState;
	::RECT viewportRect{ 0L, 0L, static_cast<LONG>(WINDOW_WIDTH), static_cast<LONG>(WINDOW_HEIGHT) };
	::AdjustWindowRect(&viewportRect, WS_OVERLAPPEDWINDOW, false);
	appState.windowRect = viewportRect;

	appState.appHandles.appHandle = ::CreateWindowEx(
		0,
		wndClsName,
		L"Simple.Animation.Viewer",
		WS_OVERLAPPEDWINDOW,
		10, 10,
		appState.windowRect.right - appState.windowRect.left,
		appState.windowRect.bottom - appState.windowRect.top,
		nullptr,
		nullptr,
		hInstance,
		&appState
	);

	if (!appState.appHandles.appHandle)
	{
		::UnregisterClass(wndClsName, hInstance);
		return -1;
	}

	createAppWindows(hInstance, appState);

	::ShowWindow(appState.appHandles.appHandle, SW_SHOWDEFAULT);
	::UpdateWindow(appState.appHandles.appHandle);

	MSG msg = {};
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		::TranslateMessage(&msg);
		::DispatchMessage(&msg);
	}

	return 0;
}