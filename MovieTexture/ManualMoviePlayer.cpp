#include "ManualMoviePlayer.h"

// for MediaFoundation
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")

ManualMoviePlayer::ManualMoviePlayer()
{
  m_format = DXGI_FORMAT_B8G8R8A8_UNORM;
}

ManualMoviePlayer::~ManualMoviePlayer()
{
  Terminate();
}

void ManualMoviePlayer::Initialize(ComPtr<ID3D12Device> device12, std::shared_ptr<DescriptorManager> descManager)
{
  m_device = device12;
  m_descriptorManager = descManager;
}

void ManualMoviePlayer::Terminate()
{
  Stop();
  m_device.Reset();
  m_descriptorManager.reset();
}

void ManualMoviePlayer::Update(ComPtr<ID3D12GraphicsCommandList> commandList)
{
  if (!m_isPlaying) {
    return;
  }

  DecodeFrame();
  UpdateTexture(commandList);
}

void ManualMoviePlayer::SetMediaSource(const Path& fileName)
{
  auto fullPath = std::filesystem::absolute(fileName).wstring();

  // フォーマット変換のために必要.
  IMFAttributes* attribs;
  MFCreateAttributes(&attribs, 1);
  attribs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);

  HRESULT hr = MFCreateSourceReaderFromURL(fullPath.c_str(), attribs, &m_mfSourceReader);
  if (FAILED(hr)) {
    throw std::runtime_error("MFCreateSourceReaderFromURL() Failed");
  }

  GUID desiredFormat = MFVideoFormat_RGB32;
  if (m_format == DXGI_FORMAT_B8G8R8A8_UNORM) {
    desiredFormat = MFVideoFormat_RGB32;
  }
  if (m_format == DXGI_FORMAT_NV12) {
    desiredFormat = MFVideoFormat_NV12;
  }

  ComPtr<IMFMediaType> mediaType;
  MFCreateMediaType(&mediaType);
  mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  hr = mediaType->SetGUID(MF_MT_SUBTYPE, desiredFormat);
  hr = m_mfSourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, mediaType.Get());

  // ビデオ情報を取得.
  m_mfSourceReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &mediaType);
  MFGetAttributeSize(mediaType.Get(), MF_MT_FRAME_SIZE, &m_width, &m_height);
  UINT32 nume, denom;
  MFGetAttributeRatio(mediaType.Get(), MF_MT_FRAME_RATE, &nume, &denom);
  m_fps = (double)nume / denom;

  hr = mediaType->GetUINT32(MF_MT_DEFAULT_STRIDE, &m_srcStride);

  // テクスチャを用意する.
  auto resDescMovieTex = CD3DX12_RESOURCE_DESC::Tex2D(
    DXGI_FORMAT_B8G8R8A8_UNORM, m_width, m_height, 1, 1);
  auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
  m_device->CreateCommittedResource(
    &defaultHeap, D3D12_HEAP_FLAG_NONE,
    &resDescMovieTex, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&m_movieTextureRes));

  // ShaderResourceViewの準備.
  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = 1;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

  if (m_format == DXGI_FORMAT_B8G8R8A8_UNORM) {
    srvDesc.Format = resDescMovieTex.Format;
    m_srvMovieTexture = m_descriptorManager->Alloc();
    m_device->CreateShaderResourceView(m_movieTextureRes.Get(), &srvDesc, m_srvMovieTexture);
  }
  if (m_format == DXGI_FORMAT_NV12) {
    auto srvDescriptors = m_descriptorManager->Alloc(2);
    m_srvMovieTexture = srvDescriptors[0];

    srvDesc.Format = DXGI_FORMAT_R8_UNORM;
    srvDesc.Texture2D.PlaneSlice = 0;
    m_device->CreateShaderResourceView(m_movieTextureRes.Get(), &srvDesc, srvDescriptors[0]);

    srvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
    srvDesc.Texture2D.PlaneSlice = 1;
    m_device->CreateShaderResourceView(m_movieTextureRes.Get(), &srvDesc, srvDescriptors[1]);
  }

  // テクスチャ転送用バッファ(ステージングバッファ).
  UINT64 totalBytes = 0;
  UINT numRows = 0;
  m_device->GetCopyableFootprints(&resDescMovieTex, 0, 1, 0, &m_layouts, &numRows, &m_rowPitchBytes, &totalBytes);
  totalBytes = (std::max)(totalBytes, numRows * m_rowPitchBytes);
  auto stagingDesc = CD3DX12_RESOURCE_DESC::Buffer(totalBytes);
  for (auto& buffer : m_frameDecoded) {
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    hr = m_device->CreateCommittedResource(
      &heapProps,
      D3D12_HEAP_FLAG_NONE,
      &stagingDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ,
      nullptr,
      IID_PPV_ARGS(&buffer)
    );
    if (FAILED(hr)) {
      throw std::runtime_error("CreateCommittedResource failed.");
    }
  }
}

