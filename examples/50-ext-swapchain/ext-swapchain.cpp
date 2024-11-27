/*
 * Copyright 2011-2024 Branimir Karadzic. All rights reserved.
 * License: https://github.com/bkaradzic/bgfx/blob/master/LICENSE
 */

#include <bx/uint32_t.h>
#include "common.h"
#include "bgfx_utils.h"
#include "imgui/imgui.h"


#include <iostream>
#include <bx/os.h>

#define EXT_D3D11
//#define EXT_D3D12
//#define EXT_GL_SDL
//#define EXT_GL
//#define EXT_GLES
//#define EXT_METAL
//#define EXT_VULKAN

namespace
{

	class SwapChainBackend
	{
	public:
		struct SwapchainCreateInfo {
			uint32_t width;
			uint32_t height;
			uint32_t count;
			void* windowHandle;
			int64_t format;
			bool vsync;
		};

		virtual void* CreateDesktopSwapchain(const SwapchainCreateInfo& swapchainCI) = 0;
		virtual void DestroyDesktopSwapchain(void*& swapchain) = 0;
		virtual void PresentDesktopSwapchainImage(void* swapchain, uint32_t index) = 0;
		virtual void* GetContext(void*& swapchain) const = 0;
		virtual void* GetBackBuffer(void*& swapchain) const = 0;
		virtual void* GetBackBufferDS(void*& swapchain) const = 0;
	};


#ifdef EXT_D3D11
}
#include <d3d11_1.h>
#include <dxgi1_6.h>
namespace{

#define D3D11_CHECK(x, y)                                                                         \
    {                                                                                             \
        HRESULT result = (x);                                                                     \
        if (FAILED(result)) {                                                                     \
            std::cout << "ERROR: D3D11: " << std::hex << "0x" << result << std::dec << std::endl; \
            std::cout << "ERROR: D3D11: " << y << std::endl;                                      \
        }                                                                                         \
    }

#define D3D11_SAFE_RELEASE(p)                                                                                                                                                                \
   {                                                                                                                                                                                         \
      if (p)                                                                                                                                                                                 \
      {                                                                                                                                                                                      \
         (p)->Release();                                                                                                                                                                     \
         (p) = nullptr;                                                                                                                                                                      \
      }                                                                                                                                                                                      \
   }

	static const GUID IID_ID3D11Texture2D = { 0x6f15aaf2, 0xd208, 0x4e89, { 0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c } };

	class D3D11Backend : public SwapChainBackend {
	public:
		D3D11Backend()
		{
			const char* dxgiDllName = "dxgi.dll";
			m_dxgiDll = bx::dlopen(dxgiDllName);
			//typedef HRESULT(WINAPI* PFN_CREATE_DXGI_FACTORY)(REFIID _riid, void** _factory);
			//PFN_CREATE_DXGI_FACTORY CreateDXGIFactory1 = (PFN_CREATE_DXGI_FACTORY)bx::dlsym(m_dxgiDll, "CreateDXGIFactory1");
			typedef HRESULT(WINAPI* PFN_CREATE_DXGI_FACTORY2)(UINT Flags, REFIID _riid, void** _factory);
			PFN_CREATE_DXGI_FACTORY2 CreateDXGIFactory2 = (PFN_CREATE_DXGI_FACTORY2)bx::dlsym(m_dxgiDll, "CreateDXGIFactory2");
			D3D11_CHECK(CreateDXGIFactory2(0, IID_PPV_ARGS(&m_factory)), "Failed to create DXGI factory.");

			IDXGIAdapter* adapter = nullptr;
			DXGI_ADAPTER_DESC adapterDesc = {};
			for (UINT i = 0; m_factory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
			{
				adapter->GetDesc(&adapterDesc);
				break;
			}
			D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
			const char* d3d11DllName = "d3d11.dll";
			m_d3d11Dll = bx::dlopen(d3d11DllName);
			PFN_D3D11_CREATE_DEVICE D3D11CreateDevice = (PFN_D3D11_CREATE_DEVICE)bx::dlsym(m_d3d11Dll, "D3D11CreateDevice");
			D3D11_CHECK(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, 0, D3D11_CREATE_DEVICE_DEBUG, &featureLevel, 1, D3D11_SDK_VERSION, &m_device, nullptr, &m_immediateContext), "Failed to create D3D11 Device.");
		}

		~D3D11Backend()
		{
			D3D11_SAFE_RELEASE(m_immediateContext);
			D3D11_SAFE_RELEASE(m_device);
			D3D11_SAFE_RELEASE(m_factory);

			if (m_dxgiDll)
				bx::dlclose(m_dxgiDll);
			if (m_d3d11Dll)
				bx::dlclose(m_d3d11Dll);
		}

