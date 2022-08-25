#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS // i was born insecure bro

#ifdef _DEBUG
#pragma comment( lib, "libMinHook-x86-v140-mdd" )
#else
#pragma comment( lib, "libMinHook-x86-v140-md" )
#endif

#include "MinHook.h"

#include <Windows.h>
#include <stdio.h>
#include <cstring>
#include <cmath>

#include <d3d11.h>

// Poor man's debug define because the actual debug target (and hence macro) are broken
// (it triggers some runtime errors because it thinks our hooking is some kind of corruption)

//#define UTADEBUG

#ifdef UTADEBUG
#define debugprintf printf
#else
#define debugprintf(...);
#endif

// These classes are more thorough than needed for this patch
// because I directly exported them from my RE efforts on the games

struct Kernel_CGraphics
{
    void* lpVtbl;
    int pad[74];
    IDXGIFactory* pDXGIFactory;
    IDXGIAdapter* pDXGIAdapter;
    ID3D11Device* pD3D11Device;
    D3D_FEATURE_LEVEL sD3D11FeatureLevel;
    ID3D11DeviceContext* pD3D11DeviceContext;
    ID3D11InputLayout* pD3D11InputLayout1;
    ID3D11InputLayout* pD3D11InputLayout2;
    ID3D11InputLayout* pD3D11InputLayout3;
    ID3D11VertexShader* pD3D11VertexShader;
    ID3D11PixelShader* pD3D11PixelShader;
    IDXGISwapChain* pDXGISwapChain;
    ID3D11RasterizerState* pD3D11RasterizerState1;
    ID3D11RasterizerState* pD3D11RasterizerState2;
    ID3D11RasterizerState* pD3D11RasterizerState3;
};


struct Kernel_CGraphics_Texture
{
    DXGI_FORMAT dxgiFormat;
    int iTextureWidth;
    int iTextureHeight;
    int unk3;
    int iMipLevels;
    int iSomeCount;
    int unk4;
    ID3D11Texture2D* pTexture2D;
    int unk5;
    ID3D11ShaderResourceView* pShaderResourceView;
    ID3D11UnorderedAccessView* pUnorderedAccessView;
    ID3D11RenderTargetView* pRenderTargetView;
    ID3D11DepthStencilView* pDepthStencilView;
};

typedef Kernel_CGraphics_Texture* (__cdecl* CreateWorldTexture2_t)(int, int);
typedef Kernel_CGraphics_Texture* (__cdecl* CreateWorldTexture1_t)(int, int, int, int);

struct AddressesStruct
{
    void* ResizeWindow;
    void* Res3dOverride;
    void* CreateTexture;
    void* Flip;
    int* Res3dX;
    int* Res3dY;

    Kernel_CGraphics_Texture** worldTexture1;
    Kernel_CGraphics_Texture** worldTexture2;

    CreateWorldTexture1_t CreateWorldTexture1;
    CreateWorldTexture2_t CreateWorldTexture2;
};

int forcedXRes = 1280;
int forcedYRes = 720;

float UpscaleFactorX;
float UpscaleFactorY;

float SupersamplingFactor = 2.0;

bool afterRenderingWorld = false; // if we skipped copying the texture to the 720p buffer
bool drawingWorldTexture = false; // if the texture being drawn is the one from the 3d world
bool isFlipping = false; // if we're currently flipping to the swapchain
bool drewBeforePresent = false; // to avoid hooking draws just before present (prevents messing with present hooks ie. steam overlay)

Kernel_CGraphics_Texture* firstTexture = nullptr;
bool skippedSecondTexture = false;
Kernel_CGraphics_Texture* worldTexture = nullptr;

void ConsoleStuff()
{
#ifdef UTADEBUG
    AllocConsole();
    freopen("CONIN$", "r", stdin);
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);

    debugprintf("UtaHook 0.0.1 initialized\n");
#endif
}

// i copy pasted these from d3d11.h and i dont want to bother on tidying them up

typedef void (STDMETHODCALLTYPE* DrawIndexed_t)(
    ID3D11DeviceContext* this_,
    /* [annotation] */
    _In_  UINT IndexCount,
    /* [annotation] */
    _In_  UINT StartIndexLocation,
    /* [annotation] */
    _In_  INT BaseVertexLocation);

DrawIndexed_t DrawIndexed_original;

