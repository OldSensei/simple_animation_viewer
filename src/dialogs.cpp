#include <array>

#include <shobjidl_core.h>

#include "dialogs.hpp"

namespace
{
	const std::array<COMDLG_FILTERSPEC, 1> gSaveFileFilter = { {L"Simple Animation Viwer data (*.sav)", L"*.sav"} };
}

namespace SAV
{
	std::optional<std::wstring> saveFileDialog()
	{
		std::optional<std::wstring> result = std::nullopt;
		if (SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
		{
			IFileDialog* saveFileDialog;
			if (SUCCEEDED(CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&saveFileDialog))))
			{
				if (SUCCEEDED(saveFileDialog->SetFileTypes(gSaveFileFilter.size(), gSaveFileFilter.data())))
				{
					if (SUCCEEDED(saveFileDialog->SetFileTypeIndex(0)))
					{
						if (SUCCEEDED(saveFileDialog->SetDefaultExtension(L"sav")))
						{
							DWORD options;
							if (SUCCEEDED(saveFileDialog->GetOptions(&options)))
							{
								saveFileDialog->SetOptions(options | FOS_FORCEFILESYSTEM);
								if (SUCCEEDED(saveFileDialog->Show(NULL)))
								{
									IShellItem* pItem;
									if (SUCCEEDED(saveFileDialog->GetResult(&pItem)))
									{
										PWSTR filePath;
										if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &filePath)))
										{
											result.emplace(filePath);
											CoTaskMemFree(filePath);
										}
										pItem->Release();
									}
								}
							}
						}
					}
				}
			}
			CoUninitialize();
		}

		return result;
	}

	std::optional<std::wstring> loadFileDialog()
	{
		std::optional<std::wstring> result = std::nullopt;
		if (SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
		{
			IFileDialog* openDialog;
			if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&openDialog))))
			{
				if (SUCCEEDED(openDialog->SetFileTypes(gSaveFileFilter.size(), gSaveFileFilter.data())))
				{
					if (SUCCEEDED(openDialog->SetFileTypeIndex(0)))
					{
						DWORD options;
						if (SUCCEEDED(openDialog->GetOptions(&options)))
						{
							openDialog->SetOptions(options | FOS_FORCEFILESYSTEM);
							if (SUCCEEDED(openDialog->Show(NULL)))
							{
								IShellItem* pItem;
								if (SUCCEEDED(openDialog->GetResult(&pItem)))
								{
									PWSTR filePath;
									if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &filePath)))
									{
										result.emplace(filePath);
										CoTaskMemFree(filePath);
									}
									pItem->Release();
								}
							}
						}
					}
				}
			}
			CoUninitialize();
		}

		return result;
	}

	std::optional<std::wstring> selectFolderDialog()
	{
		std::optional<std::wstring> result = std::nullopt;

		if (SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)))
		{
			IFileDialog* openDialog;
			if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&openDialog))))
			{
				DWORD dwOptions;
				if (SUCCEEDED(openDialog->GetOptions(&dwOptions)))
				{
					openDialog->SetOptions(dwOptions | FOS_PICKFOLDERS);

					if (SUCCEEDED(openDialog->Show(NULL)))
					{
						IShellItem* pItem;
						if (SUCCEEDED(openDialog->GetResult(&pItem)))
						{
							PWSTR folderPath;

							if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &folderPath)))
							{
								result.emplace(folderPath);
								CoTaskMemFree(folderPath);
							}
							pItem->Release();
						}
					}
				}
			}
			CoUninitialize();
		}
		return result;
	}
}