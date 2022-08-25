#define WIN32_LEAN_AND_MEAN

#include <string>
#include <algorithm>
#include <windows.h>
#include "dinput8hook.h"
#include "hooks.h"

void Init(HMODULE hModule)
{
	dinput8_init();
	hooks_init();
}

BOOL APIENTRY DllMain(HMODULE Module,
	DWORD  ReasonForCall,
	LPVOID Reserved)
{
	switch (ReasonForCall)
	{
	case DLL_PROCESS_ATTACH:
		Init(Module);
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}
