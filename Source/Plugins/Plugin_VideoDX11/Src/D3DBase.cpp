// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include "D3DBase.h"
#include "D3DTexture.h"
#include "D3DShader.h"
#include "D3Dcompiler.h"
#include "VideoConfig.h"
#include "Render.h"
#include "XFStructs.h"

#include <map>
#include <string>
#include <algorithm>
using namespace std;

namespace D3D
{

ID3D11Device* device = NULL;
ID3D11DeviceContext* context = NULL;
IDXGISwapChain* swapchain = NULL;
D3D_FEATURE_LEVEL featlevel;
D3DTexture2D* backbuf = NULL;
HWND hWnd;

#define NUM_SUPPORTED_FEATURE_LEVELS 3
const D3D_FEATURE_LEVEL supported_feature_levels[NUM_SUPPORTED_FEATURE_LEVELS] = {
	D3D_FEATURE_LEVEL_11_0,
	D3D_FEATURE_LEVEL_10_1,
	D3D_FEATURE_LEVEL_10_0
};

unsigned int xres, yres;

bool bFrameInProgress = false;

HRESULT Create(HWND wnd)
{
	hWnd = wnd;
	HRESULT hr;

	RECT client;
	GetClientRect(hWnd, &client);
	xres = client.right - client.left;
	yres = client.bottom - client.top;

	IDXGIFactory* factory;
	IDXGIAdapter* adapter;
	IDXGIOutput* output;
	hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&factory);
	if (FAILED(hr)) MessageBox(wnd, _T("Failed to create IDXGIFactory object"), _T("Dolphin Direct3D 11 plugin"), MB_OK | MB_ICONERROR);

	hr = factory->EnumAdapters(g_ActiveConfig.iAdapter, &adapter);
	if (FAILED(hr))
	{
		// try using the first one
		hr = factory->EnumAdapters(0, &adapter);
		if (FAILED(hr)) MessageBox(wnd, _T("Failed to enumerate adapters"), _T("Dolphin Direct3D 11 plugin"), MB_OK | MB_ICONERROR);
	}

	// TODO: Make this configurable
	hr = adapter->EnumOutputs(0, &output);
	if (FAILED(hr))
	{
		// try using the first one
		hr = adapter->EnumOutputs(0, &output);
		if (FAILED(hr)) MessageBox(wnd, _T("Failed to enumerate outputs"), _T("Dolphin Direct3D 11 plugin"), MB_OK | MB_ICONERROR);
	}

	// this will need to be changed once multisampling gets implemented
	DXGI_SWAP_CHAIN_DESC swap_chain_desc;
	memset(&swap_chain_desc, 0, sizeof(swap_chain_desc));
	swap_chain_desc.BufferCount = 1;
	swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swap_chain_desc.OutputWindow = wnd;
	swap_chain_desc.SampleDesc.Count = 1;
	swap_chain_desc.SampleDesc.Quality = 0;
	swap_chain_desc.Windowed = TRUE;

	DXGI_MODE_DESC mode_desc;
	memset(&mode_desc, 0, sizeof(mode_desc));
	mode_desc.Width = xres;
	mode_desc.Height = yres;
	mode_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	mode_desc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	hr = output->FindClosestMatchingMode(&mode_desc, &swap_chain_desc.BufferDesc, NULL);
	if (FAILED(hr)) MessageBox(wnd, _T("Failed to find a supported video mode"), _T("Dolphin Direct3D 11 plugin"), MB_OK | MB_ICONERROR);

	// forcing buffer resolution to xres and yres.. TODO: The new video mode might not actually be supported!
	swap_chain_desc.BufferDesc.Width = xres;
	swap_chain_desc.BufferDesc.Height = yres;

#if defined(_DEBUG) || defined(DEBUGFAST)
	D3D11_CREATE_DEVICE_FLAG device_flags = (D3D11_CREATE_DEVICE_FLAG)(D3D11_CREATE_DEVICE_DEBUG|D3D11_CREATE_DEVICE_SINGLETHREADED);
#else
	D3D11_CREATE_DEVICE_FLAG device_flags = D3D11_CREATE_DEVICE_SINGLETHREADED;
#endif
	hr = D3D11CreateDeviceAndSwapChain(adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, device_flags,
										supported_feature_levels, NUM_SUPPORTED_FEATURE_LEVELS,
										D3D11_SDK_VERSION, &swap_chain_desc, &swapchain, &device,
										&featlevel, &context);
	if (FAILED(hr) || !device || !context || !swapchain)
	{
		MessageBox(wnd, _T("Failed to initialize Direct3D.\nMake sure your video card supports at least D3D 10.0"), _T("Dolphin Direct3D 11 plugin"), MB_OK | MB_ICONERROR);
		SAFE_RELEASE(device);
		SAFE_RELEASE(context);
		SAFE_RELEASE(swapchain);
		return E_FAIL;
	}
	SetDebugObjectName((ID3D11DeviceChild*)context, "device context");
	factory->Release();
	output->Release();
	adapter->Release();

