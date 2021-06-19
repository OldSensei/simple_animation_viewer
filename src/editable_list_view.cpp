#include <Windows.h>
#include <CommCtrl.h>

#include <gdiplus.h>
#include <gdiplusheaders.h>

#include <array>
#include <exception>
#include <sstream>

#include "resource.h"
#include "editable_list_view.hpp"

namespace
{
	LRESULT CALLBACK listViewSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
	{
		switch (message)
		{
			case WM_CONTEXTMENU:
				{
					auto* editableListView = (SAV::EditableListView*)dwRefData;
					return editableListView->processContextMenu(lParam);
				}

			case WM_KEYUP:
				if (wParam == VK_UP || wParam == VK_DOWN)
				{
					auto result = ::DefSubclassProc(hwnd, message, wParam, lParam);
					auto* editableListView = (SAV::EditableListView*)dwRefData;
					editableListView->processSelectionChanged();
					return result;
				}

				if (wParam == VK_DELETE)
				{
					auto* editableListView = (SAV::EditableListView*)dwRefData;
					editableListView->removeItem();
					return 0;
				}
		}
		return ::DefSubclassProc(hwnd, message, wParam, lParam);
	}

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
					std::array<wchar_t, 256> szText = { 0 };
					LVITEM lvItem;
					lvItem.iItem = PtrToInt(GetProp(hwnd, L"ITEM"));
					lvItem.iSubItem = 1;

					GetWindowText(hwnd, szText.data(), static_cast<int>(szText.size()));
					lvItem.pszText = szText.data();

					if (auto parentWindow = ::GetAncestor(hwnd, GA_PARENT); parentWindow != nullptr)
					{
						SendMessage(parentWindow, LVM_SETITEMTEXT, (WPARAM)lvItem.iItem, (LPARAM)&lvItem);
					}
					DestroyWindow(hwnd);
				}
				return 0;

				case VK_DOWN:
				case VK_UP:
					if (auto parentWindow = ::GetAncestor(hwnd, GA_PARENT); parentWindow != nullptr)
					{
						PostMessage(parentWindow, WM_KEYDOWN, (WPARAM)wParam, 0);
					}
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
			::RemoveWindowSubclass(hwnd, inPlaceEditSubclassProc, uIdSubclass);
			return ::DefSubclassProc(hwnd, message, wParam, lParam);
		}

		return ::DefSubclassProc(hwnd, message, wParam, lParam);
	}

}

SAV::EditableListView::EditableListView(HWND parent, const RECT& position) :
	m_handle(nullptr),
	m_height(position.bottom - position.top),
	m_width(position.right - position.left),
	m_widthPercent(static_cast<float>(m_width) / 100.0f),
	m_onSelectHandler(nullptr),
	m_dragAndDropContext{ std::nullopt }
{
	m_handle = ::CreateWindow(
		WC_LISTVIEW,
		L"",
		WS_CHILD | LVS_REPORT | WS_VISIBLE | LVS_SINGLESEL,
		position.left, position.top,
		m_width, m_height,
		parent,
		NULL,
		::GetModuleHandle(nullptr),
		NULL);

	if (m_handle == nullptr)
	{
		throw std::exception("Can't create list view");
	}

	ListView_SetExtendedListViewStyle(m_handle, LVS_EX_FULLROWSELECT);

	SetWindowSubclass(m_handle, listViewSubclassProc, 1, (DWORD_PTR)this);
}

void SAV::EditableListView::addHeader(const HeaderDescription& description, int index)
{
	std::array<wchar_t, 256> szText = {0};
	wcscpy_s(szText.data(), 256, description.caption.c_str());

	LVCOLUMN lvc;
	lvc.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
	lvc.pszText = szText.data();
	lvc.fmt = LVCFMT_LEFT;

	lvc.iSubItem = index;
	lvc.cx = static_cast<int>(m_widthPercent * description.headerWidthPercent);
	
	if (ListView_InsertColumn(m_handle, index, &lvc) == -1)
	{
		throw std::exception("Can't initialize header");
	}
}