		void* CreateDesktopSwapchain(const SwapchainCreateInfo& swapchainCI) override
		{
			DXGISwapChain* sc = new DXGISwapChain;
			sc->info = swapchainCI;
			DXGI_SWAP_CHAIN_DESC1 swapchainDesc;
			swapchainDesc.Width = swapchainCI.width;
			swapchainDesc.Height = swapchainCI.height;
			swapchainDesc.Format = (DXGI_FORMAT)swapchainCI.format;
			swapchainDesc.Stereo = false;
			swapchainDesc.SampleDesc = { 1, 0 };
			swapchainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			swapchainDesc.BufferCount = swapchainCI.count;  // One of these is locked by Windows
			swapchainDesc.Scaling = DXGI_SCALING_STRETCH;
			swapchainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
			swapchainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
			swapchainDesc.Flags = swapchainCI.vsync == false ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
			D3D11_CHECK(m_factory->CreateSwapChainForHwnd(m_device, (HWND)swapchainCI.windowHandle, &swapchainDesc, nullptr, nullptr, &sc->swapchain), "Failed to create Swapchain.");

			{
				ID3D11Texture2D* backBufferColor = NULL;
				D3D11_CHECK(sc->swapchain->GetBuffer(0, IID_ID3D11Texture2D, (void**)&backBufferColor), "Failed to retrieve color buffer from swapchain");
				D3D11_RENDER_TARGET_VIEW_DESC desc;
				desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
				desc.Texture2D.MipSlice = 0;
				desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				D3D11_CHECK(m_device->CreateRenderTargetView(backBufferColor, &desc, &sc->backBufferColor), "Failed to create color buffer view from swapchain");
				D3D11_SAFE_RELEASE(backBufferColor);
			}

			{
				D3D11_TEXTURE2D_DESC dsd;
				dsd.Width = swapchainCI.width;
				dsd.Height = swapchainCI.height;
				dsd.MipLevels = 1;
				dsd.ArraySize = 1;
				dsd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
				dsd.SampleDesc = { 1, 0 }; // DXGI_SAMPLE_DESC
				dsd.Usage = D3D11_USAGE_DEFAULT;
				dsd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
				dsd.CPUAccessFlags = 0;
				dsd.MiscFlags = 0;

				ID3D11Texture2D* depthStencil;
				D3D11_CHECK(m_device->CreateTexture2D(&dsd, NULL, &depthStencil), "Failed to retrieve depth buffer from swapchain");
				D3D11_CHECK(m_device->CreateDepthStencilView(depthStencil, NULL, &sc->backBufferDepthStencil), "Failed to create depth buffer view from swapchain");
				D3D11_SAFE_RELEASE(depthStencil);
			}

			return sc;
		}

		void DestroyDesktopSwapchain(void*& swapchain) override
		{
			DXGISwapChain* sc = reinterpret_cast<DXGISwapChain*>(swapchain);
			D3D11_SAFE_RELEASE(sc->backBufferColor);
			D3D11_SAFE_RELEASE(sc->backBufferDepthStencil);
			D3D11_SAFE_RELEASE(sc->swapchain);
			swapchain = nullptr;
			delete sc;
		}

		void PresentDesktopSwapchainImage(void* swapchain, uint32_t index) override
		{
			BX_UNUSED(index)
				DXGISwapChain* sc = reinterpret_cast<DXGISwapChain*>(swapchain);
			if (sc->info.vsync)
			{
				D3D11_CHECK(sc->swapchain->Present(1, 0), "Failed to present the Image from Swapchain.");
			}
			else
			{
				D3D11_CHECK(sc->swapchain->Present(0, DXGI_PRESENT_ALLOW_TEARING), "Failed to present the Image from Swapchain.");
			}
		}

		void* GetContext(void*& swapchain) const override
		{
			BX_UNUSED(swapchain)
				return m_device;
		}

		void* GetBackBuffer(void*& swapchain) const override
		{
			DXGISwapChain* sc = reinterpret_cast<DXGISwapChain*>(swapchain);
			return sc->backBufferColor;
		}

		void* GetBackBufferDS(void*& swapchain) const override
		{
			DXGISwapChain* sc = reinterpret_cast<DXGISwapChain*>(swapchain);
			return sc->backBufferDepthStencil;
		}

	private:
		void* m_dxgiDll = nullptr;
		void* m_d3d11Dll = nullptr;

		IDXGIFactory4* m_factory = nullptr;
		ID3D11Device* m_device = nullptr;
		ID3D11DeviceContext* m_immediateContext = nullptr;

		struct DXGISwapChain
		{
			SwapchainCreateInfo info;
			IDXGISwapChain1* swapchain = nullptr;
			bool desktopSwapchainVsync = false;
			ID3D11RenderTargetView* backBufferColor = nullptr;
			ID3D11DepthStencilView* backBufferDepthStencil = nullptr;
		};
	};
#endif


class ExampleExtSwapChain : public entry::AppI
{
public:
	ExampleExtSwapChain(const char* _name, const char* _description, const char* _url)
		: entry::AppI(_name, _description, _url)
	{
	}

