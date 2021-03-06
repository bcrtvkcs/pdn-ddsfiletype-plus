////////////////////////////////////////////////////////////////////////
//
// This file is part of pdn-ddsfiletype-plus, a DDS FileType plugin
// for Paint.NET that adds support for the DX10 and later formats.
//
// Copyright (c) 2017, 2018 Nicholas Hayes
//
// This file is licensed under the MIT License.
// See LICENSE.txt for complete licensing and attribution information.
//
////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "DdsFileTypePlusIO.h"
#include <memory>

#include "DirectComputeHelper.h"

using namespace DirectX;

namespace
{
	DXGI_FORMAT GetDXGIFormat(const DdsFileFormat format)
	{
		switch (format)
		{
		case DDS_FORMAT_BC1:
			return DXGI_FORMAT_BC1_UNORM;
		case DDS_FORMAT_BC1_SRGB:
			return DXGI_FORMAT_BC1_UNORM_SRGB;
		case DDS_FORMAT_BC2:
			return DXGI_FORMAT_BC2_UNORM;
		case DDS_FORMAT_BC2_SRGB:
			return DXGI_FORMAT_BC2_UNORM_SRGB;
		case DDS_FORMAT_BC3:
			return DXGI_FORMAT_BC3_UNORM;
		case DDS_FORMAT_BC3_SRGB:
			return DXGI_FORMAT_BC3_UNORM_SRGB;
		case DDS_FORMAT_BC4:
			return DXGI_FORMAT_BC4_UNORM;
		case DDS_FORMAT_BC5:
			return DXGI_FORMAT_BC5_UNORM;
		case DDS_FORMAT_BC6H:
			return DXGI_FORMAT_BC6H_UF16;
		case DDS_FORMAT_BC7:
			return DXGI_FORMAT_BC7_UNORM;
		case DDS_FORMAT_BC7_SRGB:
			return DXGI_FORMAT_BC7_UNORM_SRGB;
		case DDS_FORMAT_B8G8R8A8:
			return DXGI_FORMAT_B8G8R8A8_UNORM;
		case DDS_FORMAT_B8G8R8X8:
			return DXGI_FORMAT_B8G8R8X8_UNORM;
		case DDS_FORMAT_B5G5R5A1:
			return DXGI_FORMAT_B5G5R5A1_UNORM;
		case DDS_FORMAT_B4G4R4A4:
			return DXGI_FORMAT_B4G4R4A4_UNORM;
		case DDS_FORMAT_B5G6R5:
			return DXGI_FORMAT_B5G6R5_UNORM;
		case DDS_FORMAT_R8G8B8A8:
		default:
			return DXGI_FORMAT_R8G8B8A8_UNORM;
		}
	}
}