void SAV::EditableListView::showInplaceEditControl(int itemIndex, int subItemIndex)
{
	::RECT timerSubItemRect;
	ListView_GetSubItemRect(m_handle, itemIndex, subItemIndex, LVIR_LABEL, &timerSubItemRect);
	auto inPlaceEditControl = CreateWindow(
		L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_LEFT, timerSubItemRect.left, timerSubItemRect.top,
		timerSubItemRect.right - timerSubItemRect.left, timerSubItemRect.bottom - timerSubItemRect.top,
		m_handle, NULL, GetModuleHandle(0), 0);

	std::array<wchar_t, 256> text = { 0 };
	ListView_GetItemText(m_handle, itemIndex, subItemIndex, text.data(), static_cast<int>(text.size()));

	SendMessage(inPlaceEditControl, EM_LIMITTEXT, 8, 0);
	SendMessage(inPlaceEditControl, WM_SETTEXT, 0, (LPARAM)text.data());
	SendMessage(inPlaceEditControl, EM_SETSEL, 0, 8);
	SetFocus(inPlaceEditControl);

	SetWindowSubclass(inPlaceEditControl, inPlaceEditSubclassProc, 0, (DWORD_PTR)this);
	SetProp(inPlaceEditControl, L"ITEM", (HANDLE)itemIndex);
}

void SAV::EditableListView::updateData(const std::vector<std::vector<std::wstring>>& data)
{
	for(int rowIndex = 0; rowIndex < data.size(); ++rowIndex)
	{
		insertItem(data[rowIndex], rowIndex);
	}
}

std::tuple<bool, int> SAV::EditableListView::processNotify(WPARAM wp, LPARAM lp)
{
	int returnedCode = 0;
	auto* nmhdr = (LPNMHDR)lp;
	if (nmhdr->hwndFrom != m_handle)
	{
		return { false, returnedCode };
	}

	auto code = nmhdr->code;
	switch (code)
	{
		case LVN_ITEMACTIVATE:
		{
			auto* itemActivate = (LPNMITEMACTIVATE)nmhdr;
			showInplaceEditControl(itemActivate->iItem, 1);
			return { true, returnedCode };
		}

		case NM_CLICK:
		{
			processSelectionChanged();
			return {true, returnedCode };
		}

		case LVN_BEGINDRAG:
		{
			processBeginDragAndDrop(lp);
			return {true, returnedCode };
		}

		case NM_CUSTOMDRAW:
		{
			auto listViewCustomDraw = reinterpret_cast<LPNMLVCUSTOMDRAW>(lp);
			returnedCode = processCustomDraw(listViewCustomDraw);
			return { true, returnedCode };
		}
	}

	return {false, returnedCode};
}

std::vector<std::wstring> SAV::EditableListView::getRowData(int index) const
{
	std::vector<std::wstring> data;

	if (index >= 0)
	{
		std::array<wchar_t, 256> buffer = { 0 };
		int subItemIndex = 0;
		LVITEM item;
		item.iSubItem = subItemIndex;
		item.pszText = buffer.data();
		item.cchTextMax = buffer.size();
		int count = ::SendMessage(m_handle, LVM_GETITEMTEXT, static_cast<WPARAM>(index), (LPARAM)&item);
		while (count > 0)
		{
			data.emplace_back(buffer.data(), buffer.data() + count);
			item.iSubItem = ++subItemIndex;
			count = ::SendMessage(m_handle, LVM_GETITEMTEXT, static_cast<WPARAM>(index), (LPARAM)&item);
		}
	}
	return data;
}

void SAV::EditableListView::insertItem(const std::vector<std::wstring>& itemData, int index)
{
	LVITEM lvI;
	lvI.mask = LVIF_TEXT | LVIF_STATE;
	lvI.stateMask = 0;
	lvI.state = 0;
	lvI.cchTextMax = 256;

	std::array<wchar_t, 256> buffer = { 0 };
	std::copy(itemData[0].begin(), itemData[0].end(), buffer.begin());
	buffer[itemData[0].length()] = 0;

	lvI.iItem = index;
	lvI.iSubItem = 0;
	lvI.pszText = buffer.data();
	ListView_InsertItem(m_handle, &lvI);
	for (int columnIndex = 1; columnIndex < itemData.size(); ++columnIndex)
	{
		std::copy(itemData[columnIndex].begin(), itemData[columnIndex].end(), buffer.begin());
		buffer[itemData[columnIndex].length()] = 0;
		lvI.iSubItem = columnIndex;
		lvI.pszText = buffer.data();
		SendMessage(m_handle, LVM_SETITEM, 0, (LPARAM)(&lvI));
	}
}

