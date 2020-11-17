#include "image_cachable_canvas.hpp"

namespace
{
	constexpr const  wchar_t* wndCanvasClsName = L"Simple.Animation.Viewer.Canvas";
}

namespace SAV
{
	ImageCachableCanvas::ImageCachableCanvas(HWND parent, const RECT& position) :
		m_handle{ nullptr },
		m_graphics{ std::nullopt },
		m_width{position.right - position.left},
		m_height{position.bottom - position.top}
	{
		WNDCLASSEX wndclass;
		ZeroMemory(&wndclass, sizeof(WNDCLASSEX));

		wndclass.cbSize = sizeof(WNDCLASSEX);
		wndclass.style = CS_HREDRAW | CS_VREDRAW;
		wndclass.lpfnWndProc = DefWindowProc;
		wndclass.lpszClassName = wndCanvasClsName;
		wndclass.hInstance = ::GetModuleHandle(nullptr);
		wndclass.hbrBackground = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
		wndclass.hCursor = static_cast<HCURSOR>(::LoadCursor(0, IDC_ARROW));
		wndclass.hIcon = 0;
		wndclass.hIconSm = 0;
		wndclass.lpszMenuName = nullptr;
		wndclass.cbClsExtra = 0;
		wndclass.cbWndExtra = 0;

		::RegisterClassEx(&wndclass);

		m_handle = ::CreateWindowEx(
			0,
			wndCanvasClsName,
			nullptr,
			WS_CHILD | WS_VISIBLE,
			position.left, position.top,
			m_width, m_height,
			parent,
			nullptr,
			::GetModuleHandle(nullptr),
			nullptr
		);

		m_graphics.emplace(m_handle);
	}

	ImageCachableCanvas::~ImageCachableCanvas() noexcept
	{
		::DestroyWindow(m_handle);
		m_handle = nullptr;
		::UnregisterClass(wndCanvasClsName, ::GetModuleHandle(nullptr));
	}

	Gdiplus::Bitmap* ImageCachableCanvas::getImage(const std::filesystem::path& imagePath)
	{
		if (auto it = m_cache.find(imagePath.wstring()); it != m_cache.end())
		{
			return it->second.get();
		}

		auto [it, result] = m_cache.insert(std::pair(imagePath.wstring(), std::make_unique<Gdiplus::Bitmap>(imagePath.wstring().c_str())));
		return it->second.get();
	}

	void ImageCachableCanvas::drawImage(const std::filesystem::path& imagePath)
	{
		auto* image = getImage(imagePath);
		m_graphics->DrawImage(image, 0, 0, m_width, m_height);
	}

}