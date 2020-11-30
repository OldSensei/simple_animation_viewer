#include <Windows.h>
#include <gdiplus.h>
#include <gdiplusheaders.h>
#include <CommCtrl.h>
#include <shobjidl_core.h>

#include <array>
#include <chrono>
#include <charconv>
#include <vector>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "resource.h"

#include "editable_list_view.hpp"
#include "image_cachable_canvas.hpp"
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


	using ListViewString = std::pair<std::array<wchar_t, 256>, std::array<wchar_t, 10>>;

	std::vector<ListViewString> gStringViewData;

	struct ImageData
	{
		ImageData(const std::wstring& nameValue, std::filesystem::path pathValue) :
			name{ nameValue },
			path{ std::move(pathValue) }
		{};

		ImageData(ImageData&&) = default;

		std::wstring name;
		std::filesystem::path path;
	};

	struct ApplicationData
	{
		std::vector<ImageData> imageData;
	} gAppData = {};

	struct ApplicationState
	{
		bool isExit = false;
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

	} gAppState = {};


	std::optional<std::chrono::milliseconds> fromWstring(const std::wstring& data)
	{
		std::uint64_t value = 0;
		char buf[10] = { 0 };

		int len = WideCharToMultiByte(CP_UTF8, 0, data.c_str(), static_cast<int>(data.length()), buf, 10, nullptr, nullptr);
		auto result = std::from_chars(buf, buf + len, value);
		if (result.ec == std::errc())
		{
			return std::optional<std::chrono::milliseconds>(std::chrono::milliseconds(value));
		}
		return std::nullopt;
	}

	bool loadData(std::wstring_view path, ApplicationData& data)
	{
		std::transform(std::filesystem::directory_iterator(std::filesystem::path(path)), std::filesystem::directory_iterator(), std::back_inserter(data.imageData),
			[](const auto& file)
			{ 
				return ImageData{ file.path().filename().wstring() , file.path()};
			});
		return true;
	}

	bool updateFileListView(const ApplicationData& data, ApplicationState& app)
	{
		std::vector<std::vector<std::wstring>> listViewData;
		for(const auto& file : data.imageData)
		{
			listViewData.push_back({file.name, L"0"});
		}
		app.appHandles.nfileList->updateData(listViewData);

		return true;
	}

	bool selectFolderDialog(ApplicationData& appData)
	{
		bool result = false;
		if (SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
		{
			IFileDialog* openDialog;
			if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&openDialog))))
			{
				DWORD dwOptions;
				if (SUCCEEDED(openDialog->GetOptions(&dwOptions)))
				{
					openDialog->SetOptions(dwOptions | FOS_PICKFOLDERS);

					if (SUCCEEDED(openDialog->Show(NULL)))
					{
						IShellItem* pItem;
						if (SUCCEEDED(openDialog->GetResult(&pItem)))
						{
							PWSTR folderPath;

							if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &folderPath)))
							{
								loadData(folderPath, appData);
								CoTaskMemFree(folderPath);
							}
							pItem->Release();
						}
					}
				}
			}

			CoUninitialize();
		}

		return result;
	}

	bool processPlayButton(ApplicationData& appData, ApplicationState& appState)
	{
		appState.appHandles.timeline->reset();
		auto data = appState.appHandles.nfileList->getListViewData();
		for (const auto& row : data)
		{
			const auto& name = row[0];
			const auto& timerString = row[1];

			auto value = fromWstring(timerString);
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
			selectFolderDialog(gAppData);
			updateFileListView(gAppData, appState);
			return true;
		}

		if (LOWORD(wp) == IDC_PLAY)
		{
			processPlayButton(gAppData, appState);
			return true;
		}

		if (LOWORD(wp) == IDC_STOP)
		{
			processStopButton(appState);
			return true;
		}

		if (LOWORD(wp) == ID_IMAGES_WRITEVIDEO)
		{
			if (appState.appHandles.nfileList)
			{
				auto data = appState.appHandles.nfileList->getListViewData();
				std::vector<std::pair<std::filesystem::path, std::chrono::milliseconds>> videoData;
				for(const auto& row : data)
				{
					auto durationValue = fromWstring(row[1]);
					auto it = std::find_if(gAppData.imageData.begin(), gAppData.imageData.end(),
											[name = row[0]](const auto& imgData) { return name == imgData.name; });

					if (it != gAppData.imageData.end() && durationValue)
					{
						videoData.push_back(std::pair<std::filesystem::path, std::chrono::milliseconds>{it->path, *durationValue});
					}
				}

				SAV::VideoFileCreator vfc{ 1024, 768, 800000 };
				vfc.write(videoData);
			}
		
		}
		return false;
	}

	LRESULT CALLBACK mainWindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
	{
		switch (msg)
		{
			case WM_NOTIFY:
				if (gAppState.appHandles.nfileList)
				{
					if (gAppState.appHandles.nfileList->processNotify(wp, lp))
					{
						return 1;
					}
				}
				return DefWindowProc(hwnd, msg, wp, lp);

			case WM_COMMAND:
				processChild(wp, gAppState);
				return 1;

			case WM_DESTROY:
				PostQuitMessage(0);
				gAppState.isExit = true;
				return 1;

			default:
				return DefWindowProc(hwnd, msg, wp, lp);
		}
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
		appState.appHandles.nfileList.emplace(appState.appHandles.appHandle, listViewRect,
			[&appState](const std::vector<std::wstring>& data)
			{
				if (!data.empty())
				{
					const auto& name = data[0];
					for (const auto& imgData : gAppData.imageData)
					{
						if (imgData.name == name)
						{
							appState.appHandles.imageCanvas->drawImage(imgData.path);
							return;
						}
					}
				}
			});
		appState.appHandles.nfileList->createHeaders(std::initializer_list<SAV::HeaderDescription>{ {L"Pictures", 70}, {L"Time", 30} });

		appState.appHandles.timeline.emplace(appState.appHandles.appHandle,
			[&appState](const std::wstring& name)
			{
				for (const auto& imgData : gAppData.imageData)
				{
					if (imgData.name == name)
					{
						appState.appHandles.imageCanvas->drawImage(imgData.path);
						return;
					}
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

	void shutdown()
	{
		gAppData.imageData.clear();
		gAppState.appHandles.imageCanvas.reset();
	}
}


int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	ULONG_PTR           gdiplusToken;

	Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

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

	::RECT viewportRect{ 0L, 0L, static_cast<LONG>(WINDOW_WIDTH), static_cast<LONG>(WINDOW_HEIGHT) };
	gAppState.windowRect = viewportRect;
	::AdjustWindowRect(&gAppState.windowRect, WS_OVERLAPPEDWINDOW, false);

	gAppState.appHandles.appHandle = ::CreateWindowEx(
		0,
		wndClsName,
		L"Simple.Animation.Viewer",
		WS_OVERLAPPEDWINDOW,
		10, 10,
		gAppState.windowRect.right - gAppState.windowRect.left,
		gAppState.windowRect.bottom - gAppState.windowRect.top,
		nullptr,
		nullptr,
		hInstance,
		nullptr
	);

	if (!gAppState.appHandles.appHandle)
	{
		::UnregisterClass(wndClsName, hInstance);
		return -1;
	}
	
	createAppWindows(hInstance, gAppState);

	::ShowWindow(gAppState.appHandles.appHandle, SW_SHOWDEFAULT);
	::UpdateWindow(gAppState.appHandles.appHandle);

	MSG msg = {};
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		::TranslateMessage(&msg);
		::DispatchMessage(&msg);
	}

	shutdown();
	Gdiplus::GdiplusShutdown(gdiplusToken);
	return 0;
}