#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <optional>

namespace SAV
{
	class AnimationDescription
	{
	public:
		inline static wchar_t delim = L';';

	public:
		AnimationDescription(std::wstring_view filepath, const std::chrono::milliseconds& duration) :
			m_filepath{ filepath },
			m_duration{ duration }
		{}

		explicit AnimationDescription(const std::filesystem::path& filepath) :
			AnimationDescription{ filepath.wstring(), std::chrono::milliseconds{ 0 } }
		{}

		AnimationDescription(std::wstring_view filepath, std::wstring_view duration) :
			AnimationDescription{filepath, toMs(duration)}
		{}

		explicit AnimationDescription(std::wstring_view rowData);

		std::wstring name() const;
		const std::chrono::milliseconds& duration() const { return m_duration; };
		std::filesystem::path path() const { return std::filesystem::path{m_filepath}; }

		std::wstring toRowData() const;

		void setDuration(std::wstring_view duration) { m_duration = toMs(duration); }

	private:
		static std::chrono::milliseconds toMs(std::wstring_view duration);

	private:
		std::wstring m_filepath;
		std::chrono::milliseconds m_duration;
	};


	class AnimationData
	{
	public:
		using Animations = std::vector<AnimationDescription>;

	public:
		std::optional<std::filesystem::path> getAnimationFilePath(std::wstring_view name) const;

		Animations loadFromFolder(const std::filesystem::path& folder);
		Animations loadFromFile(const std::filesystem::path& file);

		void saveToFile(const std::filesystem::path& file, const std::vector<std::vector<std::wstring>>& rawAnimationData) const;

	private:
		void saveToFile(const std::filesystem::path& file, const Animations& animations) const;

	private:
		std::unordered_map<std::wstring, std::wstring> m_animationFiles;
	};

}