HRESULT __stdcall Load(const ImageIOCallbacks* callbacks, DDSLoadInfo* loadInfo)
{
	if (callbacks == nullptr || loadInfo == nullptr)
	{
		return E_INVALIDARG;
	}

	TexMetadata info;
	std::unique_ptr<ScratchImage> ddsCompressedImage(new(std::nothrow) ScratchImage);

	if (ddsCompressedImage == nullptr)
	{
		return E_OUTOFMEMORY;
	}

	HRESULT hr = LoadFromDDSIOCallbacks(callbacks, DDS_FLAGS_NONE, &info, *ddsCompressedImage);

	if (FAILED(hr))
	{
		return hr;
	}

	if (IsTypeless(info.format))
	{
		info.format = MakeTypelessUNORM(info.format);

		if (IsTypeless(info.format))
		{
			return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
		}

		ddsCompressedImage->OverrideFormat(info.format);
	}

	if (IsPlanar(info.format))
	{
		std::unique_ptr<ScratchImage> interleavedImage(new(std::nothrow) ScratchImage);

		if (interleavedImage == nullptr)
		{
			return E_OUTOFMEMORY;
		}

		hr = ConvertToSinglePlane(ddsCompressedImage->GetImages(), ddsCompressedImage->GetImageCount(), info, *interleavedImage);

		if (FAILED(hr))
		{
			return hr;
		}

		info = interleavedImage->GetMetadata();
		ddsCompressedImage.swap(interleavedImage);
	}

	const DXGI_FORMAT targetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	std::unique_ptr<ScratchImage> targetImage(new(std::nothrow) ScratchImage);

	if (targetImage == nullptr)
	{
		return E_OUTOFMEMORY;
	}

	if (info.format == targetFormat)
	{
		targetImage.swap(ddsCompressedImage);
	}
	else
	{
		if (IsCompressed(info.format))
		{
			hr = Decompress(ddsCompressedImage->GetImages(), ddsCompressedImage->GetImageCount(), ddsCompressedImage->GetMetadata(), targetFormat, *targetImage);
		}
		else
		{
			hr = Convert(ddsCompressedImage->GetImages(), ddsCompressedImage->GetImageCount(), ddsCompressedImage->GetMetadata(), targetFormat,
				TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, *targetImage, nullptr);
		}

		if (FAILED(hr))
		{
			return hr;
		}

		info = targetImage->GetMetadata();
	}


	if (HasAlpha(info.format) && info.format != DXGI_FORMAT_A8_UNORM)
	{
		// Convert the premultiplied alpha to straight alpha.
		if (info.IsPMAlpha())
		{
			std::unique_ptr<ScratchImage> unmultipliedImage(new(std::nothrow) ScratchImage);

			if (unmultipliedImage == nullptr)
			{
				return E_OUTOFMEMORY;
			}

			hr = PremultiplyAlpha(targetImage->GetImages(), targetImage->GetImageCount(), info, TEX_PMALPHA_REVERSE, *unmultipliedImage);

			if (FAILED(hr))
			{
				return hr;
			}

			info = unmultipliedImage->GetMetadata();
			targetImage.swap(unmultipliedImage);
		}
	}

	const Image* firstImage = targetImage->GetImage(0, 0, 0);

	const size_t outBufferSize = firstImage->slicePitch;

	void* outData = HeapAlloc(GetProcessHeap(), 0, outBufferSize);

	if (outData == nullptr)
	{
		return E_OUTOFMEMORY;
	}

	memcpy_s(outData, outBufferSize, firstImage->pixels, outBufferSize);

	loadInfo->width = static_cast<int32_t>(firstImage->width);
	loadInfo->height = static_cast<int32_t>(firstImage->height);
	loadInfo->stride = static_cast<int32_t>(firstImage->rowPitch);
	loadInfo->scan0 = outData;

	return S_OK;
}

void __stdcall FreeLoadInfo(DDSLoadInfo* info)
{
	if (info != nullptr)
	{
		info->width = 0;
		info->height = 0;
		info->stride = 0;

		if (info->scan0 != nullptr)
		{
			HeapFree(GetProcessHeap(), 0, info->scan0);
			info->scan0 = nullptr;
		}
	}
}