typedef void (STDMETHODCALLTYPE* RSSetViewports_t)(
    ID3D11DeviceContext* this_,
    /* [annotation] */
    _In_range_(0, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE)  UINT NumViewports,
    /* [annotation] */
    _In_reads_opt_(NumViewports)  const D3D11_VIEWPORT* pViewports);

RSSetViewports_t RSSetViewports_original;

D3D11_VIEWPORT ReplacementViewports[32];
int ReplacementViewportsCount = 0;
D3D11_RECT ReplacementRects[32];
int ReplacementRectsCount = 0;

ID3D11RenderTargetView* pLastRenderTargetView;
ID3D11DepthStencilView* pLastDepthStencilView;

void STDMETHODCALLTYPE RSSetViewports_hook(
    ID3D11DeviceContext* this_,
    /* [annotation] */
    _In_range_(0, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE)  UINT NumViewports,
    /* [annotation] */
    _In_reads_opt_(NumViewports)  const D3D11_VIEWPORT* pViewports)
{
    if (!isFlipping && afterRenderingWorld)
    {
        for (UINT i = 0; i < NumViewports; ++i)
        {
            ReplacementViewports[i] = pViewports[i];
            ReplacementViewports[i].TopLeftX *= UpscaleFactorX;
            ReplacementViewports[i].TopLeftY *= UpscaleFactorY;
            ReplacementViewports[i].Width *= UpscaleFactorX;
            ReplacementViewports[i].Height *= UpscaleFactorY;
        }
        ReplacementViewportsCount = NumViewports;
    }
    return RSSetViewports_original(this_, NumViewports, pViewports);
}

typedef void (STDMETHODCALLTYPE* RSSetScissorRects_t)(
    ID3D11DeviceContext* this_,
    /* [annotation] */
    _In_range_(0, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE)  UINT NumRects,
    /* [annotation] */
    _In_reads_opt_(NumRects)  const D3D11_RECT* pRects);

RSSetScissorRects_t RSSetScissorRects_original;

void STDMETHODCALLTYPE RSSetScissorRects_hook(
    ID3D11DeviceContext* this_,
    /* [annotation] */
    _In_range_(0, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE)  UINT NumRects,
    /* [annotation] */
    _In_reads_opt_(NumRects)  const D3D11_RECT* pRects)
{
    if (!isFlipping && afterRenderingWorld)
    {
        for (UINT i = 0; i < NumRects; ++i)
        {
            ReplacementRects[i] = pRects[i];
            ReplacementRects[i].left = (long)((float)ReplacementRects[i].left * UpscaleFactorX);
            ReplacementRects[i].top = (long)((float)ReplacementRects[i].top * UpscaleFactorY);
            ReplacementRects[i].right = (long)((float)ReplacementRects[i].right * UpscaleFactorX);
            ReplacementRects[i].bottom = (long)((float)ReplacementRects[i].bottom * UpscaleFactorY);
        }
        ReplacementRectsCount = NumRects;
    }
    return RSSetScissorRects_original(this_, NumRects, pRects);
}

typedef void (STDMETHODCALLTYPE* PSSetShaderResources_t)(
    ID3D11DeviceContext* this_,
    /* [annotation] */
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1)  UINT StartSlot,
    /* [annotation] */
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - StartSlot)  UINT NumViews,
    /* [annotation] */
    _In_reads_opt_(NumViews)  ID3D11ShaderResourceView* const* ppShaderResourceViews);

PSSetShaderResources_t PSSetShaderResources_original;

void STDMETHODCALLTYPE PSSetShaderResources_hook(
    ID3D11DeviceContext* this_,
    /* [annotation] */
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - 1)  UINT StartSlot,
    /* [annotation] */
    _In_range_(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT - StartSlot)  UINT NumViews,
    /* [annotation] */
    _In_reads_opt_(NumViews)  ID3D11ShaderResourceView* const* ppShaderResourceViews)
{
    if (!afterRenderingWorld && drawingWorldTexture)
    {
        afterRenderingWorld = true;
    }

    if (worldTexture != nullptr && NumViews == 10 && ppShaderResourceViews[0] == worldTexture->pShaderResourceView)
    {
        drawingWorldTexture = true;
    }
    else
    {
        drawingWorldTexture = false;
    }
    PSSetShaderResources_original(this_, StartSlot, NumViews, ppShaderResourceViews);
}

