#include <charconv>
#include <cstdint>
#include <fstream>
#include "program_data.hpp"

// disable narrow conversion warning because of std::string(wstring)
#pragma warning( disable : 4244 ) 

namespace SAV
{
	std::chrono::milliseconds AnimationDescription::toMs(std::wstring_view duration)
	{
		std::string durationString(duration.begin(), duration.end());
		std::uint32_t value = 0;
		if (auto [c, ec] = std::from_chars(durationString.data(), durationString.data() + durationString.size(), value); ec != std::errc())
		{
			value = 0;
		}

		return std::chrono::milliseconds{value};
	}

	AnimationDescription::AnimationDescription(std::wstring_view rowData) :
		m_filepath{},
		m_duration{ 0 }
	{
		auto pos = rowData.find(AnimationDescription::delim);
		if (pos == std::wstring::npos)
		{
			throw std::exception();
		}
		auto filepath = rowData.substr(0, pos);
		m_filepath.assign(filepath);

		m_duration = toMs(rowData.substr(pos + 1));
	}

	std::wstring AnimationDescription::name() const
	{
		std::filesystem::path path{ m_filepath };
		return path.filename().wstring();
	}

	std::wstring AnimationDescription::toRowData() const
	{
		return m_filepath + delim + std::to_wstring(m_duration.count());
	}

	void AnimationData::saveToFile(const std::filesystem::path& file, const std::vector<std::vector<std::wstring>>& rawAnimationData) const
	{
		Animations animations;
		for (const auto& row : rawAnimationData)
		{
			const auto& name = row[0];
			auto animationFile = getAnimationFilePath(name);
			if (animationFile)
			{
				const auto& duration = row[1];
				animations.emplace_back( animationFile->wstring(), duration );
			}
		}
		saveToFile(file, animations);
	}

	void AnimationData::saveToFile(const std::filesystem::path& file, const Animations& animations) const
	{
		std::wofstream output(file.wstring(), std::ios_base::trunc);
		for (const auto& animation : animations)
		{
			output << animation.toRowData() << std::endl;
		}
	}

	AnimationData::Animations AnimationData::loadFromFolder(const std::filesystem::path& folder)
	{
		Animations animations;
		std::transform(std::filesystem::directory_iterator(folder), std::filesystem::directory_iterator(), std::back_inserter(animations),
			[this](const auto& file)
			{
				SAV::AnimationDescription desc{ file.path() };
				if (auto it = this->m_animationFiles.find(desc.name()); it == this->m_animationFiles.end())
				{
					this->m_animationFiles[desc.name()] = desc.path();
				}
				return desc;
			});

		return animations;
	}

	AnimationData::Animations AnimationData::loadFromFile(const std::filesystem::path& file)
	{
		std::wifstream input(file.wstring());
		std::wstring rowData;
		AnimationData::Animations animations;
		while (std::getline(input, rowData))
		{
			if (!rowData.empty())
			{
				AnimationDescription desc{ std::wstring_view{rowData} };
				auto it = m_animationFiles.find(desc.name());
				if (it == m_animationFiles.end())
				{
					m_animationFiles[desc.name()] = desc.path().wstring();
				}
				
				animations.emplace_back( std::move(desc) );
			}
		}

		return animations;
	}

	std::optional<std::filesystem::path> AnimationData::getAnimationFilePath(std::wstring_view name) const
	{
		if (auto it = m_animationFiles.find(std::wstring(name)); it != m_animationFiles.end())
		{
			return std::optional<std::filesystem::path>{ std::in_place, it->second };
		}

		return std::nullopt;
	}

	/*bool AnimationData::update(std::wstring_view animationName, std::wstring_view duration)
	{
		auto it = std::find_if(animations.begin(), animations.end(),
								[animationName]( const auto& animation)
								{ return animationName == animation.name(); });
		if (it != animations.end())
		{
			it->setDuration(duration);
			return true;
		}

		return false;
	}*/
}