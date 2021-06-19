#pragma once
#include <Windows.h>

#include <string>
#include <type_traits>
#include <vector>
#include <functional>
#include <cstdint>
#include <optional>

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
		explicit EditableListView(HWND parent, const RECT& position);

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
		std::tuple<bool, int> processNotify(WPARAM wp, LPARAM lp);

		int processContextMenu(LPARAM lParam);
		void processSelectionChanged();
		void processBeginDragAndDrop(LPARAM lParam);
		bool processMouseMoving(LPARAM lParam);
		void processEndDragAndDrop(LPARAM lParam);

		void removeItem( const std::optional<int>& itemIndex = std::nullopt );
		void copyItem( const std::optional<int>& itemIndex = std::nullopt );
		void setValueToAllItem(const std::optional<int>& itemIndex = std::nullopt);

		std::vector<std::vector<std::wstring>> getListViewData() const;

		void setOnSelectHandler(const OnSelectHandler& handler)
		{
			m_onSelectHandler = handler;
		}

	private:
		struct DragAndDropContext
		{
			DragAndDropContext(HANDLE imageList, int width, int height) noexcept :
				dragImageList{ imageList },
				hotItemIndex{ -1 },
				imageWidth{ width },
				imageHeight{ height }
			{}

			~DragAndDropContext()
			{
				if (dragImageList != INVALID_HANDLE_VALUE)
				{
					ImageList_Destroy((HIMAGELIST)dragImageList);
				}
			}

			DragAndDropContext(DragAndDropContext&& context) noexcept :
				dragImageList{ context.dragImageList },
				hotItemIndex{ context.hotItemIndex },
				imageWidth{ context.imageWidth },
				imageHeight{ context.imageHeight }
			{
				context.dragImageList = INVALID_HANDLE_VALUE;
			}

			DragAndDropContext& operator=(DragAndDropContext&& context) noexcept
			{
				dragImageList = context.dragImageList;
				context.dragImageList = INVALID_HANDLE_VALUE;

				hotItemIndex = context.hotItemIndex;
				imageWidth = context.imageWidth;
				imageHeight = context.imageHeight;

				return *this;
			}

			HIMAGELIST getImageList() { return static_cast<HIMAGELIST>(dragImageList); }

			DragAndDropContext(const DragAndDropContext& context) = delete;
			DragAndDropContext& operator=(const DragAndDropContext& context) = delete;

			HANDLE dragImageList;
			POINT mouse;
			int hotItemIndex;
			int imageWidth;
			int imageHeight;
		};
		using DragAndDrop = std::optional<DragAndDropContext>;

	private:
		void addHeader(const HeaderDescription& description, int index);
		void showInplaceEditControl(int itemIndex, int subItemIndex);
		std::vector<std::wstring> getRowData(int index) const;

		void insertItem(const std::vector<std::wstring>& itemData, int index);

		DragAndDrop createDragAndDropContext(int index);
		int processCustomDraw(LPNMLVCUSTOMDRAW listViewCustomDraw);
		int processPrePaint(LPNMLVCUSTOMDRAW listViewCustomDraw);
		int processPostPaint();

	private:
		HWND m_handle;
		int m_width;
		int m_height;
		float m_widthPercent;

		OnSelectHandler m_onSelectHandler;
		DragAndDrop m_dragAndDropContext;
	};

}