void ManualMoviePlayer::Play()
{
  if (m_isPlaying) {
    return;
  }

  QueryPerformanceCounter(&m_startedTime);
  m_isPlaying = true;
}

void ManualMoviePlayer::Stop()
{
  if (!m_isPlaying) {
    return;
  }

  m_isPlaying = false;
  m_isFinished = false;
  m_isDecodeFinished = false;

  m_decoded.clear();
  m_writeBufferIndex = 0;
  if (m_mfSourceReader) {
    ResetPosition();
  }
}

void ManualMoviePlayer::SetLoop(bool enable)
{
  m_isLoop = true;
}

DescriptorHandle ManualMoviePlayer::GetMovieTexture()
{
  return m_srvMovieTexture;
}

void ManualMoviePlayer::DecodeFrame()
{
  if (m_decoded.size() == DecodeBufferCount) {
    return;
  }

  if (m_mfSourceReader) {
    DWORD flags;
    ComPtr<IMFSample> sample;
    HRESULT hr = m_mfSourceReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, NULL, &flags, NULL, &sample);
    if (SUCCEEDED(hr)) {
      if (flags == MF_SOURCE_READERF_ENDOFSTREAM) {
        OutputDebugStringA("Stream End.\n");

        ResetPosition();
        m_isDecodeFinished = true;
      } else {
        // サンプルデータからバッファを取得
        ComPtr<IMFMediaBuffer> buffer;
        sample->GetBufferByIndex(0, &buffer);

        // テクスチャへの書き込み
        BYTE* src;
        DWORD size;
        UINT srcPitch = m_width * sizeof(UINT);
        buffer->Lock(&src, NULL, &size);

        BYTE* dst = nullptr;
        auto writeBuffer = m_frameDecoded[m_writeBufferIndex];
        writeBuffer->Map(0, nullptr, (void**)&dst);
        memcpy(dst, src, srcPitch * m_height);
        writeBuffer->Unmap(0, nullptr);
        buffer->Unlock();

        // 時間情報の取得.
        int64_t ts = 0;
        int64_t duration = 0;
        
        sample->GetSampleTime(&ts);
        sample->GetSampleDuration(&duration);

        // 100ns単位 => ns 単位へ.
        ts *= 100;
        duration *= 100;
        double durationSec = double(duration) / 1000000000;
        double timestamp = double(ts) / 1000000000;

        // キューイング
        MovieFrameInfo frameInfo{};
        frameInfo.timestamp = timestamp;
        frameInfo.bufferIndex = m_writeBufferIndex;

        m_decoded.push_back(frameInfo);

        m_writeBufferIndex = (m_writeBufferIndex + 1) % DecodeBufferCount;
      }
    }
  }
}

void ManualMoviePlayer::UpdateTexture(ComPtr<ID3D12GraphicsCommandList> commandList)
{
  if (m_decoded.empty()) {
    return;
  }
  auto frameInfo = m_decoded.front();
  
  LARGE_INTEGER now, freq;
  QueryPerformanceCounter(&now);
  QueryPerformanceFrequency(&freq);
  double playTime = double(now.QuadPart - m_startedTime.QuadPart) / freq.QuadPart;

  // 表示してよい時間かチェック.
  if (playTime < frameInfo.timestamp) {
    //OutputDebugStringA("Stay...\n");
    return;
  }
  m_decoded.pop_front();

  if (m_isDecodeFinished == true && m_decoded.empty()) {
    m_isFinished = true;
    if (m_isLoop) {
      m_isFinished = false;
      m_isDecodeFinished = false;
      QueryPerformanceCounter(&m_startedTime);
    }
  }
  
  // テクスチャの転送
  D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
  dst.pResource = m_movieTextureRes.Get();
  dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;

  src.pResource = m_frameDecoded[frameInfo.bufferIndex].Get();
  src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  src.PlacedFootprint = m_layouts;
  commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
}

void ManualMoviePlayer::ResetPosition()
{
  PROPVARIANT varPosition;
  PropVariantInit(&varPosition);
  varPosition.vt = VT_I8;
  varPosition.hVal.QuadPart = 0;
  m_mfSourceReader->SetCurrentPosition(GUID_NULL, varPosition);
  PropVariantClear(&varPosition);
}