void STDMETHODCALLTYPE DrawIndexed_hook(
    ID3D11DeviceContext* this_,
    /* [annotation] */
    _In_  UINT IndexCount,
    /* [annotation] */
    _In_  UINT StartIndexLocation,
    /* [annotation] */
    _In_  INT BaseVertexLocation)
{
    if (isFlipping && afterRenderingWorld && !drewBeforePresent)
    {
        PSSetShaderResources_original(this_, 0, 1, &worldTexture->pShaderResourceView);
        drewBeforePresent = true;
    }
    else if (afterRenderingWorld && pLastRenderTargetView == firstTexture->pRenderTargetView)
    {
        DrawIndexed_original(this_, IndexCount, StartIndexLocation, BaseVertexLocation);
        this_->OMSetRenderTargets(1, &worldTexture->pRenderTargetView, worldTexture->pDepthStencilView);
        RSSetViewports_original(this_, ReplacementViewportsCount, ReplacementViewports);
        RSSetScissorRects_original(this_, ReplacementRectsCount, ReplacementRects);
    }
    return DrawIndexed_original(this_, IndexCount, StartIndexLocation, BaseVertexLocation);
}

void (STDMETHODCALLTYPE* Draw_original)(
    ID3D11DeviceContext* this_,
    /* [annotation] */
    _In_  UINT VertexCount,
    /* [annotation] */
    _In_  UINT StartVertexLocation);

void STDMETHODCALLTYPE Draw_hook(
    ID3D11DeviceContext* this_,
    /* [annotation] */
    _In_  UINT VertexCount,
    /* [annotation] */
    _In_  UINT StartVertexLocation)
{
    if (!drewBeforePresent && afterRenderingWorld && pLastRenderTargetView == firstTexture->pRenderTargetView)
    {
        Draw_original(this_, VertexCount, StartVertexLocation);
        this_->OMSetRenderTargets(1, &worldTexture->pRenderTargetView, worldTexture->pDepthStencilView);
        RSSetViewports_original(this_, ReplacementViewportsCount, ReplacementViewports);
        RSSetScissorRects_original(this_, ReplacementRectsCount, ReplacementRects);
    }
    return Draw_original(this_, VertexCount, StartVertexLocation);
}

typedef void (STDMETHODCALLTYPE* OMSetRenderTargetsAndUnorderedAccessViews_t)(
    ID3D11DeviceContext* this_,
    /* [annotation] */
    _In_  UINT NumRTVs,
    /* [annotation] */
    _In_reads_opt_(NumRTVs)  ID3D11RenderTargetView* const* ppRenderTargetViews,
    /* [annotation] */
    _In_opt_  ID3D11DepthStencilView* pDepthStencilView,
    /* [annotation] */
    _In_range_(0, D3D11_1_UAV_SLOT_COUNT - 1)  UINT UAVStartSlot,
    /* [annotation] */
    _In_  UINT NumUAVs,
    /* [annotation] */
    _In_reads_opt_(NumUAVs)  ID3D11UnorderedAccessView* const* ppUnorderedAccessViews,
    /* [annotation] */
    _In_reads_opt_(NumUAVs)  const UINT* pUAVInitialCounts);

OMSetRenderTargetsAndUnorderedAccessViews_t OMSetRenderTargetsAndUnorderedAccessViews_original;

void STDMETHODCALLTYPE OMSetRenderTargetsAndUnorderedAccessViews_hook(
    ID3D11DeviceContext* this_,
    /* [annotation] */
    _In_  UINT NumRTVs,
    /* [annotation] */
    _In_reads_opt_(NumRTVs)  ID3D11RenderTargetView* const* ppRenderTargetViews,
    /* [annotation] */
    _In_opt_  ID3D11DepthStencilView* pDepthStencilView,
    /* [annotation] */
    _In_range_(0, D3D11_1_UAV_SLOT_COUNT - 1)  UINT UAVStartSlot,
    /* [annotation] */
    _In_  UINT NumUAVs,
    /* [annotation] */
    _In_reads_opt_(NumUAVs)  ID3D11UnorderedAccessView* const* ppUnorderedAccessViews,
    /* [annotation] */
    _In_reads_opt_(NumUAVs)  const UINT* pUAVInitialCounts)
{
    pLastRenderTargetView = ppRenderTargetViews[0];
    pLastDepthStencilView = pDepthStencilView;

    return OMSetRenderTargetsAndUnorderedAccessViews_original(this_, NumRTVs, ppRenderTargetViews, pDepthStencilView, UAVStartSlot, NumUAVs, ppUnorderedAccessViews, pUAVInitialCounts);
}

