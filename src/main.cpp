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
#include "layout.hpp"
#include "program_data.hpp"
#include "time_line.hpp"
#include "video_file_creator.hpp"
#include "utils.hpp"

#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#pragma comment (lib,"Gdiplus.lib")
#pragma comment (lib,"Comctl32.lib")

namespace
{
	constexpr int WINDOW_WIDTH = 1480;
	constexpr int WINDOW_HEIGHT = 768;

	constexpr std::uint64_t IDC_TIMER_EDIT = 0x2;
	constexpr std::uint64_t IDC_LOOP_BOX = 0x3;
	constexpr std::uint64_t IDC_PLAY = 0x5;

	constexpr std::wstring_view APP_STATE_PROP = L"AppState";
	constexpr std::uint32_t WM_CONVERSION_FINISHED = WM_USER + 1;

	constexpr std::wstring_view DEFAULT_VIDEO_WIDTH_TXT_VALUE = L"1920";
	constexpr std::wstring_view DEFAULT_VIDEO_HEIGHT_TXT_VALUE = L"1080";
	constexpr std::wstring_view DEFAULT_VIDEO_BITRATE_TXT_VALUE = L"8000";
	constexpr std::wstring_view DEFAULT_VIDEO_FILENAME_VALUE = L"output.mp4";

	constexpr std::string_view LAYOUT_ROOT_NAME = "Root";
	constexpr std::string_view LAYOUT_IMAGE_CANVAS_NAME = "ImageCanvas";
	constexpr std::string_view LAYOUT_TIME_LINE_NAME = "TimeLine";
	constexpr std::string_view LAYOUT_PLAY_BUTTON_NAME = "PlayButton";

	struct VideoConversionOptions
	{
		std::uint32_t width;
		std::uint32_t height;
		SAV::Bitrate bitrate;
		std::wstring filename;
	};

	struct ApplicationState
	{
		SAV::AnimationData animationData;
		bool isExit = false;
		std::mutex vfc_mutex;
		std::unique_ptr<SAV::VideoFileCreator> vfc;
		std::optional<std::future<HRESULT>> conversionTask; 
		std::optional<SAV::Layout::BoxLayout> layout;

		struct
		{
			HWND appHandle = nullptr;
			HWND playButton = nullptr;
			HWND loopBox = nullptr;
			HWND timerEdit = nullptr;
			std::optional<SAV::EditableListView> nfileList;
			std::optional<SAV::ImageCachableCanvas> imageCanvas;
			std::optional<SAV::TimeLine> timeline;
		} appHandles;
	};

	std::optional<std::chrono::milliseconds> msFromWstring(const std::wstring& data)
	{
		auto ms = SAV::Utils::fromString(data);
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

	HRESULT doVideoConversion(const VideoConversionOptions& options, ApplicationState* appState, HWND dlg)
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
		appState->vfc = std::make_unique<SAV::VideoFileCreator>(options.filename, options.width, options.height, options.bitrate);
		vfcLock.unlock();

		auto result = appState->vfc->write(videoData,
								[progressHWND]()
								{
									PostMessage(progressHWND, PBM_STEPIT, 0, 0);
								});

		vfcLock.lock();
		appState->vfc.reset();
		vfcLock.unlock();

		::Sleep(1000);
		PostMessage(dlg, WM_CONVERSION_FINISHED, 0, 0);

		return result;
	}

	template<typename T>
	std::optional<T> getValueFromDlgItem(HWND dlgHWND, int dialogItemId)
	{
#ifdef UNICODE
		using SymbolType = wchar_t;
#else
		using SymbolType = char;
#endif // UNICODE
		std::array<SymbolType, MAX_PATH> buffer = { 0 };
		GetDlgItemText(dlgHWND, dialogItemId, buffer.data(), static_cast<int>(buffer.size()));

		if constexpr (std::is_integral_v<T>)
		{
			if (auto value = SAV::Utils::fromString<std::wstring>(buffer.data()); value)
			{
				return std::optional{ static_cast<T>(*value) };
			}
		}
		else
		{
			return std::optional{ T{ buffer.data() } };
		}

		return std::nullopt;
	}

