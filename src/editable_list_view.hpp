#pragma once
#include <Windows.h>

#include <string>
#include <type_traits>
#include <vector>
#include <functional>
#include <cstdint>


namespace SAV
{
	struct HeaderDescription
	{
		std::wstring caption;
		std::uint32_t headerWidthPercent;
	};

	class EditableListView
	{
	public:
		using HandlerToken = std::uint32_t;
		using OnSelectHandler = std::function<void(const std::vector<std::wstring>&)>;

	public:
		explicit EditableListView(HWND parent, const RECT& position, const OnSelectHandler& handler);

		template<typename Container, typename T = typename Container::value_type, typename = std::enable_if_t<std::is_same_v<HeaderDescription, T>>>
		void createHeaders(const Container& descriptions)
		{
			int index = 0;
			for(const auto& header : descriptions)
			{
				addHeader(header, index);
				++index;
			}
		}

		void updateData(const std::vector<std::vector<std::wstring>>& data);
		bool processNotify(WPARAM wp, LPARAM lp);

		void processSelectionChanged();

		std::vector<std::vector<std::wstring>> getListViewData() const;

	private:
		void addHeader(const HeaderDescription& description, int index);
		void showInplaceEditControl(int itemIndex, int subItemIndex);
		std::vector<std::wstring> getRowData(int index) const;

	private:
		HWND m_handle;
		int m_width;
		int m_height;
		float m_widthPercent;

		OnSelectHandler m_onSelectHandler;
	};

}