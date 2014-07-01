#include "framework.h"
#include <d3dx11.h>

namespace Framework
{
	// Texture2D implementation

	Texture2D::Texture2D()
	:	m_pTex(),
		m_pSrv(),
		m_pUav(),
		m_dims(makeint2(0)),
		m_mipLevels(0),
		m_format(DXGI_FORMAT_UNKNOWN)
	{
	}

	void Texture2D::Init(
		ID3D11Device * pDevice,
		int2_arg dims,
		DXGI_FORMAT format,
		int flags /* = TEXFLAG_Default */)
	{
		ASSERT_ERR(pDevice);

		D3D11_TEXTURE2D_DESC texDesc =
		{
			dims.x, dims.y,
			(flags & TEXFLAG_Mipmaps) ? CalculateMipCount(dims) : 1,
			1,
			format,
			{ 1, 0 },
			D3D11_USAGE_DEFAULT,
			D3D11_BIND_SHADER_RESOURCE,
			0, 0,
		};
		if (flags & TEXFLAG_EnableUAV)
		{
			texDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
		}
		CHECK_D3D(pDevice->CreateTexture2D(&texDesc, nullptr, &m_pTex));

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc =
		{
			format,
			D3D11_SRV_DIMENSION_TEXTURE2D,
		};
		srvDesc.Texture2D.MipLevels = texDesc.MipLevels;
		CHECK_D3D(pDevice->CreateShaderResourceView(m_pTex, &srvDesc, &m_pSrv));

		if (flags & TEXFLAG_EnableUAV)
		{
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = { format, D3D11_UAV_DIMENSION_TEXTURE2D, };
			CHECK_D3D(pDevice->CreateUnorderedAccessView(m_pTex, &uavDesc, &m_pUav));
		}

		m_dims = dims;
		m_mipLevels = texDesc.MipLevels;
		m_format = format;
	}

	void Texture2D::Release()
	{
		m_pTex.release();
		m_pSrv.release();
		m_pUav.release();
		m_dims = makeint2(0);
		m_mipLevels = 0;
		m_format = DXGI_FORMAT_UNKNOWN;
	}

	void Texture2D::Readback(
		ID3D11DeviceContext * pCtx,
		int level,
		void * pDataOut)
	{
		ASSERT_ERR(m_pTex);
		ASSERT_ERR(pCtx);
		ASSERT_ERR(level >= 0 && level < m_mipLevels);
		ASSERT_ERR(pDataOut);

		comptr<ID3D11Device> pDevice;
		pCtx->GetDevice(&pDevice);

		int2 mipDims = CalculateMipDims(m_dims, level);

		// Create a staging resource
		D3D11_TEXTURE2D_DESC texDesc =
		{
			mipDims.x, mipDims.y, 1, 1,
			m_format,
			{ 1, 0 },
			D3D11_USAGE_STAGING,
			0,
			D3D11_CPU_ACCESS_READ,
			0,
		};
		comptr<ID3D11Texture2D> pTexStaging;
		pDevice->CreateTexture2D(&texDesc, nullptr, &pTexStaging);

		// Copy the data to the staging resource
		pCtx->CopySubresourceRegion(pTexStaging, 0, 0, 0, 0, m_pTex, level, nullptr);

		// Map the staging resource
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		CHECK_D3D(pCtx->Map(pTexStaging, 0, D3D11_MAP_READ, 0, &mapped));

		// Copy the data out row by row, in case the pitch is different
		int rowSize = mipDims.x * BitsPerPixel(m_format) / 8;
		ASSERT_ERR(mapped.RowPitch >= UINT(rowSize));
		for (int y = 0; y < mipDims.y; ++y)
		{
			memcpy(
				advanceBytes(pDataOut, y * rowSize),
				advanceBytes(mapped.pData, y * mapped.RowPitch),
				rowSize);
		}

		pCtx->Unmap(pTexStaging, 0);
	}



	// TextureCube implementation

	TextureCube::TextureCube()
	:	m_pTex(),
		m_pSrv(),
		m_pUav(),
		m_cubeSize(0),
		m_mipLevels(0),
		m_format(DXGI_FORMAT_UNKNOWN)
	{
	}

