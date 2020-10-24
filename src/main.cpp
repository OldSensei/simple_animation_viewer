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
#include <string>
#include <string_view>

#pragma comment (lib,"Gdiplus.lib")
#pragma comment (lib,"Comctl32.lib")

namespace
{
	constexpr int WINDOW_WIDTH = 1480;
	constexpr int WINDOW_HEIGHT = 768;

	constexpr std::uint64_t IDC_LOAD = 0x1;
	constexpr std::uint64_t IDC_TIMER_EDIT = 0x2;
	constexpr std::uint64_t IDC_LOOP_BOX = 0x3;
	constexpr std::uint64_t IDC_INVERSE_END_BOX = 0x4;
	constexpr std::uint64_t IDC_PLAY = 0x5;
	constexpr std::uint64_t IDC_STOP = 0x6;


	class TimeLine
	{
	public:
		TimeLine() :
			m_hwnd(nullptr)
		{};

		TimeLine(HWND window) :
			m_hwnd(window)
		{};
		~TimeLine();

		void add(std::uint32_t frameIndex, std::chrono::milliseconds interval);
		void setLooped(bool value) { m_isLooped = value; }
		void setWindow(HWND window) { m_hwnd = window; };
		void addInvertFrames();
		void reset();
		void play(bool isLooped, bool isReverInEnd);
		bool advance();
		bool isFired(UINT_PTR timerID) const { return timerID == m_animationTimer; }
		std::uint32_t getFrame() { return m_frameIndex; }

	private:
		HWND m_hwnd;
		std::uint32_t m_current = 0;
		std::uint32_t m_frameIndex = 0;
		UINT_PTR m_animationTimer = 0;
		bool m_isLooped = false;
		std::vector<std::pair<std::uint32_t, std::chrono::milliseconds>> m_frames;
	};

	TimeLine::~TimeLine()
	{
		if (m_animationTimer != 0)
		{
			::KillTimer(m_hwnd, m_animationTimer);
			m_animationTimer = 0;
		}
		
	}

	void TimeLine::add(std::uint32_t frameIndex, std::chrono::milliseconds interval)
	{
		m_frames.emplace_back(frameIndex, interval);
	}

	void TimeLine::addInvertFrames()
	{
		std::vector<std::pair<std::uint32_t, std::chrono::milliseconds>> newFrames;
		newFrames.reserve(m_frames.size() * 2);
		std::copy(m_frames.begin(), m_frames.end(), std::back_inserter(newFrames));
		std::copy(m_frames.rbegin(), m_frames.rend(), std::back_inserter(newFrames));
		m_frames = std::move(newFrames);
	}

	void TimeLine::reset()
	{
		if (m_animationTimer != 0)
		{
			::KillTimer(m_hwnd, m_animationTimer);
			m_animationTimer = 0;
		}
		
		m_current = 0;
		m_isLooped = false;
		m_frames.clear();
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

		auto& [frameIndex, timerCount] = m_frames.at(m_current++);
		m_frameIndex = frameIndex;

		if (m_animationTimer == 0)
		{
			m_animationTimer = 0x1;
		}
		m_animationTimer = SetTimer(m_hwnd, m_animationTimer, timerCount.count(), nullptr);
		return m_animationTimer != 0;
	}

	using ListViewString = std::pair<std::array<wchar_t, 256>, std::array<wchar_t, 10>>;

	std::vector<ListViewString> gStringViewData;

	struct ApplicationData
	{
		std::vector<std::wstring> filePaths;
		std::vector<std::unique_ptr<Gdiplus::Image>> images;
		TimeLine tl;
	} gAppData = {};

	struct ApplicationState
	{
		bool isExit = false;
		struct
		{
			HWND appHandle = nullptr;
			HWND canvas = nullptr;
			HWND loadButton = nullptr;
			HWND fileList = nullptr;
			HWND playButton = nullptr;
			HWND stopButton = nullptr;
			HWND loopBox = nullptr;
			HWND invEndBox = nullptr;
			HWND timerEdit = nullptr;
		} appHandles;

		::RECT windowRect;

	} gAppState = {};