SAV::EditableListView::DragAndDrop SAV::EditableListView::createDragAndDropContext(int index)
{
	::RECT itemRect;
	ListView_GetSubItemRect(m_handle, index, 0, LVIR_LABEL, &itemRect);

	const auto width = itemRect.right - itemRect.left;
	const auto height = itemRect.bottom - itemRect.top;

	auto hdc = ::GetDC(m_handle);
	auto hdcMem = ::CreateCompatibleDC(hdc);
	auto bitmap = ::CreateCompatibleBitmap(hdc, width, height);
	auto oldBitmap = SelectObject(hdcMem, bitmap);

	RECT r{ 0, 0, width, height };
	::FillRect(hdcMem, &r, static_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));

	auto data = getRowData(index);
	COLORREF rgbWhite = 0x00FFFFFF;
	COLORREF oldColor = ::SetTextColor(hdcMem, rgbWhite);
	int oldBkMode = ::SetBkMode(hdcMem, TRANSPARENT);
	::DrawText(hdcMem, data[0].c_str(), -1, &r,  DT_LEFT | DT_VCENTER);
	::SetTextColor(hdcMem, oldColor);
	::SetBkMode(hdcMem, oldBkMode);

	auto imageList = ImageList_Create(width, height, ILC_COLOR32, 0, 0);

	SelectObject(hdcMem, oldBitmap);
	DeleteDC(hdcMem);

	int bitmapIndex = ImageList_Add(imageList, bitmap, nullptr);
	if (bitmapIndex == -1)
	{
		DeleteObject(bitmap);
		ImageList_Destroy(imageList);
		return std::nullopt;
	}

	DeleteObject(bitmap);
	return DragAndDrop{ std::in_place, (HANDLE)imageList, width, height };
}

int SAV::EditableListView::processContextMenu(LPARAM lParam)
{
	POINT p{LOWORD(lParam), HIWORD(lParam)};

	HMENU contextMenu = LoadMenu(NULL, MAKEINTRESOURCE(IDR_ANIMATIONS_MENU));
	auto menuTrackPopup = GetSubMenu(contextMenu, 0);

	int index = ListView_GetNextItem(m_handle, -1, LVNI_SELECTED);
	if (index < 0)
	{
		EnableMenuItem(menuTrackPopup, ID_DELETE_ITEM, MF_GRAYED);
		EnableMenuItem(menuTrackPopup, ID_COPY_ITEM, MF_GRAYED);
		EnableMenuItem(menuTrackPopup, ID_SETTOALL, MF_GRAYED);
	}

	int id = TrackPopupMenuEx(menuTrackPopup, TPM_RIGHTBUTTON | TPM_RETURNCMD, p.x, p.y, m_handle, nullptr);
	switch (id)
	{
		case ID_DELETE_ITEM:
			removeItem(index);
			break;

		case ID_COPY_ITEM:
			copyItem(index);
			break;

		case ID_SETTOALL:
			setValueToAllItem(index);
			break;
	}

	DestroyMenu(contextMenu);
	return 0;
}

void SAV::EditableListView::processSelectionChanged()
{
	if (m_onSelectHandler)
	{
		int index = ListView_GetNextItem(m_handle, -1, LVNI_SELECTED);
		auto data = getRowData(index);
		m_onSelectHandler.operator()(data);
	}
}

void SAV::EditableListView::processBeginDragAndDrop(LPARAM lParam)
{
	int index = ListView_GetNextItem(m_handle, -1, LVNI_SELECTED);
	if (index < 0)
	{
		m_dragAndDropContext = std::nullopt;
		return;
	}

	auto pt = reinterpret_cast<NM_LISTVIEW*>(lParam)->ptAction;
	ClientToScreen(m_handle, &pt);

	m_dragAndDropContext = createDragAndDropContext(index);
	auto hwnd = ::GetAncestor(m_handle, GA_PARENT);
	::SetCapture(hwnd);
}

bool SAV::EditableListView::processMouseMoving(LPARAM lParam)
{
	if (!m_dragAndDropContext)
	{
		return false;
	}

	::POINT p{ 0, 0 };
	p.x = LOWORD(lParam);
	p.y = HIWORD(lParam);

	auto hwnd = ::GetAncestor(m_handle, GA_PARENT);
	ClientToScreen(hwnd, &p);

	// get rect for item which mouse covered
	LVHITTESTINFO lvhti = { 0 };
	lvhti.pt.x = p.x;
	lvhti.pt.y = p.y;
	ScreenToClient(m_handle, &lvhti.pt);
	ListView_HitTest(m_handle, &lvhti);
	m_dragAndDropContext->mouse = lvhti.pt;

	if (lvhti.iItem != -1)
	{
		m_dragAndDropContext->hotItemIndex = lvhti.iItem;
		InvalidateRect(m_handle, nullptr, false);
	}

	return true;
}

