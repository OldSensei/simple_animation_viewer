
#include <mfapi.h>
#include <mferror.h>

#include <cmath>
#include "video_file_creator.hpp"

namespace SAV
{
	VideoFileCreator::VideoFileCreator(std::wstring_view filename, std::uint32_t width, std::uint32_t height, std::uint32_t bitrate) :
        m_width{width},
        m_height{height},
        m_bitrate{bitrate},
        m_fps{30},
        m_filename{filename},
        m_frameDuration{ 1000.0f / m_fps },
        m_frameTimestamp{0},
        m_isCanceled{ false }
    {
		auto hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		if (!SUCCEEDED(hr))
		{
			::CoUninitialize();
			throw std::exception("CoInitializeEx is failed");
		}

		hr = ::MFStartup(MF_VERSION);
		if (!SUCCEEDED(hr))
		{
			::CoUninitialize();
			throw std::exception("MFStartup is failed");
		}

		hr = initializeSinkWriter();
	}

    HRESULT VideoFileCreator::write(const std::vector<AnimationDescription>& data, std::function<void()> progressCallback)
    {
        std::unique_ptr<Gdiplus::Bitmap> videoFrameBitmap{ new Gdiplus::Bitmap(m_width, m_height) };
        std::unique_ptr<Gdiplus::Graphics> frameGraphics{ new Gdiplus::Graphics(videoFrameBitmap.get()) };
        HRESULT hr = S_OK;

        for (const auto& frame : data)
        {
            std::unique_ptr<Gdiplus::Bitmap> originalBitmap = std::make_unique<Gdiplus::Bitmap>(frame.path().wstring().c_str());
            frameGraphics->DrawImage(originalBitmap.get(), 0, 0, m_width, m_height);

            auto frameCount = static_cast<std::uint32_t>(std::ceil(frame.duration().count() / m_frameDuration));

            for (std::uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
            {
                hr = writeFrame(videoFrameBitmap, static_cast<std::uint32_t>(m_frameDuration * 10000));
                if (!SUCCEEDED(hr) || m_isCanceled)
                {
                    m_isCanceled = false;
                    return hr;
                }
            }

            if (progressCallback)
            {
                progressCallback();
            }
        }

        if (SUCCEEDED(hr))
        {
            m_sinkWriter->Finalize();
        }
        return S_OK;
    }

	HRESULT VideoFileCreator::initializeSinkWriter()
	{
		winrt::com_ptr<IMFMediaType> mediaTypeOut = nullptr;
		winrt::com_ptr<IMFMediaType> mediaTypeIn = nullptr;
		winrt::com_ptr<IMFAttributes> attributes = nullptr;

		auto hr = MFCreateAttributes(attributes.put(), 1);
		if (!SUCCEEDED(hr))
		{
			return hr;
		}

		hr = attributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4);
		if (!SUCCEEDED(hr))
		{
			return hr;
		}

		hr = MFCreateSinkWriterFromURL(m_filename.c_str(), NULL, attributes.get(), m_sinkWriter.put());
		if (!SUCCEEDED(hr))
		{
			return hr;
		}

		hr = MFCreateMediaType(mediaTypeOut.put());
		if (!SUCCEEDED(hr))
		{
			return hr;
		}

		hr = mediaTypeOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		if (!SUCCEEDED(hr))
		{
			return hr;
		}

        hr = mediaTypeOut->SetGUID(MF_MT_SUBTYPE, /*MFVideoFormat_WMV3); MFVideoFormat_M4S2 MFVideoFormat_H264*/ MFVideoFormat_H264);
		if (!SUCCEEDED(hr))
		{
			return hr;
		}

        hr = mediaTypeOut->SetUINT32(MF_MT_AVG_BITRATE, m_bitrate); // consider bitrate as 8Mbps = 8e+6
        if (!SUCCEEDED(hr))
        {
            return hr;
        }

        hr = mediaTypeOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (!SUCCEEDED(hr))
        {
            return hr;
        }

        hr = MFSetAttributeSize(mediaTypeOut.get(), MF_MT_FRAME_SIZE, m_width, m_height);
        if (!SUCCEEDED(hr))
        {
            return hr;
        }

        hr = MFSetAttributeRatio(mediaTypeOut.get(), MF_MT_FRAME_RATE, m_fps, 1);
        if (!SUCCEEDED(hr))
        {
            return hr;
        }

        hr = MFSetAttributeRatio(mediaTypeOut.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (!SUCCEEDED(hr))
        {
            return hr;
        }

        hr = m_sinkWriter->AddStream(mediaTypeOut.get(), &m_videoStreamIndex);
        if (!SUCCEEDED(hr))
        {
            return hr;
        }

        // Set the input media type.
        hr = MFCreateMediaType(mediaTypeIn.put());
        if (!SUCCEEDED(hr))
        {
            return hr;
        }

        hr = mediaTypeIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (!SUCCEEDED(hr))
        {
            return hr;
        }

        hr = mediaTypeIn->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (!SUCCEEDED(hr))
        {
            return hr;
        }

        hr = mediaTypeIn->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        if (!SUCCEEDED(hr))
        {
            return hr;
        }

        hr = mediaTypeIn->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<std::int32_t>(m_width) * 4);
        if (!SUCCEEDED(hr))
        {
            return hr;
        }

        hr = MFSetAttributeSize(mediaTypeIn.get(), MF_MT_FRAME_SIZE, m_width, m_height);
        if (!SUCCEEDED(hr))
        {
            return hr;
        }

        hr = MFSetAttributeRatio(mediaTypeIn.get(), MF_MT_FRAME_RATE, m_fps, 1);
        if (!SUCCEEDED(hr))
        {
            return hr;
        }

        hr = MFSetAttributeRatio(mediaTypeIn.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        if (!SUCCEEDED(hr))
        {
            return hr;
        }

        hr = m_sinkWriter->SetInputMediaType(m_videoStreamIndex, mediaTypeIn.get(), NULL);
        if (!SUCCEEDED(hr))
        {
            return hr;
        }

        // Tell the sink writer to start accepting data.
        return m_sinkWriter->BeginWriting();
	}

    HRESULT VideoFileCreator::writeFrame(std::unique_ptr<Gdiplus::Bitmap>& frame, std::uint32_t frameDuration)
    {
        Gdiplus::Rect rect{0, 0, static_cast<std::int32_t>(frame->GetWidth()), static_cast<std::int32_t>(frame->GetHeight())};
        Gdiplus::BitmapData frameData;

        if (frame->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &frameData) != Gdiplus::Ok)
        {
            return E_FAIL;
        }

        winrt::com_ptr<IMFSample> sample;
        winrt::com_ptr<IMFMediaBuffer> buffer;

        const DWORD bufferSize = frameData.Stride * m_height;
        BYTE* pData = NULL;

        HRESULT hr = MFCreateMemoryBuffer(bufferSize, buffer.put());
        if (!SUCCEEDED(hr))
        {
            frame->UnlockBits(&frameData);
            return hr;
        }

        hr = buffer->Lock(&pData, NULL, NULL);
        if (!SUCCEEDED(hr))
        {
            frame->UnlockBits(&frameData);
            return hr;
        }

        hr = MFCopyImage(pData, frameData.Stride, (BYTE*)frameData.Scan0, frameData.Stride, frameData.Stride, frameData.Height);
        if (!SUCCEEDED(hr))
        {
            frame->UnlockBits(&frameData);
            buffer->Unlock();
            return hr;
        }

        hr = buffer->SetCurrentLength(bufferSize);
        if (!SUCCEEDED(hr))
        {
            frame->UnlockBits(&frameData);
            buffer->Unlock();
            return hr;
        }

        // Create a media sample and add the buffer to the sample.
        hr = MFCreateSample(sample.put());
        if (!SUCCEEDED(hr))
        {
            frame->UnlockBits(&frameData);
            buffer->Unlock();
            return hr;
        }

        hr = sample->AddBuffer(buffer.get());
        if (!SUCCEEDED(hr))
        {
            frame->UnlockBits(&frameData);
            buffer->Unlock();
            return hr;
        }

        // Set the time stamp and the duration.
        hr = sample->SetSampleTime(m_frameTimestamp);
        if (!SUCCEEDED(hr))
        {
            frame->UnlockBits(&frameData);
            buffer->Unlock();
            return hr;
        }

        m_frameTimestamp += frameDuration;
        hr = sample->SetSampleDuration(frameDuration);
        if (!SUCCEEDED(hr))
        {
            m_frameTimestamp = 0;
            frame->UnlockBits(&frameData);
            buffer->Unlock();
            return hr;
        }

        hr = m_sinkWriter->WriteSample(m_videoStreamIndex, sample.get());

        frame->UnlockBits(&frameData);
        buffer->Unlock();
        return hr;
    }
}