	LRESULT CALLBACK inPlaceEditSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
	{
		switch (message)
		{
			case WM_KILLFOCUS:
				DestroyWindow(hwnd);
				return 0;

			case WM_KEYDOWN:
				switch (wParam)
				{
					case VK_RETURN:
					{
						wchar_t buf[8] = { 0 };
						LVITEM lvItem;
						lvItem.iItem = (int)GetProp(hwnd, L"ITEM");
						lvItem.iSubItem = 1;

						GetWindowText(hwnd, buf, 8);

						lvItem.pszText = buf;
						SendMessage(gAppState.appHandles.fileList, LVM_SETITEMTEXT, (WPARAM)lvItem.iItem, (LPARAM)&lvItem);
						DestroyWindow(hwnd);
					}
					return 0;

					case VK_DOWN:
					case VK_UP:
						PostMessage(gAppState.appHandles.fileList, WM_KEYDOWN, (WPARAM)wParam, 0 );
						DestroyWindow(hwnd);
						return 0;

					case VK_ESCAPE:
						DestroyWindow(hwnd);
						return 0;

					default:
						return ::DefSubclassProc(hwnd, message, wParam, lParam);
				}
				break;

			case WM_NCDESTROY:
				gAppState.appHandles.timerEdit = nullptr;
				::RemoveWindowSubclass(hwnd, inPlaceEditSubclassProc, uIdSubclass);
				return ::DefSubclassProc(hwnd, message, wParam, lParam);
		}

		return ::DefSubclassProc(hwnd, message, wParam, lParam);
	}

	bool loadData(std::wstring_view path, ApplicationData& data)
	{
		std::transform(std::filesystem::directory_iterator(std::filesystem::path(path)), std::filesystem::directory_iterator(), std::back_inserter(data.filePaths),
			[](const auto& file) { return file.path().filename().wstring(); });

		std::transform(std::filesystem::directory_iterator(std::filesystem::path(path)), std::filesystem::directory_iterator(), std::back_inserter(data.images),
			[](const auto& file) 
			{ 
				return std::make_unique<Gdiplus::Image>(file.path().wstring().c_str());
			});

		return true;
	}

	bool updateFileListView(const ApplicationData& data, const ApplicationState& app)
	{
		gStringViewData.clear();
		std::wstring time{L"0"};

		LVITEM lvI;
		lvI.mask = LVIF_TEXT | LVIF_STATE;
		lvI.stateMask = 0;
		lvI.state = 0;
		lvI.cchTextMax = 256;

		for (int index = 0; index < data.filePaths.size(); index++)
		{
			ListViewString item = { {0}, {0} };
			std::copy(data.filePaths[index].begin(), data.filePaths[index].end(), item.first.begin());
			std::copy(time.begin(), time.end(), item.second.begin());
			gStringViewData.push_back(item);
			lvI.iItem = index;
			lvI.iSubItem = 0;
			lvI.pszText = gStringViewData.back().first.data();

			if (ListView_InsertItem(app.appHandles.fileList, &lvI) == -1)
				return false;

			lvI.iSubItem = 1;
			lvI.pszText = gStringViewData.back().second.data();
			SendMessage(app.appHandles.fileList, LVM_SETITEM, 0, (LPARAM)(&lvI));
		}

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

		return result;;
	}

	bool processPlayButton(ApplicationData& appData, ApplicationState& appState)
	{
		appData.tl.reset();
		int totalItemsCount = ListView_GetItemCount(gAppState.appHandles.fileList);
		for (int index = 0; index < totalItemsCount; ++index)
		{
			wchar_t pszText[10] = { 0 };
			ListView_GetItemText(gAppState.appHandles.fileList,
				index,
				1,
				pszText,
				10
			);

			if (pszText)
			{
				char buf[10] = { 0 };
				int len = WideCharToMultiByte(CP_UTF8, 0, pszText, lstrlenW(pszText), buf, 10, nullptr, nullptr);

				std::uint64_t value = 0;
				auto result = std::from_chars(buf, buf + len, value);
				if (result.ec == std::errc())
				{
					appData.tl.add(static_cast<std::uint32_t>(index), std::chrono::milliseconds(value));
				}
			}
		}
		bool isLooped = SendMessage(appState.appHandles.loopBox, BM_GETCHECK, 0, 0) == BST_CHECKED;
		bool isInvert = SendMessage(appState.appHandles.invEndBox, BM_GETCHECK, 0, 0) == BST_CHECKED;

		appData.tl.play(isLooped, isInvert);

		return true;
	}

