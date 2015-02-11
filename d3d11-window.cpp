#include "framework.h"

// Include shader binaries generated by build
#include "fullscreen_vs.h"
#include "rect_vs.h"
#include "copy_ps.h"



namespace Framework
{
	static LRESULT CALLBACK StaticMsgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	D3D11Window::D3D11Window()
	:	m_hInstance(nullptr),
		m_hWnd(nullptr),
		m_pSwapChain(),
		m_pDevice(),
		m_pCtx(),
		m_dims(makeint2(0)),
		m_pRtvSRGB(),
		m_pRtvRaw(),
		m_hasDepthBuffer(true),
		m_pDsv(),
		m_pSrvDepth(),
		m_pRsDefault(),
		m_pRsDoubleSided(),
		m_pDssDepthTest(),
		m_pDssNoDepthWrite(),
		m_pDssNoDepthTest(),
		m_pBsAdditive(),
		m_pBsAlphaBlend(),
		m_pSsPointClamp(),
		m_pSsBilinearClamp(),
		m_pSsTrilinearRepeat(),
		m_pSsTrilinearRepeatAniso(),
		m_pSsPCF(),
		m_pVsFullscreen(),
		m_pVsRect(),
		m_pPsCopy(),
		m_cbBlit()
	{
	}

	void D3D11Window::Init(
		const char * windowClassName,
		const char * windowTitle,
		HINSTANCE hInstance)
	{
		LOG("Initialization started");

		m_hInstance = hInstance;

		// Register window class
		WNDCLASS wc =
		{
			0,
			&StaticMsgProc,
			0,
			0,
			hInstance,
			LoadIcon(nullptr, IDI_APPLICATION),
			LoadCursor(nullptr, IDC_ARROW),
			nullptr,
			nullptr,
			windowClassName,
		};
		CHECK_ERR(RegisterClass(&wc));

		// Create the window
		m_hWnd = CreateWindow(
					windowClassName,
					windowTitle,
					WS_OVERLAPPEDWINDOW,
					CW_USEDEFAULT,
					CW_USEDEFAULT,
					CW_USEDEFAULT,
					CW_USEDEFAULT,
					nullptr,
					nullptr,
					hInstance,
					this);
		ASSERT_ERR(m_hWnd);

#ifdef _DEBUG
		// Take a look at the adaptors on the system, just for kicks
		comptr<IDXGIFactory> pFactory;
		CHECK_D3D(CreateDXGIFactory(__uuidof(IDXGIFactory), (void **)&pFactory));
		for (int iAdapter = 0; ; ++iAdapter)
		{
			comptr<IDXGIAdapter> pAdapter;
			HRESULT hr = pFactory->EnumAdapters(iAdapter, &pAdapter);
			if (hr == DXGI_ERROR_NOT_FOUND)
				break;
			ASSERT_ERR(SUCCEEDED(hr));

			DXGI_ADAPTER_DESC adapterDesc;
			pAdapter->GetDesc(&adapterDesc);
			if (adapterDesc.DedicatedVideoMemory > 0)
				LOG("Adapter %d: %ls (%dMB VRAM)", iAdapter, adapterDesc.Description, adapterDesc.DedicatedVideoMemory / 1048576);
			else
				LOG("Adapter %d: %ls (%dMB shared RAM)", iAdapter, adapterDesc.Description, adapterDesc.SharedSystemMemory / 1048576);
		}
#endif // _DEBUG

		// Initialize D3D11
		UINT flags = 0;
#ifdef _DEBUG
		flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
		DXGI_SWAP_CHAIN_DESC swapChainDesc =
		{
			{ 1, 1, {}, DXGI_FORMAT_R8G8B8A8_UNORM, },
			{ 1, 0, },
			DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_BACK_BUFFER,
			2,
			m_hWnd,
			true,
			DXGI_SWAP_EFFECT_DISCARD,
		};
		D3D_FEATURE_LEVEL featureLevel;
		CHECK_D3D(D3D11CreateDeviceAndSwapChain(
								nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
								flags, nullptr, 0, D3D11_SDK_VERSION,
								&swapChainDesc,
								&m_pSwapChain, &m_pDevice,
								&featureLevel, &m_pCtx));

#if defined(_DEBUG)
		// Set up D3D11 debug layer settings
		comptr<ID3D11InfoQueue> pInfoQueue;
		if (SUCCEEDED(m_pDevice->QueryInterface(__uuidof(ID3D11InfoQueue), (void **)&pInfoQueue)))
		{
			// Break in the debugger when an error or warning is issued
			pInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
			pInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);
			pInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, true);