	ID3D11Texture2D* buf;
	hr = swapchain->GetBuffer(0, IID_ID3D11Texture2D, (void**)&buf);
	if (FAILED(hr))
	{
		MessageBox(wnd, _T("Failed to get swapchain buffer"), _T("Dolphin Direct3D 11 plugin"), MB_OK | MB_ICONERROR);
		device->Release();
		context->Release();
		swapchain->Release();
		return E_FAIL;
	}
	backbuf = new D3DTexture2D(buf, D3D11_BIND_RENDER_TARGET);
	buf->Release();
	CHECK(backbuf!=NULL, "Create back buffer texture");
	SetDebugObjectName((ID3D11DeviceChild*)backbuf->GetTex(), "backbuffer texture");
	SetDebugObjectName((ID3D11DeviceChild*)backbuf->GetRTV(), "backbuffer render target view");

	context->OMSetRenderTargets(1, &backbuf->GetRTV(), NULL);

	gfxstate = new EmuGfxState;
	stateman = new StateManager;
	return S_OK;
}

void Close()
{
	// release all bound resources
	context->ClearState();
	SAFE_RELEASE(backbuf);
	SAFE_RELEASE(swapchain);
	SAFE_DELETE(gfxstate);
	SAFE_DELETE(stateman);
	context->Flush();  // immediately destroy device objects

	SAFE_RELEASE(context);
	ULONG references = device->Release();
	if (references)
	{
		ERROR_LOG(VIDEO, "Unreleased references: %i.", references);
	}
	else
	{
		NOTICE_LOG(VIDEO, "Successfully released all device references!");
	}
	device = NULL;
}

/* just returning the 4_0 ones here */
const char* VertexShaderVersionString() { return "vs_4_0"; }
const char* PixelShaderVersionString() { return "ps_4_0"; }

D3DTexture2D* &GetBackBuffer() { return backbuf; }
unsigned int GetBackBufferWidth() { return xres; }
unsigned int GetBackBufferHeight() { return yres; }

void Reset()
{
	// release all back buffer references
	SAFE_RELEASE(backbuf);

	// resize swapchain buffers
	RECT client;
	GetClientRect(hWnd, &client);
	xres = client.right - client.left;
	yres = client.bottom - client.top;
	D3D::swapchain->ResizeBuffers(1, xres, yres, DXGI_FORMAT_R8G8B8A8_UNORM, 0);

	// recreate back buffer texture
	ID3D11Texture2D* buf;
	HRESULT hr = swapchain->GetBuffer(0, IID_ID3D11Texture2D, (void**)&buf);
	if (FAILED(hr))
	{
		MessageBox(hWnd, _T("Failed to get swapchain buffer"), _T("Dolphin Direct3D 11 plugin"), MB_OK | MB_ICONERROR);
		device->Release();
		context->Release();
		swapchain->Release();
		return;
	}
	backbuf = new D3DTexture2D(buf, D3D11_BIND_RENDER_TARGET);
	buf->Release();
	CHECK(backbuf!=NULL, "Create back buffer texture");
	SetDebugObjectName((ID3D11DeviceChild*)backbuf->GetTex(), "backbuffer texture");
	SetDebugObjectName((ID3D11DeviceChild*)backbuf->GetRTV(), "backbuffer render target view");
}

bool BeginFrame()
{
	if (bFrameInProgress)
	{
		PanicAlert("BeginFrame called although a frame is already in progress");
		return false;
	}
	bFrameInProgress = true;
	return (device != NULL);
}

void EndFrame()
{
	if (!bFrameInProgress)
	{
		PanicAlert("EndFrame called although no frame is in progress");
		return;
	}
	bFrameInProgress = false;
}

void Present()
{
	// TODO: Is 1 the correct value for vsyncing?
	swapchain->Present((UINT)g_ActiveConfig.bVSync, 0);
}

}  // namespace
