#include "MoviePlayer.h"

#include <stdexcept>

#pragma comment(lib, "d3d11.lib")

class MediaEngineNotify : public IMFMediaEngineNotify {
  long m_cRef;
  MoviePlayer* m_callback;
public:
  MediaEngineNotify() : m_cRef(0), m_callback(nullptr) {
  }

  STDMETHODIMP QueryInterface(REFIID riid, void** ppv)
  {
    if (__uuidof(IMFMediaEngineNotify) == riid) {
      *ppv = static_cast<IMFMediaEngineNotify*>(this);
    } else {
      *ppv = nullptr;
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }

  STDMETHODIMP_(ULONG) AddRef()
  {
    return InterlockedIncrement(&m_cRef);
  }
  STDMETHODIMP_(ULONG) Release()
  {
    LONG cRef = InterlockedDecrement(&m_cRef);
    if (cRef == 0) {
      delete this;
    }
    return cRef;
  }
  void SetCallback(MoviePlayer* target)
  {
    m_callback = target;
  }

  STDMETHODIMP EventNotify(DWORD meEvent, DWORD_PTR param1, DWORD param2)
  {
    if (meEvent == MF_MEDIA_ENGINE_EVENT_NOTIFYSTABLESTATE)
    {
      SetEvent(reinterpret_cast<HANDLE>(param1));
    } else
    {
      m_callback->OnMediaEngineEvent(meEvent);
    }
    return S_OK;
  }
};

MoviePlayer::MoviePlayer()
{
  m_cRef = 1;
  m_hPrepare = CreateEvent(NULL, FALSE, FALSE, NULL);

  //m_format = DXGI_FORMAT_NV12;
}

MoviePlayer::~MoviePlayer()
{
  Terminate();
}

void MoviePlayer::Initialize(ComPtr<IDXGIAdapter1> adapter, ComPtr<ID3D12Device> device12, std::shared_ptr<DescriptorManager> descManager)
{
  m_d3d12Device = device12;
  m_descriptorManager = descManager;

  ComPtr<ID3D11Device> deviceD3D11;
  D3D11CreateDevice(
    adapter.Get(),
    D3D_DRIVER_TYPE_UNKNOWN,
    nullptr,
    D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
    nullptr,
    0,
    D3D11_SDK_VERSION,
    deviceD3D11.ReleaseAndGetAddressOf(),
    nullptr,
    nullptr);
  deviceD3D11.As(&m_d3d11Device);

  ComPtr<ID3D10Multithread> multithread;
  deviceD3D11.As(&multithread);
  multithread->SetMultithreadProtected(TRUE);

  // Media Engine のセットアップ.
  ComPtr<IMFDXGIDeviceManager> dxgiManager;
  UINT resetToken = 0;
  MFCreateDXGIDeviceManager(&resetToken, dxgiManager.ReleaseAndGetAddressOf());
  dxgiManager->ResetDevice(m_d3d11Device.Get(), resetToken);

  ComPtr<MediaEngineNotify> notify = new MediaEngineNotify();
  notify->SetCallback(this);

  ComPtr<IMFAttributes> attributes;
  MFCreateAttributes(attributes.GetAddressOf(), 1);
  attributes->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, dxgiManager.Get());
  attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, notify.Get());
  attributes->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, m_format);

  // Create media engine
  ComPtr<IMFMediaEngineClassFactory> mfFactory;
  CoCreateInstance(CLSID_MFMediaEngineClassFactory,
    nullptr,
    CLSCTX_ALL,
    IID_PPV_ARGS(mfFactory.GetAddressOf()));

  mfFactory->CreateInstance(
    0, attributes.Get(), m_mediaEngine.ReleaseAndGetAddressOf());

}

void MoviePlayer::Terminate()
{
  Stop();
  if (m_mediaEngine) {
    m_mediaEngine->Shutdown();
  }
  m_mediaEngine.Reset();

  m_d3d11Device.Reset();
  m_d3d12Device.Reset();
  if (m_hSharedTexture) {
    CloseHandle(m_hSharedTexture);
    m_hSharedTexture = 0;
  }
  if (m_hPrepare) {
    CloseHandle(m_hPrepare);
    m_hPrepare = 0;
  }
}