	void TextureCube::Init(
		ID3D11Device * pDevice,
		int cubeSize,
		DXGI_FORMAT format,
		int flags /* = TEXFLAG_Default */)
	{
		ASSERT_ERR(pDevice);

		D3D11_TEXTURE2D_DESC texDesc =
		{
			cubeSize, cubeSize,
			(flags & TEXFLAG_Mipmaps) ? CalculateMipCount(cubeSize) : 1,
			6,
			format,
			{ 1, 0 },
			D3D11_USAGE_DEFAULT,
			D3D11_BIND_SHADER_RESOURCE,
			0,
			D3D11_RESOURCE_MISC_TEXTURECUBE,
		};
		if (flags & TEXFLAG_EnableUAV)
		{
			texDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
		}
		CHECK_D3D(pDevice->CreateTexture2D(&texDesc, nullptr, &m_pTex));

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc =
		{
			format,
			D3D11_SRV_DIMENSION_TEXTURECUBE,
		};
		srvDesc.TextureCube.MipLevels = texDesc.MipLevels;
		CHECK_D3D(pDevice->CreateShaderResourceView(m_pTex, &srvDesc, &m_pSrv));

		if (flags & TEXFLAG_EnableUAV)
		{
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = { format, D3D11_UAV_DIMENSION_TEXTURE2DARRAY, };
			uavDesc.Texture2DArray.ArraySize = 6;
			CHECK_D3D(pDevice->CreateUnorderedAccessView(m_pTex, &uavDesc, &m_pUav));
		}

		m_cubeSize = cubeSize;
		m_mipLevels = texDesc.MipLevels;
		m_format = format;
	}

	void TextureCube::Release()
	{
		m_pTex.release();
		m_pSrv.release();
		m_pUav.release();
		m_cubeSize = 0;
		m_mipLevels = 0;
		m_format = DXGI_FORMAT_UNKNOWN;
	}

	void TextureCube::Readback(
		ID3D11DeviceContext * pCtx,
		int face,
		int level,
		void * pDataOut)
	{
		ASSERT_ERR(m_pTex);
		ASSERT_ERR(pCtx);
		ASSERT_ERR(face >= 0 && face < 6);
		ASSERT_ERR(level >= 0 && level < m_mipLevels);
		ASSERT_ERR(pDataOut);

		comptr<ID3D11Device> pDevice;
		pCtx->GetDevice(&pDevice);

		int mipDim = CalculateMipDims(m_cubeSize, level);

		// Create a staging resource
		D3D11_TEXTURE2D_DESC texDesc =
		{
			mipDim, mipDim, 1, 1,
			m_format,
			{ 1, 0 },
			D3D11_USAGE_STAGING,
			0,
			D3D11_CPU_ACCESS_READ,
			0,
		};
		comptr<ID3D11Texture2D> pTexStaging;
		pDevice->CreateTexture2D(&texDesc, nullptr, &pTexStaging);

		// Copy the data to the staging resource
		pCtx->CopySubresourceRegion(pTexStaging, 0, 0, 0, 0, m_pTex, face * m_mipLevels + level, nullptr);

		// Map the staging resource and copy the data out
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		CHECK_D3D(pCtx->Map(pTexStaging, 0, D3D11_MAP_READ, 0, &mapped));

		// Copy the data out row by row, in case the pitch is different
		int rowSize = mipDim * BitsPerPixel(m_format) / 8;
		ASSERT_ERR(mapped.RowPitch >= UINT(rowSize));
		for (int y = 0; y < mipDim; ++y)
		{
			memcpy(
				advanceBytes(pDataOut, y * rowSize),
				advanceBytes(mapped.pData, y * mapped.RowPitch),
				rowSize);
		}

		pCtx->Unmap(pTexStaging, 0);
	}



	// Texture3D implementation

	Texture3D::Texture3D()
	:	m_pTex(),
		m_pSrv(),
		m_pUav(),
		m_dims(makeint3(0)),
		m_mipLevels(0),
		m_format(DXGI_FORMAT_UNKNOWN)
	{
	}