void D3D11Hooks(ID3D11Device* pD3D11Device, ID3D11DeviceContext* pD3D11DeviceContext)
{
    // initially, i wanted to just replace the pointers from the virtual table
    // but it keep correcting itself for whatever reason
    // so we hook it with minhook

    void* pDeviceContextVtbl = *((void**)pD3D11DeviceContext);

    void* pPSSetShaderResources = *(void**)((char*)pDeviceContextVtbl + 0x20);
    MH_CreateHook(pPSSetShaderResources, PSSetShaderResources_hook, (LPVOID*)&PSSetShaderResources_original);
    MH_QueueEnableHook(pPSSetShaderResources);

    void* pDrawIndexed = *(void**)((char*)pDeviceContextVtbl + 0x30);
    MH_CreateHook(pDrawIndexed, DrawIndexed_hook, (LPVOID*)&DrawIndexed_original);
    MH_QueueEnableHook(pDrawIndexed);

    void* pDraw = *(void**)((char*)pDeviceContextVtbl + 0x34);
    MH_CreateHook(pDraw, Draw_hook, (LPVOID*)&Draw_original);
    MH_QueueEnableHook(pDraw);

    void* pOMSetRenderTargetsAndUnorderedAccessViews = *(void**)((char*)pDeviceContextVtbl + 0x88);
    MH_CreateHook(pOMSetRenderTargetsAndUnorderedAccessViews, OMSetRenderTargetsAndUnorderedAccessViews_hook, (LPVOID*)&OMSetRenderTargetsAndUnorderedAccessViews_original);
    MH_QueueEnableHook(pOMSetRenderTargetsAndUnorderedAccessViews);

    void* pRSSetViewports = *(void**)((char*)pDeviceContextVtbl + 0xB0);
    MH_CreateHook(pRSSetViewports, RSSetViewports_hook, (LPVOID*)&RSSetViewports_original);
    MH_QueueEnableHook(pRSSetViewports);

    void* pRSSetScissorRects = *(void**)((char*)pDeviceContextVtbl + 0xB4);
    MH_CreateHook(pRSSetScissorRects, RSSetScissorRects_hook, (LPVOID*)&RSSetScissorRects_original);
    MH_QueueEnableHook(pRSSetScissorRects);

    MH_ApplyQueued();
}

Kernel_CGraphics_Texture* (__thiscall* Kernel__CGraphics___CreateTexture_original)(Kernel_CGraphics* this_, unsigned int textureWidth, unsigned int textureHeight, int a4, int a5, void* a6, int a7, UINT sampleDescCount);
typedef int(__thiscall* Kernel__CGraphics___Flip_t)(Kernel_CGraphics* this_, char a2);
Kernel__CGraphics___Flip_t Kernel__CGraphics___Flip_original;

// msvc won't let me put __thiscall on normal functions but it does on static member functions
// it makes no sense (and intellisense still whines about it but it compiles just fine) but whatever
// microsoft moment
class Dummy
{
public:
    static Kernel_CGraphics_Texture* __thiscall Kernel__CGraphics___CreateTexture(Kernel_CGraphics* this_, unsigned int textureWidth, unsigned int textureHeight, int a4, int a5, void* a6, int a7, UINT sampleDescCount)
    {
#ifdef UTADEBUG
        if (textureWidth == forcedXRes)
        {
            debugprintf("texture created %d %d %d %d %x %d %d\n", textureWidth, textureHeight, a4, a5, a6, a7, sampleDescCount);
        }
#endif
        auto ret = Kernel__CGraphics___CreateTexture_original(this_, textureWidth, textureHeight, a4, a5, a6, a7, sampleDescCount);
        if (firstTexture == nullptr)
        {
            firstTexture = ret;

            D3D11Hooks(this_->pD3D11Device, this_->pD3D11DeviceContext);
        }
        else if (!skippedSecondTexture)
        {
            skippedSecondTexture = true;
        }
        else if (worldTexture == nullptr)
        {
            worldTexture = ret;
        }
        return ret;
    }

    static int __thiscall Kernel__CGraphics___Flip(Kernel_CGraphics* this_, char a2)
    {
        isFlipping = true;
        int ret = Kernel__CGraphics___Flip_original(this_, a2);
        FLOAT color[4] = {0.0, 0.0, 0.0, 0.0};
        isFlipping = false;
        drewBeforePresent = false;

        afterRenderingWorld = false;

        return ret;
    }
};

