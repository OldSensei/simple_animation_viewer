#include <Windows.h>
#include <CommCtrl.h>

#include <array>
#include <exception>

#include "editable_list_view.hpp"
namespace
{
	LRESULT CALLBACK listViewSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
	{
		switch (message)
		{
			case WM_KEYUP:
				if (wParam == VK_UP || wParam == VK_DOWN)
				{
					auto result = ::DefSubclassProc(hwnd, message, wParam, lParam);
					auto* editableListView = (SAV::EditableListView*)dwRefData;
					editableListView->processSelectionChanged();
					return result;
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
	m_onSelectHandler(nullptr)
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
	LVITEM lvI;
	lvI.mask = LVIF_TEXT | LVIF_STATE;
	lvI.stateMask = 0;
	lvI.state = 0;
	lvI.cchTextMax = 256;
	std::array<wchar_t, 256> buffer = { 0 };
	for(int rowIndex = 0; rowIndex < data.size(); ++rowIndex)
	{
		std::copy(data[rowIndex][0].begin(), data[rowIndex][0].end(), buffer.begin());
		buffer[data[rowIndex][0].length()] = 0;

		lvI.iItem = rowIndex;
		lvI.iSubItem = 0;
		lvI.pszText = buffer.data();
		ListView_InsertItem(m_handle, &lvI);
		for(int columnIndex = 1; columnIndex < data[rowIndex].size(); ++columnIndex)
		{
			std::copy(data[rowIndex][columnIndex].begin(), data[rowIndex][columnIndex].end(), buffer.begin());
			buffer[data[rowIndex][columnIndex].length()] = 0;
			lvI.iSubItem = columnIndex;
			lvI.pszText = buffer.data();
			SendMessage(m_handle, LVM_SETITEM, 0, (LPARAM)(&lvI));
		}
	}
}

bool SAV::EditableListView::processNotify(WPARAM wp, LPARAM lp)
{
	auto* nmhdr = (LPNMHDR)lp;
	if (nmhdr->hwndFrom != m_handle)
	{
		return false;
	}
	auto code = nmhdr->code;

	switch (code)
	{
		case LVN_ITEMACTIVATE:
		{
			auto* itemActivate = (LPNMITEMACTIVATE)nmhdr;
			showInplaceEditControl(itemActivate->iItem, 1);
			return true;
		}

		case NM_CLICK:
		{
			processSelectionChanged();
			return true;
		}
	}

	return false;
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

void SAV::EditableListView::processSelectionChanged()
{
	if (m_onSelectHandler)
	{
		int index = ListView_GetNextItem(m_handle, -1, LVNI_SELECTED);
		auto data = getRowData(index);
		m_onSelectHandler.operator()(data);
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