	void makeVideo(HWND dlgHWND, ApplicationState& appState)
	{
		auto width = getValueFromDlgItem<std::uint32_t>(dlgHWND, IDC_W_EDIT);
		auto height = getValueFromDlgItem<std::uint32_t>(dlgHWND, IDC_H_EDIT);
		auto bitrate = getValueFromDlgItem<std::uint32_t>(dlgHWND, IDC_BITRATE_EDIT);
		auto filename = getValueFromDlgItem<std::wstring>(dlgHWND, IDC_FILE_NAME_EDIT);

		if (width && height && bitrate && filename && !filename->empty())
		{
			appState.conversionTask.emplace( std::async( std::launch::async, doVideoConversion,
				VideoConversionOptions{ *width, *height, SAV::Bitrate{*bitrate, SAV::Bitrate::KBPS()}, *filename },
				&appState, dlgHWND ) );
		}
	}

	void cancelVideo(HWND dlgHWND, ApplicationState& appState)
	{
		if (appState.conversionTask)
		{
			std::lock_guard guard(appState.vfc_mutex);
			if (appState.vfc)
			{
				appState.vfc->cancel();
			}
		}
		else
		{
			PostMessage(dlgHWND, WM_CONVERSION_FINISHED, 0, 0);
		}
	}

	void initVideoDlg(HWND dlgHWND, ApplicationState& appState)
	{
		auto menu = ::GetSystemMenu(dlgHWND, FALSE);
		if (menu)
		{
			EnableMenuItem(menu, SC_CLOSE, MF_BYCOMMAND | MF_GRAYED | MF_DISABLED);
		}

		SetDlgItemText(dlgHWND, IDC_W_EDIT, DEFAULT_VIDEO_WIDTH_TXT_VALUE.data());
		SetDlgItemText(dlgHWND, IDC_H_EDIT, DEFAULT_VIDEO_HEIGHT_TXT_VALUE.data());
		SetDlgItemText(dlgHWND, IDC_BITRATE_EDIT, DEFAULT_VIDEO_BITRATE_TXT_VALUE.data());

		std::array<wchar_t, MAX_PATH> buffer = { 0 };
		auto count = GetCurrentDirectory(static_cast<DWORD>(buffer.max_size()), buffer.data());
		if (count > 0)
		{
			std::wstring filepath{ buffer.data() };
			filepath += L'\\';
			filepath.append(DEFAULT_VIDEO_FILENAME_VALUE);
			SetDlgItemText(dlgHWND, IDC_FILE_NAME_EDIT, filepath.data());
		}
	}