void MoviePlayer::SetMediaSource(const std::filesystem::path& fileName)
{
  BSTR bstrURL = nullptr;
  if (bstrURL != nullptr) {
    ::CoTaskMemFree(bstrURL);
    bstrURL = nullptr;
  }
  auto fullPath = std::filesystem::absolute(fileName).wstring();

  size_t cchAllocationSize = 1 + fullPath.size();
  bstrURL = reinterpret_cast<BSTR>(::CoTaskMemAlloc(sizeof(wchar_t) * (cchAllocationSize)));

  HRESULT hr;
  wcscpy_s(bstrURL, cchAllocationSize, fullPath.c_str());
  m_isPlaying = false;
  m_isFinished = false;
  hr = m_mediaEngine->SetSource(bstrURL);

  WaitForSingleObject(m_hPrepare, INFINITE);

  m_mediaEngine->GetNativeVideoSize(&m_width, &m_height);

  D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
  D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

  auto resDesc = CD3DX12_RESOURCE_DESC::Tex2D(
    m_format, m_width, m_height, 1, 1);
  resDesc.Flags = flags;

  auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
  m_d3d12Device->CreateCommittedResource(
    &heapProps,
    D3D12_HEAP_FLAG_SHARED,
    &resDesc,
    initialState,
    nullptr,
    IID_PPV_ARGS(&m_videoTexture)
  );
  // D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS は複数のキューからアクセスされることになるので追加.

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = 1;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

  if (m_format == DXGI_FORMAT_B8G8R8A8_UNORM) {
    srvDesc.Format = m_format;
    m_srvVideoTexture = m_descriptorManager->Alloc();
    m_d3d12Device->CreateShaderResourceView(m_videoTexture.Get(), &srvDesc, m_srvVideoTexture);
  }
  if (m_format == DXGI_FORMAT_NV12) {
    auto descriptors = m_descriptorManager->Alloc(2);

    srvDesc.Format = DXGI_FORMAT_R8_UNORM;
    srvDesc.Texture2D.PlaneSlice = 0;
    m_srvLuminance = descriptors[0];
    m_d3d12Device->CreateShaderResourceView(m_videoTexture.Get(), &srvDesc, m_srvLuminance);

    srvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
    srvDesc.Texture2D.PlaneSlice = 1;
    m_srvChrominance = descriptors[1];
    m_d3d12Device->CreateShaderResourceView(m_videoTexture.Get(), &srvDesc, m_srvChrominance);
  }


  // 共有ハンドルの処理.
  hr = m_d3d12Device->CreateSharedHandle(
    m_videoTexture.Get(),
    nullptr,
    GENERIC_ALL,
    nullptr,
    &m_hSharedTexture);
  if (FAILED(hr)) {
    throw std::runtime_error("Failed CreateSharedHandle");
  }
}

void MoviePlayer::Play()
{
  if (m_isPlaying) {
    return;
  }
  if (m_mediaEngine) {
    m_mediaEngine->Play();
    m_isPlaying = true;
  }
}

void MoviePlayer::Stop()
{
  if (!m_isPlaying) {
    return;
  }

  if (m_mediaEngine) {
    m_mediaEngine->Pause();
    m_mediaEngine->SetCurrentTime(0.0);
  }

  m_isPlaying = false;
  m_isFinished = false;
}

void MoviePlayer::OnMediaEngineEvent(uint32_t meEvent)
{
  switch (meEvent)
  {
  case MF_MEDIA_ENGINE_EVENT_LOADSTART:
    OutputDebugStringA("MF_MEDIA_ENGINE_EVENT_LOADSTART\n");
    break;
  case MF_MEDIA_ENGINE_EVENT_PROGRESS:
    OutputDebugStringA("MF_MEDIA_ENGINE_EVENT_PROGRESS\n");
    break;
  case MF_MEDIA_ENGINE_EVENT_LOADEDDATA:
    SetEvent(m_hPrepare);
    break;
  case MF_MEDIA_ENGINE_EVENT_PLAY:
    OutputDebugStringA("MF_MEDIA_ENGINE_EVENT_PLAY\n");
    break;

  case MF_MEDIA_ENGINE_EVENT_CANPLAY:
    break;

  case MF_MEDIA_ENGINE_EVENT_WAITING:
    OutputDebugStringA("MF_MEDIA_ENGINE_EVENT_WAITING\n");
    break;

  case MF_MEDIA_ENGINE_EVENT_TIMEUPDATE:
    break;
  case MF_MEDIA_ENGINE_EVENT_ENDED:
    OutputDebugStringA("MF_MEDIA_ENGINE_EVENT_ENDED\n");
    if (m_mediaEngine) {
      m_mediaEngine->Pause();
      m_isPlaying = false;
      m_isFinished = true;
    }
    break;

  case MF_MEDIA_ENGINE_EVENT_ERROR:
    if (m_mediaEngine) {
      ComPtr<IMFMediaError> err;
      if (SUCCEEDED(m_mediaEngine->GetError(&err))) {
        USHORT errCode = err->GetErrorCode();
        HRESULT hr = err->GetExtendedErrorCode();
        char buf[256] = { 0 };
        sprintf_s(buf, "ERROR: Media Foundation Event Error %u (%08X)\n", errCode, static_cast<unsigned int>(hr));
        OutputDebugStringA(buf);
      } else
      {
        OutputDebugStringA("ERROR: Media Foundation Event Error *FAILED GetError*\n");
      }
    }
    break;
  }
}

bool MoviePlayer::TransferFrame()
{
  if (m_mediaEngine && m_isPlaying) {
    LONGLONG pts;
    HRESULT hr = m_mediaEngine->OnVideoStreamTick(&pts);
    if (SUCCEEDED(hr)) {
      ComPtr<ID3D11Texture2D> mediaTexture;
      hr = m_d3d11Device->OpenSharedResource1(m_hSharedTexture, IID_PPV_ARGS(mediaTexture.GetAddressOf()));
      if (SUCCEEDED(hr)) {
        MFVideoNormalizedRect rect{ 0.0f, 0.0f, 1.0f, 1.0f };
        RECT rcTarget{ 0, 0, LONG(m_width), LONG(m_height)};

        MFARGB  m_bkgColor{ 0xFF, 0xFF, 0xFF, 0xFF };
        hr = m_mediaEngine->TransferVideoFrame(
          mediaTexture.Get(), &rect, &rcTarget, &m_bkgColor);
        if (SUCCEEDED(hr)) {
          return true;
        }
      }
    }
  }
  return false;
}

void MoviePlayer::SetLoop(bool enable)
{
  if (m_mediaEngine) {
    m_mediaEngine->SetLoop(enable);
  }
}