	void Texture3D::Init(
		ID3D11Device * pDevice,
		int3_arg dims,
		DXGI_FORMAT format,
		int flags /* = TEXFLAG_Default */)
	{
		ASSERT_ERR(pDevice);

		D3D11_TEXTURE3D_DESC texDesc =
		{
			dims.x, dims.y, dims.z,
			(flags & TEXFLAG_Mipmaps) ? CalculateMipCount(dims) : 1,
			format,
			D3D11_USAGE_DEFAULT,
			D3D11_BIND_SHADER_RESOURCE,
			0, 0,
		};
		if (flags & TEXFLAG_EnableUAV)
		{
			texDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
		}
		CHECK_D3D(pDevice->CreateTexture3D(&texDesc, nullptr, &m_pTex));

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc =
		{
			format,
			D3D11_SRV_DIMENSION_TEXTURE3D,
		};
		srvDesc.Texture3D.MipLevels = texDesc.MipLevels;
		CHECK_D3D(pDevice->CreateShaderResourceView(m_pTex, &srvDesc, &m_pSrv));

		if (flags & TEXFLAG_EnableUAV)
		{
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = { format, D3D11_UAV_DIMENSION_TEXTURE3D, };
			uavDesc.Texture3D.WSize = dims.z;
			CHECK_D3D(pDevice->CreateUnorderedAccessView(m_pTex, &uavDesc, &m_pUav));
		}

		m_dims = dims;
		m_mipLevels = texDesc.MipLevels;
		m_format = format;
	}

	void Texture3D::Release()
	{
		m_pTex.release();
		m_pSrv.release();
		m_pUav.release();
		m_dims = makeint3(0);
		m_mipLevels = 0;
		m_format = DXGI_FORMAT_UNKNOWN;
	}

	void Texture3D::Readback(
		ID3D11DeviceContext * pCtx,
		int level,
		void * pDataOut)
	{
		ASSERT_ERR(m_pTex);
		ASSERT_ERR(pCtx);
		ASSERT_ERR(level >= 0 && level < m_mipLevels);
		ASSERT_ERR(pDataOut);

		comptr<ID3D11Device> pDevice;
		pCtx->GetDevice(&pDevice);

		int3 mipDims = CalculateMipDims(m_dims, level);

		// Create a staging resource
		D3D11_TEXTURE3D_DESC texDesc =
		{
			mipDims.x, mipDims.y, mipDims.z, 1,
			m_format,
			D3D11_USAGE_STAGING,
			0,
			D3D11_CPU_ACCESS_READ,
			0,
		};
		comptr<ID3D11Texture3D> pTexStaging;
		pDevice->CreateTexture3D(&texDesc, nullptr, &pTexStaging);

		// Copy the data to the staging resource
		pCtx->CopySubresourceRegion(pTexStaging, 0, 0, 0, 0, m_pTex, level, nullptr);

		// Map the staging resource and copy the data out
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		CHECK_D3D(pCtx->Map(pTexStaging, 0, D3D11_MAP_READ, 0, &mapped));

		// Copy the data out slice by slice and row by row, in case the pitches are different
		int rowSize = mipDims.x * BitsPerPixel(m_format) / 8;
		int sliceSize = mipDims.y * rowSize;
		ASSERT_ERR(mapped.RowPitch >= UINT(rowSize));
		ASSERT_ERR(mapped.DepthPitch >= UINT(sliceSize));
		for (int z = 0; z < mipDims.z; ++z)
		{
			for (int y = 0; y < mipDims.y; ++y)
			{
				memcpy(
					advanceBytes(pDataOut, z * sliceSize + y * rowSize),
					advanceBytes(mapped.pData, z * mapped.DepthPitch + y * mapped.RowPitch),
					rowSize);
			}
		}

		pCtx->Unmap(pTexStaging, 0);
	}



	// Texture loading - helper functions