void SAV::EditableListView::processEndDragAndDrop(LPARAM lParam)
{
	if (!m_dragAndDropContext)
	{
		return;
	}

	m_dragAndDropContext.reset();

	ReleaseCapture();

	LVHITTESTINFO lvhti = { 0 };
	lvhti.pt.x = LOWORD(lParam);
	lvhti.pt.y = HIWORD(lParam);

	auto hwnd = ::GetAncestor(m_handle, GA_PARENT);

	ClientToScreen(hwnd, &lvhti.pt);
	ScreenToClient(m_handle, &lvhti.pt);

	ListView_HitTest(m_handle, &lvhti);
	if (lvhti.iItem == -1)
	{
		return;
	}

	int index = ListView_GetNextItem(m_handle, -1, LVNI_SELECTED);

	if (index != -1 && index == lvhti.iItem)
	{
		return;
	}

	auto draggedData = getRowData(index);
	removeItem(index);
	insertItem(draggedData, lvhti.iItem);

	InvalidateRect(m_handle, nullptr, false);
}

void SAV::EditableListView::removeItem(const std::optional<int>& itemIndex)
{
	int index = itemIndex.has_value() ? *itemIndex : ListView_GetNextItem(m_handle, -1, LVNI_SELECTED);
	if (index != -1)
	{
		::SendMessage(m_handle, LVM_DELETEITEM, static_cast<WPARAM>(index), 0);
	}
}

void SAV::EditableListView::copyItem(const std::optional<int>& itemIndex )
{
	int index = itemIndex.has_value() ? *itemIndex : ListView_GetNextItem(m_handle, -1, LVNI_SELECTED);
	if (index >= 0)
	{
		insertItem(getRowData(index), index);
	}
}

void SAV::EditableListView::setValueToAllItem(const std::optional<int>& itemIndex)
{
	int index = itemIndex.has_value() ? *itemIndex : ListView_GetNextItem(m_handle, -1, LVNI_SELECTED);
	if (index >= 0)
	{
		auto data = getRowData(index);
		auto& value = data[1];
		auto itemCount = ListView_GetItemCount(m_handle);
		for (int i = 0; i < itemCount; ++i)
		{
			if (i != index)
			{
				LVITEM lvItem;
				lvItem.iItem = i;
				lvItem.iSubItem = 1;
				lvItem.pszText = value.data();
				SendMessage(m_handle, LVM_SETITEMTEXT, (WPARAM)lvItem.iItem, (LPARAM)&lvItem);
			}
		}
	}
}

std::vector<std::vector<std::wstring>> SAV::EditableListView::getListViewData() const
{
	std::vector<std::vector<std::wstring>> result;
	int totalItemsCount = ListView_GetItemCount(m_handle);
	for (int index = 0; index < totalItemsCount; ++index)
	{
		result.push_back(getRowData(index));
	}

	return result;
}

int SAV::EditableListView::processCustomDraw(LPNMLVCUSTOMDRAW listViewCustomDraw)
{
	switch (listViewCustomDraw->nmcd.dwDrawStage)
	{
		case CDDS_PREPAINT:
			return m_dragAndDropContext ? (CDRF_NOTIFYPOSTPAINT | CDRF_NOTIFYITEMDRAW) : CDRF_DODEFAULT;

		case CDDS_ITEMPREPAINT:
			return processPrePaint(listViewCustomDraw);

		case CDDS_POSTPAINT:
			return processPostPaint();
	}
}

int SAV::EditableListView::processPrePaint(LPNMLVCUSTOMDRAW listViewCustomDraw)
{
	if (m_dragAndDropContext->hotItemIndex != -1 && listViewCustomDraw->nmcd.dwItemSpec == m_dragAndDropContext->hotItemIndex)
	{
		listViewCustomDraw->clrTextBk = RGB(255, 0, 0);
		return CDRF_NEWFONT;
	}

	return CDRF_DODEFAULT;
}

int SAV::EditableListView::processPostPaint()
{
	if (m_dragAndDropContext)
	{
		ImageList_Draw(m_dragAndDropContext->getImageList(), 0,
						GetDC(m_handle),
						m_dragAndDropContext->mouse.x, m_dragAndDropContext->mouse.y,
						ILD_BLEND);
	}
	
	return CDRF_DODEFAULT;
}