	bool processStopButton(ApplicationData& appData)
	{
		appData.tl.reset();
		return true;
	}

	bool processChild(WPARAM wp, ApplicationState& appState)
	{
		if (LOWORD(wp) == IDC_LOAD)
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
			processStopButton(gAppData);
			return true;
		}

		return false;
	}

	void showInplaceEditControl(HWND control, ApplicationState& appState, int itemIndex)
	{
		::RECT timerSubItemRect;
		ListView_GetSubItemRect(control, itemIndex, 1, LVIR_LABEL, &timerSubItemRect);
		if (appState.appHandles.timerEdit == nullptr)
		{
			appState.appHandles.timerEdit = CreateWindow(
				L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT, timerSubItemRect.left, timerSubItemRect.top,
				timerSubItemRect.right - timerSubItemRect.left, timerSubItemRect.bottom - timerSubItemRect.top,
				control, NULL, GetModuleHandle(0), 0);

			wchar_t buf[8];
			ListView_GetItemText(control, itemIndex, 1, buf, 8);

			SendMessage(appState.appHandles.timerEdit, EM_LIMITTEXT, 8, 0);
			SendMessage(appState.appHandles.timerEdit, WM_SETTEXT, 0, (LPARAM)buf);
			SendMessage(appState.appHandles.timerEdit, EM_SETSEL, 0, 8);
			SetFocus(appState.appHandles.timerEdit);

			SetWindowSubclass(appState.appHandles.timerEdit, inPlaceEditSubclassProc, 0, 0);
			SetProp(appState.appHandles.timerEdit, L"ITEM", (HANDLE)itemIndex);
		}
	}

	bool processFileListView(LPNMHDR nmhdr, ApplicationState& appState, ApplicationData& data)
	{
		auto code = nmhdr->code;
		if (code == NM_CLICK)
		{
			auto* itemActivate = (LPNMITEMACTIVATE)nmhdr;
			int index = itemActivate->iItem;
			if (index != -1)
			{
				auto& imgPtr = data.images[index];
				Gdiplus::Graphics graphics(appState.appHandles.canvas);
				graphics.DrawImage(imgPtr.get(), 0, 0);
				return true;
			}
		}

		if (code == NM_DBLCLK)
		{
			auto* itemActivate = (LPNMITEMACTIVATE)nmhdr;
			showInplaceEditControl(itemActivate->hdr.hwndFrom, appState, itemActivate->iItem);
			return true;
		}

		if (code == NM_RETURN)
		{
			auto index = ListView_GetNextItem(nmhdr->hwndFrom, -1, LVNI_SELECTED);
			if (index != -1)
			{
				showInplaceEditControl(nmhdr->hwndFrom, appState, index);
			}
			return true;
		}
		return false;
	}

	bool processChildNotification(ApplicationState& appState, ApplicationData& data, WPARAM wp, LPARAM lp)
	{
		auto* nmhdr = (LPNMHDR)lp;
		if (nmhdr->hwndFrom == appState.appHandles.fileList)
		{
			return processFileListView(nmhdr, appState, data);
		}

		return false;
	}

	bool processTimer(ApplicationState& appState, ApplicationData& data, WPARAM wp)
	{
		if (data.tl.isFired(wp))
		{
			data.tl.advance();
			auto frameIndex = data.tl.getFrame();

			auto& imgPtr = data.images[frameIndex];
			Gdiplus::Graphics graphics(appState.appHandles.canvas);
			graphics.DrawImage(imgPtr.get(), 0, 0);
		}
		return true;
	}

	LRESULT CALLBACK mainWindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
	{
		switch (msg)
		{
			case WM_NOTIFY:
				if (processChildNotification(gAppState, gAppData, wp, lp))
					return 1;
				else
					return DefWindowProc(hwnd, msg, wp, lp);

			case WM_COMMAND:
				processChild(wp, gAppState);
				return 1;

			case WM_TIMER:
				processTimer(gAppState, gAppData, wp);
				return 1;

			case WM_DESTROY:
				PostQuitMessage(0);
				gAppState.isExit = true;
				return 1;

			default:
				return DefWindowProc(hwnd, msg, wp, lp);
		}
	}

	HWND createCanvasWindow(HINSTANCE hInstance, ApplicationState app)
	{
		constexpr const  wchar_t* wndCanvasClsName = L"Simple.Animation.Viewer.Canvas";

		WNDCLASSEX wndclass;
		ZeroMemory(&wndclass, sizeof(WNDCLASSEX));

		wndclass.cbSize = sizeof(WNDCLASSEX);
		wndclass.style = CS_HREDRAW | CS_VREDRAW;
		wndclass.lpfnWndProc = DefWindowProc;
		wndclass.lpszClassName = wndCanvasClsName;
		wndclass.hInstance = hInstance;
		wndclass.hbrBackground = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
		wndclass.hCursor = static_cast<HCURSOR>(::LoadCursor(0, IDC_ARROW));
		wndclass.hIcon = 0;
		wndclass.hIconSm = 0;
		wndclass.lpszMenuName = nullptr;
		wndclass.cbClsExtra = 0;
		wndclass.cbWndExtra = 0;

		::RegisterClassEx(&wndclass);

		return ::CreateWindowEx(
			0,
			wndCanvasClsName,
			nullptr,
			WS_CHILD | WS_VISIBLE,
			10, 10,
			1100, 700,
			app.appHandles.appHandle,
			nullptr,
			hInstance,
			nullptr
		);
	}

	bool initListView(HWND listView, int listWidth)
	{
		float percentWidth = static_cast<float>(listWidth) / 100.0f;

		WCHAR szText[256] = L"Pictures";

		LVCOLUMN lvc;
		lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
		lvc.pszText = szText;
		lvc.fmt = LVCFMT_LEFT;

		lvc.iSubItem = 0;
		lvc.cx = static_cast<int>(percentWidth * 70.f);
		wcscpy_s(szText, 256, L"Pictures");
		if (ListView_InsertColumn(listView, 0, &lvc) == -1)
			return false;

		lvc.iSubItem = 1;
		lvc.cx = static_cast<int>(percentWidth * 30.f);
		wcscpy_s(szText, 256, L"Time");
		if (ListView_InsertColumn(listView, 1, &lvc) == -1)
			return false;

		return true;
	}

	bool createAppWindows(HINSTANCE hInstance, ApplicationState& appState)
	{
		appState.appHandles.canvas = createCanvasWindow(hInstance, appState);

		if (!appState.appHandles.canvas)
		{
			return false;
		}

		int buttonWidth = 350;
		int buttonHeight = 20;

		int left = appState.windowRect.right - buttonWidth - 10;
		int top = 10;

		appState.appHandles.loadButton = ::CreateWindow(
			WC_BUTTON,
			L"load", 
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 
			left, top, 
			buttonWidth, buttonHeight,
			appState.appHandles.appHandle,
			reinterpret_cast<HMENU>(IDC_LOAD),
			hInstance,
			NULL);
		
		int listTop = top + buttonHeight + 10;
		int listHeight = 300;
		appState.appHandles.fileList = ::CreateWindow(
			WC_LISTVIEW,
			L"",
			WS_CHILD | LVS_REPORT | WS_VISIBLE | LVS_SINGLESEL,
			left, listTop,
			buttonWidth, listHeight,
			appState.appHandles.appHandle,
			NULL,
			hInstance,
			NULL);
		ListView_SetExtendedListViewStyle(appState.appHandles.fileList, LVS_EX_FULLROWSELECT);
		initListView(appState.appHandles.fileList, buttonWidth);

		int boxesTop = listTop + listHeight + 10;
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
		gAppData.filePaths.clear();
		gAppData.images.clear();
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
	wndclass.lpszMenuName = nullptr;
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
	
	gAppData.tl.setWindow(gAppState.appHandles.appHandle);

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