	void init(int32_t _argc, const char* const* _argv, uint32_t _width, uint32_t _height) override
	{
		if (_argc < 0)
		{
			if (m_backend && m_swapchain)
            m_backend->PresentDesktopSwapchainImage(m_swapchain, 0);
			return;
		}
		Args args(_argc, _argv);

		m_width  = _width;
		m_height = _height;
		m_debug  = BGFX_DEBUG_TEXT;
		m_reset  = BGFX_RESET_VSYNC;

		m_backend = new D3D11Backend();
		SwapChainBackend::SwapchainCreateInfo sci;
		sci.width = m_width;
		sci.height = m_height;
		sci.count = 2;
		sci.vsync = true;
		sci.windowHandle = entry::getNativeWindowHandle(entry::kDefaultWindowHandle);
		sci.format = DXGI_FORMAT_R8G8B8A8_UNORM;
		m_swapchain = m_backend->CreateDesktopSwapchain(sci);

		bgfx::Init init;
		init.type = bgfx::RendererType::Enum::Direct3D11;//args.m_type;
		init.vendorId = args.m_pciId;
		init.platformData.nwh  = entry::getNativeWindowHandle(entry::kDefaultWindowHandle);
		init.platformData.ndt  = entry::getNativeDisplayHandle();
		init.platformData.type = entry::getNativeWindowHandleType();
		init.platformData.context = m_backend->GetContext(m_swapchain);
		init.platformData.backBuffer = m_backend->GetBackBuffer(m_swapchain);
		init.platformData.backBufferDS = m_backend->GetBackBufferDS(m_swapchain);
		init.resolution.width  = m_width;
		init.resolution.height = m_height;
		init.resolution.reset  = m_reset;
		bgfx::init(init);

		// Enable debug text.
		bgfx::setDebug(m_debug);

		// Set view 0 clear state.
		bgfx::setViewClear(0
			, BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH
			, 0x303030ff
			, 1.0f
			, 0
			);

		imguiCreate();
	}

	virtual int shutdown() override
	{
		imguiDestroy();

		// Shutdown bgfx.
		bgfx::shutdown();

		if (m_swapchain)
			m_backend->DestroyDesktopSwapchain(m_swapchain);
		delete m_backend;

		return 0;
	}

	bool update() override
	{
		if (!entry::processEvents(m_width, m_height, m_debug, m_reset, &m_mouseState) )
		{
			imguiBeginFrame(m_mouseState.m_mx
				,  m_mouseState.m_my
				, (m_mouseState.m_buttons[entry::MouseButton::Left  ] ? IMGUI_MBUT_LEFT   : 0)
				| (m_mouseState.m_buttons[entry::MouseButton::Right ] ? IMGUI_MBUT_RIGHT  : 0)
				| (m_mouseState.m_buttons[entry::MouseButton::Middle] ? IMGUI_MBUT_MIDDLE : 0)
				,  m_mouseState.m_mz
				, uint16_t(m_width)
				, uint16_t(m_height)
				);

			showExampleDialog(this);

			imguiEndFrame();

			// Set view 0 default viewport.
			bgfx::setViewRect(0, 0, 0, uint16_t(m_width), uint16_t(m_height) );

			// This dummy draw call is here to make sure that view 0 is cleared
			// if no other draw calls are submitted to view 0.
			bgfx::touch(0);

			// Use debug font to print information about this example.
			bgfx::dbgTextClear();

			const bgfx::Stats* stats = bgfx::getStats();

			bgfx::dbgTextPrintf(0, 1, 0x0f, "Color can be changed with ANSI \x1b[9;me\x1b[10;ms\x1b[11;mc\x1b[12;ma\x1b[13;mp\x1b[14;me\x1b[0m code too.");

			bgfx::dbgTextPrintf(80, 1, 0x0f, "\x1b[;0m    \x1b[;1m    \x1b[; 2m    \x1b[; 3m    \x1b[; 4m    \x1b[; 5m    \x1b[; 6m    \x1b[; 7m    \x1b[0m");
			bgfx::dbgTextPrintf(80, 2, 0x0f, "\x1b[;8m    \x1b[;9m    \x1b[;10m    \x1b[;11m    \x1b[;12m    \x1b[;13m    \x1b[;14m    \x1b[;15m    \x1b[0m");

			bgfx::dbgTextPrintf(0, 2, 0x0f, "Backbuffer %dW x %dH in pixels, debug text %dW x %dH in characters."
				, stats->width
				, stats->height
				, stats->textWidth
				, stats->textHeight
				);

			// Advance to next frame. Rendering thread will be kicked to
			// process submitted rendering primitives.
			bgfx::frame();

			//m_backend->PresentDesktopSwapchainImage(m_swapchain, 0);

			return true;
		}

		return false;
	}

	D3D11Backend* m_backend = nullptr;
	void* m_swapchain = nullptr;

	entry::MouseState m_mouseState;

	uint32_t m_width;
	uint32_t m_height;
	uint32_t m_debug;
	uint32_t m_reset;
};

} // namespace

ENTRY_IMPLEMENT_MAIN(
	  ExampleExtSwapChain
	, "50-External-SwapChain"
	, "Use an external swapchain instead of BGFX builtin."
	, "https://bkaradzic.github.io/bgfx/examples.html#ext-swapchain"
	);