			// Disable warning about setting private data (i.e. debug names of resources)
			D3D11_MESSAGE_ID aMsgToFilter[] =
			{
				D3D11_MESSAGE_ID_SETPRIVATEDATA_CHANGINGPARAMS,
			};
			D3D11_INFO_QUEUE_FILTER filter = {};
			filter.DenyList.NumIDs = dim(aMsgToFilter);
			filter.DenyList.pIDList = aMsgToFilter;
			pInfoQueue->AddStorageFilterEntries(&filter);
		}
#endif

		// Set up commonly used state blocks

		D3D11_RASTERIZER_DESC rssDesc =
		{
			D3D11_FILL_SOLID,
			D3D11_CULL_BACK,
			true,							// FrontCounterClockwise
			0, 0, 0,						// depth bias
			true,							// DepthClipEnable
			false,							// ScissorEnable
			true,							// MultisampleEnable
		};
		CHECK_D3D(m_pDevice->CreateRasterizerState(&rssDesc, &m_pRsDefault));

		rssDesc.CullMode = D3D11_CULL_NONE;
		CHECK_D3D(m_pDevice->CreateRasterizerState(&rssDesc, &m_pRsDoubleSided));

		D3D11_DEPTH_STENCIL_DESC dssDesc = 
		{
			true,							// DepthEnable
			D3D11_DEPTH_WRITE_MASK_ALL,
			D3D11_COMPARISON_LESS_EQUAL,
		};
		CHECK_D3D(m_pDevice->CreateDepthStencilState(&dssDesc, &m_pDssDepthTest));

		dssDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		CHECK_D3D(m_pDevice->CreateDepthStencilState(&dssDesc, &m_pDssNoDepthWrite));

		dssDesc.DepthEnable = false;
		CHECK_D3D(m_pDevice->CreateDepthStencilState(&dssDesc, &m_pDssNoDepthTest));

		D3D11_BLEND_DESC bsAdditiveDesc =
		{
			false, false,
			{
				true,
				D3D11_BLEND_ONE,
				D3D11_BLEND_ONE,
				D3D11_BLEND_OP_ADD,
				D3D11_BLEND_ONE,
				D3D11_BLEND_ONE,
				D3D11_BLEND_OP_ADD,
				D3D11_COLOR_WRITE_ENABLE_ALL,
			},
		};
		CHECK_D3D(m_pDevice->CreateBlendState(&bsAdditiveDesc, &m_pBsAdditive));

		D3D11_BLEND_DESC bsAlphaBlendDesc =
		{
			false, false,
			{
				true,
				D3D11_BLEND_SRC_ALPHA,
				D3D11_BLEND_INV_SRC_ALPHA,
				D3D11_BLEND_OP_ADD,
				D3D11_BLEND_SRC_ALPHA,
				D3D11_BLEND_INV_SRC_ALPHA,
				D3D11_BLEND_OP_ADD,
				D3D11_COLOR_WRITE_ENABLE_ALL,
			},
		};
		CHECK_D3D(m_pDevice->CreateBlendState(&bsAlphaBlendDesc, &m_pBsAlphaBlend));

		// Set up commonly used samplers

		D3D11_SAMPLER_DESC sampDesc =
		{
			D3D11_FILTER_MIN_MAG_MIP_POINT,
			D3D11_TEXTURE_ADDRESS_CLAMP,
			D3D11_TEXTURE_ADDRESS_CLAMP,
			D3D11_TEXTURE_ADDRESS_CLAMP,
			0.0f,
			1,
			D3D11_COMPARISON_FUNC(0),
			{ 0.0f, 0.0f, 0.0f, 0.0f },
			0.0f,
			FLT_MAX,
		};
		CHECK_D3D(m_pDevice->CreateSamplerState(&sampDesc, &m_pSsPointClamp));

		sampDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
		CHECK_D3D(m_pDevice->CreateSamplerState(&sampDesc, &m_pSsBilinearClamp));
	
		sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		CHECK_D3D(m_pDevice->CreateSamplerState(&sampDesc, &m_pSsTrilinearRepeat));
	
		sampDesc.Filter = D3D11_FILTER_ANISOTROPIC;
		sampDesc.MaxAnisotropy = 16;
		CHECK_D3D(m_pDevice->CreateSamplerState(&sampDesc, &m_pSsTrilinearRepeatAniso));

		// PCF shadow comparison filter, with border color set to 1.0 so areas outside
		// the shadow map will be unshadowed
		sampDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
		sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
		sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
		sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
		sampDesc.MaxAnisotropy = 1;
		sampDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
		sampDesc.BorderColor[0] = 1.0f;
		sampDesc.BorderColor[1] = 1.0f;
		sampDesc.BorderColor[2] = 1.0f;
		sampDesc.BorderColor[3] = 1.0f;
		CHECK_D3D(m_pDevice->CreateSamplerState(&sampDesc, &m_pSsPCF));

		// Set up commonly used shaders
		CHECK_D3D(m_pDevice->CreateVertexShader(fullscreen_vs_bytecode, dim(fullscreen_vs_bytecode), nullptr, &m_pVsFullscreen));
		CHECK_D3D(m_pDevice->CreateVertexShader(rect_vs_bytecode, dim(rect_vs_bytecode), nullptr, &m_pVsRect));
		CHECK_D3D(m_pDevice->CreatePixelShader(copy_ps_bytecode, dim(copy_ps_bytecode), nullptr, &m_pPsCopy));

		// Init CB for blits and fullscreen passes
		m_cbBlit.Init(m_pDevice);
	}

	void D3D11Window::Shutdown()
	{
		LOG("Shutting down");

		if (m_hWnd)
		{
			DestroyWindow(m_hWnd);
			m_hWnd = nullptr;
		}
	}

	void D3D11Window::MainLoop(int nShowCmd)
	{
		// Show the window.  This sends the initial WM_SIZE message which results in
		// calling OnRender(); we don't want to do this until all initialization 
		// (including subclass init) is done, so it's here instead of in Init().
		ShowWindow(m_hWnd, nShowCmd);

		LOG("Main loop started");

		MSG msg;
		for (;;)
		{
			// Handle any messages
			while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}

			// Quit if the window has been destroyed
			if (!m_hWnd)
				break;

			// Render a new frame
			OnRender();
		}
	}

	static LRESULT CALLBACK StaticMsgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		D3D11Window * pWindow;

		if (message == WM_CREATE)
		{
			// On creation, stash pointer to the D3D11Window object in the window data
			CREATESTRUCT * pCreate = (CREATESTRUCT *)lParam;
			pWindow = (D3D11Window *)pCreate->lpCreateParams;
			SetWindowLongPtr(hWnd, GWLP_USERDATA, LONG_PTR(pWindow));
		}
		else
		{
			// Retrieve the D3D11Window object from the window data
			pWindow = (D3D11Window *)GetWindowLongPtr(hWnd, GWLP_USERDATA);

			// If it's not there yet (due to messages prior to WM_CREATE),
			// just fall back to DefWindowProc
			if (!pWindow)
				return DefWindowProc(hWnd, message, wParam, lParam);
		}

		// Pass the message to the D3D11Window object
		return pWindow->MsgProc(hWnd, message, wParam, lParam);
	}

	LRESULT D3D11Window::MsgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_CLOSE:
			Shutdown();
			return 0;

		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;

		case WM_SIZE:
			{
				int2 dimsNew = { LOWORD(lParam), HIWORD(lParam) };
				if (all(dimsNew > 0) && any(dimsNew != m_dims))
				{
					OnResize(dimsNew);
					OnRender();
				}
				return 0;
			}

		case WM_SIZING:
			{
				RECT clientRect;
				GetClientRect(hWnd, &clientRect);
				int2 dimsNew = { clientRect.right - clientRect.left, clientRect.bottom - clientRect.top };
				if (all(dimsNew > 0) && any(dimsNew != m_dims))
				{
					OnResize(dimsNew);
					OnRender();
				}
				return 0;
			}

		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
	}

	void D3D11Window::OnResize(int2_arg dimsNew)
	{
		LOG("Resizing swap chain to %d x %d", dimsNew.x, dimsNew.y);

		m_dims = dimsNew;

		// Have to release old render target views before swap chain can be resized
		m_pTexBackBuffer.release();
		m_pRtvSRGB.release();
		m_pRtvRaw.release();
		m_pTexDepth.release();
		m_pDsv.release();
		m_pSrvDepth.release();

		// Resize the swap chain to fit the window again
		ASSERT_ERR(m_pSwapChain);
		CHECK_D3D(m_pSwapChain->ResizeBuffers(0, dimsNew.x, dimsNew.y, DXGI_FORMAT_UNKNOWN, 0));

		{
			// Retrieve the back buffer
			CHECK_D3D(m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&m_pTexBackBuffer));

			// Create render target views in sRGB and raw formats
			D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = 
			{
				DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
				D3D11_RTV_DIMENSION_TEXTURE2D,
			};
			CHECK_D3D(m_pDevice->CreateRenderTargetView(m_pTexBackBuffer, &rtvDesc, &m_pRtvSRGB));
			rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			CHECK_D3D(m_pDevice->CreateRenderTargetView(m_pTexBackBuffer, &rtvDesc, &m_pRtvRaw));
		}

		if (m_hasDepthBuffer)
		{
			// Create depth buffer and its views

			D3D11_TEXTURE2D_DESC texDesc =
			{
				dimsNew.x, dimsNew.y, 1, 1,
				DXGI_FORMAT_R32_TYPELESS,
				{ 1, 0 },
				D3D11_USAGE_DEFAULT,
				D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE,
			};
			CHECK_D3D(m_pDevice->CreateTexture2D(&texDesc, nullptr, &m_pTexDepth));

			D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc =
			{
				DXGI_FORMAT_D32_FLOAT,
				D3D11_DSV_DIMENSION_TEXTURE2D,
			};
			CHECK_D3D(m_pDevice->CreateDepthStencilView(m_pTexDepth, &dsvDesc, &m_pDsv));

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc =
			{
				DXGI_FORMAT_R32_FLOAT,
				D3D11_SRV_DIMENSION_TEXTURE2D,
			};
			srvDesc.Texture2D.MipLevels = 1;
			CHECK_D3D(m_pDevice->CreateShaderResourceView(m_pTexDepth, &srvDesc, &m_pSrvDepth));
		}
	}



	// Utility methods

	void D3D11Window::BindSRGBBackBuffer(ID3D11DeviceContext * pCtx)
	{
		pCtx->OMSetRenderTargets(1, &m_pRtvSRGB, m_pDsv);
		D3D11_VIEWPORT viewport = { 0.0f, 0.0f, float(m_dims.x), float(m_dims.y), 0.0f, 1.0f, };
		pCtx->RSSetViewports(1, &viewport);
	}

	void D3D11Window::BindRawBackBuffer(ID3D11DeviceContext * pCtx)
	{
		pCtx->OMSetRenderTargets(1, &m_pRtvRaw, m_pDsv);
		D3D11_VIEWPORT viewport = { 0.0f, 0.0f, float(m_dims.x), float(m_dims.y), 0.0f, 1.0f, };
		pCtx->RSSetViewports(1, &viewport);
	}

	void D3D11Window::SetViewport(ID3D11DeviceContext * pCtx, box2_arg viewport)
	{
		D3D11_VIEWPORT vp =
		{
			viewport.m_mins.x, viewport.m_mins.y,
			viewport.diagonal().x, viewport.diagonal().y,
			0.0f, 1.0f,
		};
		pCtx->RSSetViewports(1, &vp);
	}

	void D3D11Window::SetViewport(ID3D11DeviceContext * pCtx, box3_arg viewport)
	{
		D3D11_VIEWPORT vp =
		{
			viewport.m_mins.x, viewport.m_mins.y,
			viewport.diagonal().x, viewport.diagonal().y,
			viewport.m_mins.z, viewport.m_maxs.z,
		};
		pCtx->RSSetViewports(1, &vp);
	}

	void D3D11Window::DrawFullscreenPass(
		ID3D11DeviceContext * pCtx,
		box2_arg boxSrc /*= makebox2(0, 0, 1, 1)*/)
	{
		CBBlit cbBlit =
		{
			boxSrc,
			makebox2(0, 0, 1, 1),
		};
		m_cbBlit.Update(pCtx, &cbBlit);

		pCtx->IASetInputLayout(nullptr);
		pCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCtx->VSSetShader(m_pVsFullscreen, nullptr, 0);
		pCtx->VSSetConstantBuffers(0, 1, &m_cbBlit.m_pBuf);
		pCtx->Draw(3, 0);
	}

	void D3D11Window::BlitFullscreen(
		ID3D11DeviceContext * pCtx,
		ID3D11ShaderResourceView * pSrvSrc,
		ID3D11SamplerState * pSampSrc,
		box2_arg boxSrc /*= makebox2(0, 0, 1, 1)*/)
	{
		CBBlit cbBlit =
		{
			boxSrc,
			makebox2(0, 0, 1, 1),
		};
		m_cbBlit.Update(pCtx, &cbBlit);

		pCtx->IASetInputLayout(nullptr);
		pCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCtx->VSSetShader(m_pVsFullscreen, nullptr, 0);
		pCtx->VSSetConstantBuffers(0, 1, &m_cbBlit.m_pBuf);
		pCtx->PSSetShader(m_pPsCopy, nullptr, 0);
		pCtx->PSSetShaderResources(0, 1, &pSrvSrc);
		pCtx->PSSetSamplers(0, 1, &pSampSrc);
		pCtx->Draw(3, 0);
	}

	void D3D11Window::Blit(
		ID3D11DeviceContext * pCtx,
		ID3D11ShaderResourceView * pSrvSrc,
		ID3D11SamplerState * pSampSrc,
		box2_arg boxSrc,
		box2_arg boxDst)
	{
		CBBlit cbBlit =
		{
			boxSrc,
			boxDst,
		};
		m_cbBlit.Update(pCtx, &cbBlit);

		pCtx->IASetInputLayout(nullptr);
		pCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		pCtx->VSSetShader(m_pVsRect, nullptr, 0);
		pCtx->VSSetConstantBuffers(0, 1, &m_cbBlit.m_pBuf);
		pCtx->PSSetShader(m_pPsCopy, nullptr, 0);
		pCtx->PSSetShaderResources(0, 1, &pSrvSrc);
		pCtx->PSSetSamplers(0, 1, &pSampSrc);
		pCtx->Draw(6, 0);
	}
}



// Magic incantation to make the app run on the NV GPU on Optimus laptops
extern "C"
{
    _declspec(dllexport) DWORD NvOptimusEnablement = 1;
}