	bool LoadTexture2D(
		ID3D11Device * pDevice,
		const char * path,
		Texture2D * pTexOut,
		int flags /*= TEXLOADFLAG_Default*/)
	{
		ASSERT_ERR(pDevice);
		ASSERT_ERR(path);
		ASSERT_ERR(pTexOut);

		bool mipmap = (flags & TEXLOADFLAG_Mipmap) != 0;
		bool SRGB = (flags & TEXLOADFLAG_SRGB) != 0;
		bool HDR = (flags & TEXLOADFLAG_HDR) != 0;

		// HDR bitmaps are always in linear color space
		if (HDR)
			ASSERT_WARN_MSG(!SRGB, "HDR bitmaps cannot be in SRGB space");

		// Load the texture, generating mipmaps if requested

		D3DX11_IMAGE_LOAD_INFO imgLoadInfo;
		imgLoadInfo.MipLevels = mipmap ? D3DX11_DEFAULT : 1;
		imgLoadInfo.Usage = D3D11_USAGE_IMMUTABLE;
		imgLoadInfo.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		imgLoadInfo.Format = HDR ? DXGI_FORMAT_R16G16B16A16_FLOAT :
								(SRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM);
		imgLoadInfo.Filter = D3DX11_FILTER_TRIANGLE | (SRGB ? D3DX11_FILTER_SRGB : 0);
		imgLoadInfo.MipFilter = D3DX11_FILTER_TRIANGLE;

		comptr<ID3D11ShaderResourceView> pSrv;
		if (FAILED(D3DX11CreateShaderResourceViewFromFile(
								pDevice,
								path,
								&imgLoadInfo,
								nullptr,		// no thread pump
								&pSrv,
								nullptr)))		// no async return value
		{
			WARN("Couldn't load texture %s", path);
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		pSrv->GetDesc(&srvDesc);
		if (srvDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D)
		{
			WARN("Loaded texture %s, but it's not a 2D texture - srvDesc.ViewDimension = %d", path, srvDesc.ViewDimension);
			return false;
		}

		comptr<ID3D11Resource> pRes;
		pSrv->GetResource(&pRes);
		comptr<ID3D11Texture2D> pTex;
		if (FAILED(pRes->QueryInterface(IID_ID3D11Texture2D, (void **)&pTex)))
		{
			WARN("Loaded texture %s, but couldn't get ID3D11Texture2D interface", path);
			return false;
		}

		D3D11_TEXTURE2D_DESC texDesc;
		pTex->GetDesc(&texDesc);

		LOG(
			"Loaded 2D texture %s - %dx%d, format %s, %d mip levels",
			path, texDesc.Width, texDesc.Height, NameOfFormat(texDesc.Format), texDesc.MipLevels);

		pTexOut->m_pTex = pTex;
		pTexOut->m_pSrv = pSrv;
		pTexOut->m_dims = makeint2(texDesc.Width, texDesc.Height);
		pTexOut->m_mipLevels = texDesc.MipLevels;
		pTexOut->m_format = texDesc.Format;

		return true;
	}

	bool LoadTextureCube(
		ID3D11Device * pDevice,
		const char * path,
		TextureCube * pTexOut,
		int flags /*= TEXLOADFLAG_Default*/)
	{
		ASSERT_ERR(pDevice);
		ASSERT_ERR(path);
		ASSERT_ERR(pTexOut);

		bool mipmap = (flags & TEXLOADFLAG_Mipmap) != 0;
		bool SRGB = (flags & TEXLOADFLAG_SRGB) != 0;
		bool HDR = (flags & TEXLOADFLAG_HDR) != 0;

		// HDR bitmaps are always in linear color space
		if (HDR)
			ASSERT_WARN_MSG(!SRGB, "HDR bitmaps cannot be in SRGB space");

		// Load the texture, generating mipmaps if requested

		D3DX11_IMAGE_LOAD_INFO imgLoadInfo;
		imgLoadInfo.MipLevels = mipmap ? D3DX11_DEFAULT : 1;
		imgLoadInfo.Usage = D3D11_USAGE_IMMUTABLE;
		imgLoadInfo.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		imgLoadInfo.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
		imgLoadInfo.Format = HDR ? DXGI_FORMAT_R16G16B16A16_FLOAT :
								(SRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM);
		imgLoadInfo.Filter = D3DX11_FILTER_TRIANGLE | (SRGB ? D3DX11_FILTER_SRGB : 0);
		imgLoadInfo.MipFilter = D3DX11_FILTER_TRIANGLE;

		comptr<ID3D11ShaderResourceView> pSrv;
		if (FAILED(D3DX11CreateShaderResourceViewFromFile(
								pDevice,
								path,
								&imgLoadInfo,
								nullptr,		// no thread pump
								&pSrv,
								nullptr)))		// no async return value
		{
			WARN("Couldn't load texture %s", path);
			return false;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		pSrv->GetDesc(&srvDesc);
		if (srvDesc.ViewDimension != D3D11_SRV_DIMENSION_TEXTURECUBE)
		{
			WARN("Loaded texture %s, but it's not a cubemap - srvDesc.ViewDimension = %d", path, srvDesc.ViewDimension);
			return false;
		}

		comptr<ID3D11Resource> pRes;
		pSrv->GetResource(&pRes);
		comptr<ID3D11Texture2D> pTex;
		if (FAILED(pRes->QueryInterface(IID_ID3D11Texture2D, (void **)&pTex)))
		{
			WARN("Loaded texture %s, but couldn't get ID3D11Texture2D interface", path);
			return false;
		}

		D3D11_TEXTURE2D_DESC texDesc;
		pTex->GetDesc(&texDesc);
		ASSERT_ERR(texDesc.Width == texDesc.Height);
		ASSERT_ERR(texDesc.ArraySize == 6);
		ASSERT_ERR(texDesc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE);

		LOG(
			"Loaded cubemap %s - cube size %d, format %s, %d mip levels",
			path, texDesc.Width, NameOfFormat(texDesc.Format), texDesc.MipLevels);

		pTexOut->m_pTex = pTex;
		pTexOut->m_pSrv = pSrv;
		pTexOut->m_cubeSize = texDesc.Width;
		pTexOut->m_mipLevels = texDesc.MipLevels;
		pTexOut->m_format = texDesc.Format;

		return true;
	}
	


	// Texture creation - helper functions

	void CreateTexture1x1(
		ID3D11Device * pDevice,
		rgba_arg color,
		Texture2D * pTexOut,
		DXGI_FORMAT format /*= DXGI_FORMAT_R8G8B8A8_UNORM_SRGB*/)
	{
		ASSERT_ERR(pDevice);
		ASSERT_ERR(pTexOut);

		// Convert floats to 8-bit format
		byte4 colorBytes = makebyte4(round(255.0f * saturate(color)));

		D3D11_TEXTURE2D_DESC texDesc = 
		{
			1, 1, 1, 1,
			format,
			{ 1, 0 },
			D3D11_USAGE_DEFAULT,
			D3D11_BIND_SHADER_RESOURCE,
			0, 0,
		};

		D3D11_SUBRESOURCE_DATA initialData = { colorBytes, sizeof(colorBytes), };
		comptr<ID3D11Texture2D> pTex;
		CHECK_D3D(pDevice->CreateTexture2D(&texDesc, &initialData, &pTex));

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc =
		{
			texDesc.Format,
			D3D11_SRV_DIMENSION_TEXTURE2D,
		};
		srvDesc.Texture2D.MipLevels = 1;

		ID3D11ShaderResourceView * pSrv = nullptr;
		CHECK_D3D(pDevice->CreateShaderResourceView(pTex, &srvDesc, &pSrv));

		pTexOut->m_pTex = pTex;
		pTexOut->m_pSrv = pSrv;
		pTexOut->m_dims = makeint2(1, 1);
		pTexOut->m_mipLevels = 1;
		pTexOut->m_format = format;
	}

	void CreateTextureCube1x1(
		ID3D11Device * pDevice,
		rgba_arg color,
		TextureCube * pTexOut,
		DXGI_FORMAT format /*= DXGI_FORMAT_R8G8B8A8_UNORM_SRGB*/)
	{
		ASSERT_ERR(pDevice);
		ASSERT_ERR(pTexOut);

		// Convert floats to 8-bit format
		byte4 colorBytes[6] = { makebyte4(round(255.0f * saturate(color))) };
		for (int i = 1; i < dim(colorBytes); ++i)
			colorBytes[i] = colorBytes[0];

		D3D11_TEXTURE2D_DESC texDesc = 
		{
			1, 1, 1, 6,
			format,
			{ 1, 0 },
			D3D11_USAGE_DEFAULT,
			D3D11_BIND_SHADER_RESOURCE,
			0,
			D3D11_RESOURCE_MISC_TEXTURECUBE,
		};

		D3D11_SUBRESOURCE_DATA initialData = { colorBytes, sizeof(colorBytes), };
		comptr<ID3D11Texture2D> pTex;
		CHECK_D3D(pDevice->CreateTexture2D(&texDesc, &initialData, &pTex));

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc =
		{
			texDesc.Format,
			D3D11_SRV_DIMENSION_TEXTURECUBE,
		};
		srvDesc.TextureCube.MipLevels = 1;

		ID3D11ShaderResourceView * pSrv = nullptr;
		CHECK_D3D(pDevice->CreateShaderResourceView(pTex, &srvDesc, &pSrv));

		pTexOut->m_pTex = pTex;
		pTexOut->m_pSrv = pSrv;
		pTexOut->m_cubeSize = 1;
		pTexOut->m_mipLevels = 1;
		pTexOut->m_format = format;
	}

	void CreateTexture2DFromMemory(
		ID3D11Device * pDevice,
		int2_arg dims,
		DXGI_FORMAT format,
		const void * pPixels,
		Texture2D * pTexOut)
	{
		ASSERT_ERR(pDevice);
		ASSERT_ERR(pPixels);
		ASSERT_ERR(pTexOut);

		D3D11_TEXTURE2D_DESC texDesc =
		{
			dims.x, dims.y, 1, 1,
			format,
			{ 1, 0 },
			D3D11_USAGE_IMMUTABLE,
			D3D11_BIND_SHADER_RESOURCE,
			0, 0,
		};

		D3D11_SUBRESOURCE_DATA initialData = { pPixels, dims.x * BitsPerPixel(format) / 8 };
		comptr<ID3D11Texture2D> pTex;
		CHECK_D3D(pDevice->CreateTexture2D(&texDesc, &initialData, &pTex));

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc =
		{
			format,
			D3D11_SRV_DIMENSION_TEXTURE2D,
		};
		srvDesc.Texture2D.MipLevels = 1;

		ID3D11ShaderResourceView * pSrv = nullptr;
		CHECK_D3D(pDevice->CreateShaderResourceView(pTex, &srvDesc, &pSrv));

		pTexOut->m_pTex = pTex;
		pTexOut->m_pSrv = pSrv;
		pTexOut->m_dims = dims;
		pTexOut->m_mipLevels = 1;
		pTexOut->m_format = format;
	}



	const char * NameOfFormat(DXGI_FORMAT format)
	{
		static const char * s_names[] =
		{
			"UNKNOWN",
			"R32G32B32A32_TYPELESS",
			"R32G32B32A32_FLOAT",
			"R32G32B32A32_UINT",
			"R32G32B32A32_SINT",
			"R32G32B32_TYPELESS",
			"R32G32B32_FLOAT",
			"R32G32B32_UINT",
			"R32G32B32_SINT",
			"R16G16B16A16_TYPELESS",
			"R16G16B16A16_FLOAT",
			"R16G16B16A16_UNORM",
			"R16G16B16A16_UINT",
			"R16G16B16A16_SNORM",
			"R16G16B16A16_SINT",
			"R32G32_TYPELESS",
			"R32G32_FLOAT",
			"R32G32_UINT",
			"R32G32_SINT",
			"R32G8X24_TYPELESS",
			"D32_FLOAT_S8X24_UINT",
			"R32_FLOAT_X8X24_TYPELESS",
			"X32_TYPELESS_G8X24_UINT",
			"R10G10B10A2_TYPELESS",
			"R10G10B10A2_UNORM",
			"R10G10B10A2_UINT",
			"R11G11B10_FLOAT",
			"R8G8B8A8_TYPELESS",
			"R8G8B8A8_UNORM",
			"R8G8B8A8_UNORM_SRGB",
			"R8G8B8A8_UINT",
			"R8G8B8A8_SNORM",
			"R8G8B8A8_SINT",
			"R16G16_TYPELESS",
			"R16G16_FLOAT",
			"R16G16_UNORM",
			"R16G16_UINT",
			"R16G16_SNORM",
			"R16G16_SINT",
			"R32_TYPELESS",
			"D32_FLOAT",
			"R32_FLOAT",
			"R32_UINT",
			"R32_SINT",
			"R24G8_TYPELESS",
			"D24_UNORM_S8_UINT",
			"R24_UNORM_X8_TYPELESS",
			"X24_TYPELESS_G8_UINT",
			"R8G8_TYPELESS",
			"R8G8_UNORM",
			"R8G8_UINT",
			"R8G8_SNORM",
			"R8G8_SINT",
			"R16_TYPELESS",
			"R16_FLOAT",
			"D16_UNORM",
			"R16_UNORM",
			"R16_UINT",
			"R16_SNORM",
			"R16_SINT",
			"R8_TYPELESS",
			"R8_UNORM",
			"R8_UINT",
			"R8_SNORM",
			"R8_SINT",
			"A8_UNORM",
			"R1_UNORM",
			"R9G9B9E5_SHAREDEXP",
			"R8G8_B8G8_UNORM",
			"G8R8_G8B8_UNORM",
			"BC1_TYPELESS",
			"BC1_UNORM",
			"BC1_UNORM_SRGB",
			"BC2_TYPELESS",
			"BC2_UNORM",
			"BC2_UNORM_SRGB",
			"BC3_TYPELESS",
			"BC3_UNORM",
			"BC3_UNORM_SRGB",
			"BC4_TYPELESS",
			"BC4_UNORM",
			"BC4_SNORM",
			"BC5_TYPELESS",
			"BC5_UNORM",
			"BC5_SNORM",
			"B5G6R5_UNORM",
			"B5G5R5A1_UNORM",
			"B8G8R8A8_UNORM",
			"B8G8R8X8_UNORM",
			"R10G10B10_XR_BIAS_A2_UNORM",
			"B8G8R8A8_TYPELESS",
			"B8G8R8A8_UNORM_SRGB",
			"B8G8R8X8_TYPELESS",
			"B8G8R8X8_UNORM_SRGB",
			"BC6H_TYPELESS",
			"BC6H_UF16",
			"BC6H_SF16",
			"BC7_TYPELESS",
			"BC7_UNORM",
			"BC7_UNORM_SRGB",
			"AYUV",
			"Y410",
			"Y416",
			"NV12",
			"P010",
			"P016",
			"420_OPAQUE",
			"YUY2",
			"Y210",
			"Y216",
			"NV11",
			"AI44",
			"IA44",
			"P8",
			"A8P8",
			"B4G4R4A4_UNORM",
		};

		if (uint(format) >= dim(s_names))
		{
			WARN("Unexpected DXGI_FORMAT %d", format);
			return "UNKNOWN";
		}

		return s_names[format];
	}

	int BitsPerPixel(DXGI_FORMAT format)
	{
		static const int s_bitsPerPixel[] =
		{
			0,			// UNKNOWN
			128,		// R32G32B32A32_TYPELESS
			128,		// R32G32B32A32_FLOAT
			128,		// R32G32B32A32_UINT
			128,		// R32G32B32A32_SINT
			96,			// R32G32B32_TYPELESS
			96,			// R32G32B32_FLOAT
			96,			// R32G32B32_UINT
			96,			// R32G32B32_SINT
			64,			// R16G16B16A16_TYPELESS
			64,			// R16G16B16A16_FLOAT
			64,			// R16G16B16A16_UNORM
			64,			// R16G16B16A16_UINT
			64,			// R16G16B16A16_SNORM
			64,			// R16G16B16A16_SINT
			64,			// R32G32_TYPELESS
			64,			// R32G32_FLOAT
			64,			// R32G32_UINT
			64,			// R32G32_SINT
			64,			// R32G8X24_TYPELESS
			64,			// D32_FLOAT_S8X24_UINT
			64,			// R32_FLOAT_X8X24_TYPELESS
			64,			// X32_TYPELESS_G8X24_UINT
			32,			// R10G10B10A2_TYPELESS
			32,			// R10G10B10A2_UNORM
			32,			// R10G10B10A2_UINT
			32,			// R11G11B10_FLOAT
			32,			// R8G8B8A8_TYPELESS
			32,			// R8G8B8A8_UNORM
			32,			// R8G8B8A8_UNORM_SRGB
			32,			// R8G8B8A8_UINT
			32,			// R8G8B8A8_SNORM
			32,			// R8G8B8A8_SINT
			32,			// R16G16_TYPELESS
			32,			// R16G16_FLOAT
			32,			// R16G16_UNORM
			32,			// R16G16_UINT
			32,			// R16G16_SNORM
			32,			// R16G16_SINT
			32,			// R32_TYPELESS
			32,			// D32_FLOAT
			32,			// R32_FLOAT
			32,			// R32_UINT
			32,			// R32_SINT
			32,			// R24G8_TYPELESS
			32,			// D24_UNORM_S8_UINT
			32,			// R24_UNORM_X8_TYPELESS
			32,			// X24_TYPELESS_G8_UINT
			16,			// R8G8_TYPELESS
			16,			// R8G8_UNORM
			16,			// R8G8_UINT
			16,			// R8G8_SNORM
			16,			// R8G8_SINT
			16,			// R16_TYPELESS
			16,			// R16_FLOAT
			16,			// D16_UNORM
			16,			// R16_UNORM
			16,			// R16_UINT
			16,			// R16_SNORM
			16,			// R16_SINT
			8,			// R8_TYPELESS
			8,			// R8_UNORM
			8,			// R8_UINT
			8,			// R8_SNORM
			8,			// R8_SINT
			8,			// A8_UNORM
			1,			// R1_UNORM
			32,			// R9G9B9E5_SHAREDEXP
			16,			// R8G8_B8G8_UNORM
			16,			// G8R8_G8B8_UNORM
			4,			// BC1_TYPELESS
			4,			// BC1_UNORM
			4,			// BC1_UNORM_SRGB
			8,			// BC2_TYPELESS
			8,			// BC2_UNORM
			8,			// BC2_UNORM_SRGB
			8,			// BC3_TYPELESS
			8,			// BC3_UNORM
			8,			// BC3_UNORM_SRGB
			4,			// BC4_TYPELESS
			4,			// BC4_UNORM
			4,			// BC4_SNORM
			8,			// BC5_TYPELESS
			8,			// BC5_UNORM
			8,			// BC5_SNORM
			16,			// B5G6R5_UNORM
			16,			// B5G5R5A1_UNORM
			32,			// B8G8R8A8_UNORM
			32,			// B8G8R8X8_UNORM
			32,			// R10G10B10_XR_BIAS_A2_UNORM
			32,			// B8G8R8A8_TYPELESS
			32,			// B8G8R8A8_UNORM_SRGB
			32,			// B8G8R8X8_TYPELESS
			32,			// B8G8R8X8_UNORM_SRGB
			8,			// BC6H_TYPELESS
			8,			// BC6H_UF16
			8,			// BC6H_SF16
			8,			// BC7_TYPELESS
			8,			// BC7_UNORM
			8,			// BC7_UNORM_SRGB
			
			// NOTE: I don't know what are the bit depths for the video formats;
			// the MS docs don't seem to describe them in any detail.

			0,			// AYUV
			0,			// Y410
			0,			// Y416
			0,			// NV12
			0,			// P010
			0,			// P016
			0,			// 420_OPAQUE
			0,			// YUY2
			0,			// Y210
			0,			// Y216
			0,			// NV11
			0,			// AI44
			0,			// IA44

			8,			// P8
			16,			// A8P8
			16,			// B4G4R4A4_UNORM
		};

		if (uint(format) >= dim(s_bitsPerPixel))
		{
			WARN("Unexpected DXGI_FORMAT %d", format);
			return 0;
		}

		return s_bitsPerPixel[format];
	}
}