HRESULT __stdcall Save(const DDSSaveInfo* input, const ImageIOCallbacks* callbacks, ProgressProc progressFn)
{
	if (input == nullptr || callbacks == nullptr)
	{
		return E_INVALIDARG;
	}

	std::unique_ptr<ScratchImage> image(new(std::nothrow) ScratchImage);

	if (image == nullptr)
	{
		return E_OUTOFMEMORY;
	}

	HRESULT hr = image->Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM, input->width, input->height, 1, 1, DDS_FLAGS_NONE);

	if (FAILED(hr))
	{
		return hr;
	}

	const uint8_t* srcScan0 = reinterpret_cast<const uint8_t*>(input->scan0);
	uint8_t* destScan0 = image->GetPixels();

	const int destStride = input->width * 4;

	for (int y = 0; y < input->height; y++)
	{
		const uint8_t* src = srcScan0 + (y * input->stride);
		uint8_t* dst = destScan0 + (y * destStride);

		for (int x = 0; x < input->width; x++)
		{
			dst[0] = src[2];
			dst[1] = src[1];
			dst[2] = src[0];
			dst[3] = src[3];

			src += 4;
			dst += 4;
		}
	}

	if (input->generateMipmaps)
	{
		DWORD filter = TEX_FILTER_DEFAULT | TEX_FILTER_SEPARATE_ALPHA;

		switch (input->mipmapSampling)
		{
		case DDS_MIPMAP_SAMPLING_NEAREST_NEIGHBOR:
			filter |= TEX_FILTER_POINT;
			break;
		case DDS_MIPMAP_SAMPLING_BILINEAR:
			filter |= TEX_FILTER_LINEAR;
			break;
		case DDS_MIPMAP_SAMPLING_BICUBIC:
			filter |= TEX_FILTER_CUBIC;
			break;
		case DDS_MIPMAP_SAMPLING_FANT:
		default:
			filter |= TEX_FILTER_FANT;
			break;
		}


		std::unique_ptr<ScratchImage> mipImage(new(std::nothrow) ScratchImage);

		if (mipImage == nullptr)
		{
			return E_OUTOFMEMORY;
		}

		const size_t allMipLevels = 0;

		hr = GenerateMipMaps(image->GetImages(), image->GetImageCount(), image->GetMetadata(), filter, allMipLevels, *mipImage);

		if (FAILED(hr))
		{
			return hr;
		}

		image.swap(mipImage);
	}

	const DXGI_FORMAT dxgiFormat = GetDXGIFormat(input->format);

	if (IsCompressed(dxgiFormat))
	{
		std::unique_ptr<ScratchImage> compressedImage(new(std::nothrow) ScratchImage);

		if (compressedImage == nullptr)
		{
			return E_OUTOFMEMORY;
		}

		DWORD compressFlags = TEX_COMPRESS_DEFAULT | TEX_COMPRESS_PARALLEL;

		if (input->errorMetric == DDS_ERROR_METRIC_UNIFORM)
		{
			compressFlags |= TEX_COMPRESS_UNIFORM;
		}

		std::unique_ptr<DirectComputeHelper> dcHelper = nullptr;
		bool useDirectCompute = false;

		if (dxgiFormat == DXGI_FORMAT_BC7_UNORM || dxgiFormat == DXGI_FORMAT_BC7_UNORM_SRGB || dxgiFormat == DXGI_FORMAT_BC7_TYPELESS ||
			dxgiFormat == DXGI_FORMAT_BC6H_UF16 || dxgiFormat == DXGI_FORMAT_BC6H_SF16 || dxgiFormat == DXGI_FORMAT_BC6H_TYPELESS)
		{
			switch (input->compressionMode)
			{
			case BC7_COMPRESSION_MODE_FAST:
				compressFlags |= TEX_COMPRESS_BC7_QUICK;
				break;
			case BC7_COMPRESSION_MODE_SLOW:
				compressFlags |= TEX_COMPRESS_BC7_USE_3SUBSETS;
				break;
			case BC7_COMPRESSION_MODE_NORMAL:
			default:
				break;
			}

			dcHelper.reset(new(std::nothrow) DirectComputeHelper);
			if (dcHelper != nullptr)
			{
				useDirectCompute = dcHelper->ComputeDeviceAvailable();
			}
		}

		if (useDirectCompute)
		{
			const float alphaWeight = 1.0;

			hr = Compress(dcHelper->GetComputeDevice(), image->GetImage(0, 0, 0), image->GetImageCount(), image->GetMetadata(), dxgiFormat, compressFlags,
				alphaWeight, *compressedImage, progressFn);
		}
		else
		{
			hr = Compress(image->GetImage(0, 0, 0), image->GetImageCount(), image->GetMetadata(), dxgiFormat, compressFlags, TEX_THRESHOLD_DEFAULT, *compressedImage, progressFn);
		}

		if (FAILED(hr))
		{
			return hr;
		}

		image.swap(compressedImage);
	}
	else if (image->GetMetadata().format != dxgiFormat)
	{
		std::unique_ptr<ScratchImage> convertedImage(new(std::nothrow) ScratchImage);

		if (convertedImage == nullptr)
		{
			return E_OUTOFMEMORY;
		}

		hr = Convert(image->GetImage(0, 0, 0), image->GetImageCount(), image->GetMetadata(), dxgiFormat, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, *convertedImage, progressFn);

		if (FAILED(hr))
		{
			return hr;
		}

		image.swap(convertedImage);
	}

	TexMetadata metadata = image->GetMetadata();

	if (HasAlpha(dxgiFormat) && dxgiFormat != DXGI_FORMAT_A8_UNORM)
	{
		if (image->IsAlphaAllOpaque())
		{
			metadata.SetAlphaMode(TEX_ALPHA_MODE_OPAQUE);
		}
		else if (metadata.GetAlphaMode() == TEX_ALPHA_MODE_UNKNOWN)
		{
			metadata.SetAlphaMode(TEX_ALPHA_MODE_STRAIGHT);
		}
	}

	hr = SaveToDDSIOCallbacks(image->GetImages(), image->GetImageCount(), metadata, DDS_FLAGS_NONE, callbacks);

	return hr;
}
