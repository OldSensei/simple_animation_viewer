#pragma once
#include <Windows.h>

#include <functional>
#include <string>
#include <stack>
#include <memory>
#include <vector>
#include <optional>
#include <utility>

namespace SAV
{
	namespace Layout
	{
		enum class LayoutDirectionType : std::uint32_t
		{
			Vertical,
			Horizontal
		};

		struct Vector2
		{
			float x = 0.f;
			float y = 0.f;
		};

		struct ItemDimensions
		{
			std::uint32_t x;
			std::uint32_t y;
			std::uint32_t width;
			std::uint32_t height;

			operator ::RECT() const;
			ItemDimensions operator*(const Vector2& ratios) const;
		};

		struct ItemMargin
		{
			std::uint32_t left;
			std::uint32_t top;
			std::uint32_t right;
			std::uint32_t bottom;

			ItemMargin operator*(const Vector2& ratios) const;
		};

		class BoxLayout
		{
		public:
			BoxLayout(const std::string& name, const ItemDimensions& dimensions, const ItemMargin& margins = {0, 0, 0, 0}) :
				m_name{ name },
				m_dimensions{ dimensions },
				m_restWidth{ dimensions.width - (margins.left + margins.right) },
				m_restHeight{ dimensions.height - (margins.top + margins.bottom) },
				m_margins{ margins }
			{}

			virtual ~BoxLayout()
			{};

			ItemDimensions getDimensions() const { return m_dimensions * m_ratios; };

			template<typename LayoutType>
			auto addItem(const std::string& name, const ItemMargin& margins = { 0, 0, 0, 0 }) -> std::pair<BoxLayout*, std::shared_ptr<LayoutType>>
			{
				ItemDimensions dimensions = { m_dimensions.x + m_margins.left, m_dimensions.y + m_margins.top, m_restWidth, m_restHeight };
				return { this, addItem<LayoutType>(name, dimensions, margins) };
			}

			void resize(std::uint32_t width, std::uint32_t height);
			friend std::optional<ItemDimensions> getDimensions(const BoxLayout& root, const std::string& name);

		protected:
			template<typename LayoutType>
			std::shared_ptr<LayoutType> addItem(const std::string& name, const ItemDimensions& dimensions, const ItemMargin& margins)
			{
				auto calculatedDim = calculate(dimensions);
				if (calculatedDim)
				{
					auto item = std::make_shared<LayoutType>(name, *calculatedDim, margins);
					m_children.push_back(item);
					return item;
				}
				return nullptr;
			}

			void resize(const Vector2& ratios);
			std::uint32_t toNumber(const std::string& value, std::uint32_t maxValue = 0) const;

			virtual std::optional<ItemDimensions> calculate(const ItemDimensions& dimensions);
		protected:
			std::string m_name;
			ItemDimensions m_dimensions;
			Vector2 m_ratios = { 1.f, 1.f };
			std::uint32_t m_restWidth;
			std::uint32_t m_restHeight;
			ItemMargin m_margins;
			std::vector<std::shared_ptr<BoxLayout>> m_children = {};
		};

		template<LayoutDirectionType direction>
		class LinearBox final : public BoxLayout
		{
		public:
			template<typename ... Args>
			LinearBox(const Args& ... args) :
				BoxLayout{ args... },
				m_position{ direction == LayoutDirectionType::Horizontal ? m_margins.left : m_margins.top }
			{};

			template<typename LayoutType>
			auto addItem(const std::string& name, std::uint32_t size, const ItemMargin& margins = { 0, 0, 0, 0 }) -> std::pair<decltype(this), std::shared_ptr<LayoutType>>
			{
				ItemDimensions dimensions{ 0, 0, 0, 0 };
				if constexpr (direction == LayoutDirectionType::Horizontal)
				{
					dimensions.width = size;
				}
				else
				{
					dimensions.height = size;
				}

				return { this, BoxLayout::addItem<LayoutType>(name, dimensions, margins) };
			}

			template<typename LayoutType>
			auto addItem(const std::string& name, const std::string& size, const ItemMargin& margins = { 0, 0, 0, 0 })
			{
				std::uint32_t sizeValue = 0;
				if constexpr (direction == LayoutDirectionType::Horizontal)
				{
					sizeValue = toNumber(size, m_restWidth);
				}
				else
				{
					sizeValue = toNumber(size, m_restHeight);
				}
				return addItem<LayoutType>(name, sizeValue, margins);
			}

		protected:
			std::optional<ItemDimensions> calculate(const ItemDimensions& dimensions) override
			{
				if constexpr (direction == LayoutDirectionType::Horizontal)
				{
					const auto& width = dimensions.width;
					if (m_restWidth >= width)
					{
						ItemDimensions newDimensions{ m_dimensions.x + m_position, m_dimensions.y, width, m_restHeight };
						m_position += width;
						m_restWidth -= width;
						return std::optional<ItemDimensions>(newDimensions);
					}
					return std::nullopt;
				}
				else if constexpr (direction == LayoutDirectionType::Vertical)
				{
					const auto& height = dimensions.height;
					if (m_restHeight >= height)
					{
						ItemDimensions newDimensions{ m_dimensions.x + m_margins.left, m_dimensions.y + m_position, m_restWidth, height };
						m_position += height;
						m_restHeight -= height;
						return std::optional<ItemDimensions>(newDimensions);
					}
					return std::nullopt;
				}

				return std::nullopt;
			}

		private:
			std::uint32_t m_position = 0;
		};

		using HBoxLayout = LinearBox<LayoutDirectionType::Horizontal>;
		using VBoxLayout = LinearBox<LayoutDirectionType::Vertical>;
	} // namespace Layout
} // namespace SAV