	bool processSaveVideoDlgCommand(WORD commandID, HWND dlgBoxhwnd, ApplicationState& appState)
	{
		switch(commandID)
		{
			case ID_START:
				makeVideo(dlgBoxhwnd, appState);
				return true;

			case ID_CANCEL:
				cancelVideo(dlgBoxhwnd, appState);
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

	void finishVideoCreationDialog(HWND dlgHwnd, ApplicationState& appState)
	{
		if (appState.conversionTask)
		{
			INT_PTR result = appState.conversionTask->get() == S_OK ? 0 : 1;
			appState.conversionTask.reset();
			EndDialog(dlgHwnd, result);
			return;
		}

		EndDialog(dlgHwnd, 0);
	}

	INT_PTR CALLBACK SaveVideoDlgProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		static ApplicationState* appState = nullptr;

		switch (message)
		{
			case WM_INITDIALOG:
				appState = reinterpret_cast<ApplicationState*>(lParam);
				initVideoDlg(hwnd, *appState);
			return TRUE;

			case WM_COMMAND:
			{
				if (!processSaveVideoDlgCommand(LOWORD(wParam), hwnd, *appState))
				{
					return FALSE;
				}
			}
			return TRUE;

			case WM_CONVERSION_FINISHED:
				finishVideoCreationDialog(hwnd, *appState);
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

	void processResizeWindow(ApplicationState& appState)
	{
		auto dimension = getDimensions(*appState.layout, std::string(LAYOUT_IMAGE_CANVAS_NAME));
		if (dimension)
		{
			appState.appHandles.imageCanvas->onResize(*dimension);
		}

		dimension = getDimensions(*appState.layout, std::string(LAYOUT_TIME_LINE_NAME));
		if (dimension)
		{
			appState.appHandles.nfileList->onResize(*dimension);
		}

		dimension = getDimensions(*appState.layout, std::string(LAYOUT_PLAY_BUTTON_NAME));
		if (dimension)
		{
			::SetWindowPos(appState.appHandles.playButton, HWND_TOP, dimension->x, dimension->y,
				dimension->width,
				dimension->height,
				SWP_NOZORDER);
		}
	}

	bool createAppWindows(HINSTANCE hInstance, ApplicationState& appState)
	{
		auto dimension = getDimensions(*appState.layout, std::string(LAYOUT_IMAGE_CANVAS_NAME));
		if (dimension)
		{
			appState.appHandles.imageCanvas.emplace(appState.appHandles.appHandle, *dimension);
		}
		
		dimension = getDimensions(*appState.layout, std::string(LAYOUT_TIME_LINE_NAME));
		if (dimension)
		{
			appState.appHandles.nfileList.emplace(appState.appHandles.appHandle, *dimension);
		}
		
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

		dimension = getDimensions(*appState.layout, std::string(LAYOUT_PLAY_BUTTON_NAME));
		if (dimension)
		{
			appState.appHandles.playButton = ::CreateWindow(
				WC_BUTTON,
				L"play",
				WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
				static_cast<int>(dimension->x),
				static_cast<int>(dimension->y),
				static_cast<int>(dimension->width),
				static_cast<int>(dimension->height),
				appState.appHandles.appHandle,
				reinterpret_cast<HMENU>(IDC_PLAY),
				hInstance,
				NULL);
		}

		return true;
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

			case WM_SIZE:
			{
				auto width = LOWORD(lp);
				auto height = HIWORD(lp);
				appState->layout->resize(width, height);
				processResizeWindow(*appState);
				return 0;
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
	ULONG_PTR gdiplusToken;
	Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
	std::unique_ptr<ULONG_PTR, GdiPlusDeleter> gdiplusHandle(gdiplusToken, GdiPlusDeleter());

	INITCOMMONCONTROLSEX iccex;
	iccex.dwSize = sizeof(INITCOMMONCONTROLSEX);
	iccex.dwICC = ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
	::InitCommonControlsEx(&iccex);

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
	::AdjustWindowRect(&viewportRect, WS_OVERLAPPEDWINDOW, true);

	appState.layout.emplace(
		std::string(LAYOUT_ROOT_NAME),
		SAV::Layout::ItemDimensions{ 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT },
		SAV::Layout::ItemMargin{ 10, 10, 10, 10 })
		.addItem<SAV::Layout::HBoxLayout>("").second
		->addItem<SAV::Layout::BoxLayout>(std::string(LAYOUT_IMAGE_CANVAS_NAME), 1110).first
		->addItem<SAV::Layout::VBoxLayout>("", "*", SAV::Layout::ItemMargin{ 10, 0, 0, 0 }).second
		->addItem<SAV::Layout::BoxLayout>(std::string(LAYOUT_TIME_LINE_NAME), "90%").first
		->addItem<SAV::Layout::BoxLayout>("", "*", SAV::Layout::ItemMargin{ 0, 10, 0, 0 }).second
		->addItem<SAV::Layout::BoxLayout>(std::string(LAYOUT_PLAY_BUTTON_NAME));

	appState.appHandles.appHandle = ::CreateWindowEx(
		0,
		wndClsName,
		L"Simple.Animation.Viewer",
		WS_OVERLAPPEDWINDOW,
		10, 10,
		viewportRect.right - viewportRect.left, viewportRect.bottom - viewportRect.top,
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