AddressesStruct Addresses;

void UpdateResolution(int resX, int resY)
{
    if ((float)resY / (float)resX != 0.5625)
    {
        char Buffer[150];
        sprintf(Buffer, "Warning: Selected resolution (%dx%d) doesn't have an aspect ratio of 16:9. Graphical glitches on 3D gameplay may appear. Please choose a 16:9 resolution from the settings menu.", resX, resY);
        MessageBoxA(NULL, Buffer, "UtaHook (3d res upscaler): Resolution warning", MB_OK | MB_ICONWARNING);
    }

    debugprintf("Resolution change: %dx%d - Resizing 3D RenderTarget to %f%%: ", resX, resY, SupersamplingFactor);
    resX = (int)((float)resX * SupersamplingFactor);
    resY = (int)((float)resY * SupersamplingFactor);
    debugprintf("%dx%d\n", resX, resY);

    forcedXRes = resX;
    forcedYRes = resY;
    *Addresses.Res3dX = forcedXRes;
    *Addresses.Res3dY = forcedYRes;

    UpscaleFactorX = (float)forcedXRes / 1280.0f;
    UpscaleFactorY = (float)forcedYRes / 720.0f;

    // i know replacing these textures technically make the old ones leak, but it's not really an issue unless you
    // change the resolution hundreds of times!
    *Addresses.worldTexture1 = worldTexture = Addresses.CreateWorldTexture1(resX, resY, 0x4000, 0x8100000);
    *Addresses.worldTexture2 = Addresses.CreateWorldTexture2(resX, resY);
}

// This hook overrides the window size, and with it the swapchain
bool ranOnce = false;
void(__fastcall* sub_50CE20_original)(int xRight, int yBottom);
void __fastcall sub_50CE20(int xRight, int yBottom)
{
    if (ranOnce)
    {
        UpdateResolution(xRight, yBottom);
    }

    ranOnce = true;
    debugprintf("sub_50CE20: %d - %d\n", xRight, yBottom);
    return sub_50CE20_original(xRight, yBottom);
}

// This hook overrides the 3d resolution
int (*sub_450AF6_original)();
int sub_450AF6_hook()
{
    debugprintf("sub_450AF6_hook ran\n");
    *Addresses.Res3dX = forcedXRes;
    *Addresses.Res3dY = forcedYRes;
    int ret = sub_450AF6_original();

    return ret;
}

bool DetermineGame()
{
    if (*(char*)0x91F5AE == '1') // prelude
    {
        debugprintf("Game detected: Prelude to the Fallen\n");
        Addresses.ResizeWindow = (void*)0x50CE20;
        Addresses.Res3dOverride = (void*)0x451492;
        Addresses.CreateTexture = (void*)0x4FD2A3;
        Addresses.Flip = (void*)0x4FE0D5;
        Addresses.Res3dX = (int*)0xA0139C;
        Addresses.Res3dY = (int*)0xA013A0;
        Addresses.CreateWorldTexture1 = (CreateWorldTexture1_t)0x4FF2EE;
        Addresses.CreateWorldTexture2 = (CreateWorldTexture2_t)0x4FF3A0;
        Addresses.worldTexture1 = (Kernel_CGraphics_Texture**)0xA01D38;
        Addresses.worldTexture2 = (Kernel_CGraphics_Texture**)0xA01538;
        return true;
    }

    if (*(char*)0x8F0792 == '2') // deception
    {
        debugprintf("Game detected: Mask of Deception\n");
        Addresses.ResizeWindow = (void*)0x503515;
        Addresses.Res3dOverride = (void*)0x42523E;
        Addresses.CreateTexture = (void*)0x4F3A9F;
        Addresses.Flip = (void*)0x4F48D1;
        Addresses.Res3dX = (int*)0x9D7584;
        Addresses.Res3dY = (int*)0x9D7588;
        Addresses.CreateWorldTexture1 = (CreateWorldTexture1_t)0x4F5AEA;
        Addresses.CreateWorldTexture2 = (CreateWorldTexture2_t)0x4F5B69;
        Addresses.worldTexture1 = (Kernel_CGraphics_Texture**)0x9D7D78;
        Addresses.worldTexture2 = (Kernel_CGraphics_Texture**)0x9D73E8;
        return true;
    }

    if (*(char*)0x91C53E == '3') // truth
    {
        debugprintf("Game detected: Mask of Truth\n");
        Addresses.ResizeWindow = (void*)0x528CA4;
        Addresses.Res3dOverride = (void*)0x436CAD;
        Addresses.CreateTexture = (void*)0x51922E;
        Addresses.Flip = (void*)0x51A060;
        Addresses.Res3dX = (int*)0xA06494;
        Addresses.Res3dY = (int*)0xA06498;
        Addresses.CreateWorldTexture1 = (CreateWorldTexture1_t)0x51B279;
        Addresses.CreateWorldTexture2 = (CreateWorldTexture2_t)0x51B2F8;
        Addresses.worldTexture1 = (Kernel_CGraphics_Texture**)0xA06AFC;
        Addresses.worldTexture2 = (Kernel_CGraphics_Texture**)0xA06488;
        return true;
    }

    debugprintf("Unknown game %c %c %c\n", *(char*)0x91F5AF, *(char*)0x8F0792, *(char*)0x91C53F);
    return false;
}

