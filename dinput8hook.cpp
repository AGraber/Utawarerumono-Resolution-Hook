#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <Unknwn.h>
#include <stdio.h>

typedef HRESULT(*DirectInput8Create_t)(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter); // function pointer
HMODULE hDinput8Dll; // dinput8 dll module

DirectInput8Create_t pfnDirectInput8Create = nullptr;

void dinput8_init()
{
	// Load the original dinput8.dll from the system directory
	char DInputDllName[MAX_PATH];
	GetSystemDirectoryA(DInputDllName, MAX_PATH);
	strcat_s(DInputDllName, "\\dinput8.dll");
	hDinput8Dll = LoadLibraryA(DInputDllName);
	if (hDinput8Dll > (HMODULE)31)
	{
		pfnDirectInput8Create = (DirectInput8Create_t)GetProcAddress(hDinput8Dll, "DirectInput8Create");
	}
}
extern "C" {
	__declspec(dllexport) HRESULT DirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter)
	{
		if (pfnDirectInput8Create)
		{
			return pfnDirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
		}
		return S_FALSE;
	}
}
