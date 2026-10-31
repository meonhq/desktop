// MEON - 윈도우: WIC (Windows Imaging Component) 로 읽기. 설명은 MeonImageDecode.h
#include "MeonImageDecode.h"

#if defined (_WIN32)

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>

namespace meon
{

namespace
{
    // uuid 라이브러리 없이 (빌드 기준이 윈도우 7): CLSID_WICImagingFactory1, IID_IWICImagingFactory, GUID_WICPixelFormat32bppPBGRA
    const CLSID clsidFactory = { 0xcacaf262, 0x9370, 0x4615, { 0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a } };
    const IID iidFactory = { 0xec5ec8a9, 0xc395, 0x4314, { 0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70 } };
    const GUID formatPBGRA = { 0x6fddc324, 0x4e03, 0x4bfe, { 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x10 } };

    template <typename T> void release (T*& p) { if (p != nullptr) { p->Release(); p = nullptr; } }
}

juce::Image decodeWithSystem (const void* data, size_t size)
{
    if (data == nullptr || size == 0 || size > 0x7fffffff) return {};
    const HRESULT init = CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);   // 이미 됐으면 S_FALSE (짝을 맞춰 푼다)
    juce::Image out;
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    if (SUCCEEDED (CoCreateInstance (clsidFactory, nullptr, CLSCTX_INPROC_SERVER, iidFactory, reinterpret_cast<void**> (&factory))) && factory != nullptr
        && SUCCEEDED (factory->CreateStream (&stream))
        && SUCCEEDED (stream->InitializeFromMemory (static_cast<BYTE*> (const_cast<void*> (data)), (DWORD) size))
        && SUCCEEDED (factory->CreateDecoderFromStream (stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder))
        && SUCCEEDED (decoder->GetFrame (0, &frame))
        && SUCCEEDED (factory->CreateFormatConverter (&converter))
        && SUCCEEDED (converter->Initialize (frame, formatPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
    {
        UINT w = 0, h = 0;
        if (SUCCEEDED (converter->GetSize (&w, &h)) && w > 0 && h > 0 && w <= 16384 && h <= 16384)
        {
            std::vector<BYTE> buf ((size_t) w * (size_t) h * 4);
            if (SUCCEEDED (converter->CopyPixels (nullptr, w * 4, (UINT) buf.size(), buf.data())))
            {
                out = juce::Image (juce::Image::ARGB, (int) w, (int) h, false);
                juce::Image::BitmapData bd (out, juce::Image::BitmapData::writeOnly);
                for (UINT y = 0; y < h; ++y)
                {
                    const BYTE* src = buf.data() + (size_t) y * w * 4;
                    for (UINT x = 0; x < w; ++x, src += 4)   // 미리 곱한 BGRA → JUCE PixelARGB (미리 곱함)
                        reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer ((int) x, (int) y))->setARGB (src[3], src[2], src[1], src[0]);
                }
            }
        }
    }
    release (converter);
    release (frame);
    release (decoder);
    release (stream);
    release (factory);
    if (SUCCEEDED (init)) CoUninitialize();
    return out;
}

} // namespace meon

#endif