BOOL FileExists(LPCTSTR szPath)
{
    DWORD dwAttrib = GetFileAttributes(szPath);

    return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
        !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

void LoadSupersamplingRate()
{
    float fNewSupersamplingRate;
    bool bCustomSupersamplingRate = false;
    if (FileExists("supersampling.txt"))
    {
        HANDLE hFile = CreateFileA("supersampling.txt", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE)
        {
            char szReadBuffer[20]; // more than enough for a simple float
            DWORD iRead;
            if (ReadFile(hFile, szReadBuffer, 20, &iRead, NULL))
            {
                szReadBuffer[iRead] = 0;
                if (sscanf(szReadBuffer, "%f", &fNewSupersamplingRate) == 1)
                {
                    bCustomSupersamplingRate = true;
                }
            }
            
            CloseHandle(hFile);
        }
    }

    const char* pCommandLine = GetCommandLineA();
    const char* pFound = strstr(pCommandLine, "--supersampling=");

    if (pFound != nullptr)
    {
        if (sscanf(pFound + 16, "%f", &fNewSupersamplingRate) == 1)
        {
            bCustomSupersamplingRate = true;
        }
    }

    if (bCustomSupersamplingRate)
    {
        if (fNewSupersamplingRate >= 1.0)
        {
            float garbage;
            float fraction = std::modf(fNewSupersamplingRate, &garbage);
            if (fraction != 0.0 && fraction != 0.25 && fraction != 0.50 && fraction != 0.75)
            {
                MessageBoxA(NULL, "To prevent glitches, fractional values only support quarters (X.25, X.5, X.75). Defaulting to 1.5", "UtaHook (3d res upscaler): Supersampling value warning", MB_OK | MB_ICONWARNING);
            }
            else
            {
                SupersamplingFactor = fNewSupersamplingRate;
                debugprintf("Loaded custom supersampling rate: %f\n", SupersamplingFactor);
            }
        }
        else
        {
            MessageBoxA(NULL, "Only positive values starting from 1.0 are supported. Defaulting to 1.5.", "UtaHook (3d res upscaler): Supersampling value warning", MB_OK | MB_ICONWARNING);
        }
    }
}

void hooks_init()
{
    ConsoleStuff();

    // Load custom supersampling rate
    LoadSupersamplingRate();

    UpscaleFactorX = (float)forcedXRes / 1280.0f;
    UpscaleFactorY = (float)forcedYRes / 720.0f;

    if (!DetermineGame())
    {
        return;
    }

    if (MH_Initialize() != MH_OK)
    {
        MessageBoxA(NULL, "MH_Initialize != MH_OK", "MH Error", 0);
        return;
    }

    MH_CreateHook(Addresses.ResizeWindow, sub_50CE20, (LPVOID*)&sub_50CE20_original); // Window/Swapchain size hook
    MH_CreateHook(Addresses.Res3dOverride, sub_450AF6_hook, (LPVOID*)&sub_450AF6_original); // 3d res override
    MH_CreateHook(Addresses.CreateTexture, Dummy::Kernel__CGraphics___CreateTexture, (LPVOID*)&Kernel__CGraphics___CreateTexture_original);
    MH_CreateHook(Addresses.Flip, Dummy::Kernel__CGraphics___Flip, (LPVOID*)&Kernel__CGraphics___Flip_original);
    MH_EnableHook(MH_ALL_HOOKS);
}
