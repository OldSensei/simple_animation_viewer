#include "layout.hpp"
#include "utils.hpp"

namespace SAV::Layout
{
	ItemDimensions::operator ::RECT() const
	{
		return { static_cast<int>(x),
			static_cast<int>(y),
			static_cast<int>(x + width),
			static_cast<int>(y + height) };
	}

	ItemDimensions ItemDimensions::operator*(const Vector2& ratios) const
	{
		ItemDimensions dimensions = { 0 };
		dimensions.x = static_cast<std::uint32_t>(x * ratios.x);
		dimensions.y = static_cast<std::uint32_t>(y * ratios.y);

		dimensions.width = static_cast<std::uint32_t>(width * ratios.x);
		dimensions.height = static_cast<std::uint32_t>(height * ratios.y);

		return dimensions;
	}

	ItemMargin ItemMargin::operator*(const Vector2& ratios) const
	{
		ItemMargin margins;
		margins.left = static_cast<std::uint32_t>(left * ratios.x);
		margins.top = static_cast<std::uint32_t>(top * ratios.y);

		margins.right = static_cast<std::uint32_t>(right * ratios.x);
		margins.bottom = static_cast<std::uint32_t>(bottom * ratios.y);

		return margins;
	}

	std::optional<ItemDimensions> getDimensions(const BoxLayout& root, const std::string& name)
	{
		std::stack<std::reference_wrapper<const std::vector<std::shared_ptr<BoxLayout>>>> stack;
		if (!root.m_children.empty())
		{
			stack.push(std::cref(root.m_children));
		}

		while (!stack.empty())
		{
			auto items = stack.top();
			stack.pop();
			for (const auto& item : items.get())
			{
				if (item->m_name == name)
				{
					return item->getDimensions();
				}
				if (!item->m_children.empty())
				{
					stack.push(std::cref(item->m_children));
				}
			}
		}
		return std::nullopt;
	}

	void BoxLayout::resize(std::uint32_t width, std::uint32_t height)
	{
		Vector2 ratios{ static_cast<float>(width) / static_cast<float>(m_dimensions.width),
						static_cast<float>(height) / static_cast<float>(m_dimensions.height) };
		resize(ratios);
	}

	void BoxLayout::resize(const Vector2& ratios)
	{
		m_ratios = ratios;
		for (auto& child : m_children)
		{
			child->resize(ratios);
		}
	}

	std::optional<ItemDimensions> BoxLayout::calculate(const ItemDimensions& dimensions)
	{
		// do nothing
		return std::optional<ItemDimensions>{dimensions};
	}

	std::uint32_t BoxLayout::toNumber(const std::string& value, std::uint32_t maxValue) const
	{
		if (value.empty())
		{
			return 0;
		}

		auto last = value.size() - 1;
		if (value[last] == '*')
		{
			if (value.size() == 1)
			{
				return maxValue;
			}
			else
			{
				return 0;
			}
		}

		if (value[last] == '%')
		{
			if (value.size() == 1)
			{
				return 0;
			}
			auto stringOfNumber = value.substr(0, last);
			if (auto percent = ::SAV::Utils::fromString(stringOfNumber); percent)
			{
				float result = maxValue / 100.0f * percent.value();
				return static_cast<std::uint32_t>(result);
			}
			return 0;
		}

		if (auto number = ::SAV::Utils::fromString(value); number)
		{
			return static_cast<std::uint32_t>(*number);
		}

		return 0;
	}

} // namespace SAV::Layout