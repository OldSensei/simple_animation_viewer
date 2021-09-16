#pragma once
#include <Windows.h>

#include <array>
#include <charconv>
#include <optional>
#include <sstream>
#include <type_traits>

namespace SAV::Utils
{
	template<typename StringType>
	std::optional<std::uint64_t> fromString(const StringType& data)
	{
		std::uint64_t value = 0;
		std::array<char, 10> buf = { 0 };

		int len = 0;
		if constexpr (std::is_same_v<typename StringType::value_type, wchar_t>)
		{
			len = WideCharToMultiByte(CP_UTF8, 0, data.c_str(), static_cast<int>(data.length()), buf.data(), static_cast<int>(buf.size()), nullptr, nullptr);
		}
		else
		{
			len = static_cast<int>(data.size());
			std::copy(data.begin(), data.end(), buf.begin());
		}

		auto result = std::from_chars(buf.data(), buf.data() + len, value);
		if (result.ec == std::errc())
		{
			return std::optional<std::uint64_t>(value);
		}
		return std::nullopt;
	}

	template<typename ... Args>
	void debugPrint(const Args& ... args)
	{
		std::stringstream ss;
		(ss << ... << args) << "\n";
		::OutputDebugStringA(ss.str().data());
	}
} // SAV::Utils