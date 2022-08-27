#pragma once

#include <filesystem>

#include <wrl.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <cstdint>

// for MediaFoundation
#include <mfapi.h>
#include <mfidl.h>
#include <mfmediaengine.h>

#include "DescriptorManager.h"

class MoviePlayer {
public:
  template<typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  MoviePlayer();
  ~MoviePlayer();

  void Initialize(ComPtr<IDXGIAdapter1> adapter, ComPtr<ID3D12Device> device12, std::shared_ptr<DescriptorManager> descManager);
  void Terminate();

  void SetMediaSource(const std::filesystem::path& fileName);

  void Play();
  void Stop();

  void OnMediaEngineEvent(uint32_t meEvent);

  bool TransferFrame();

  bool IsPlaying() const { return m_isPlaying; }
  bool IsFinished() const { return m_isFinished; }

  void SetLoop(bool enable);
  DescriptorHandle GetMovieTexture() { return m_srvVideoTexture; }

  DescriptorHandle GetMovieTextureLum() { return m_srvLuminance; }
  DescriptorHandle GetMovieTextureChrom() { return m_srvChrominance; }
private:
  ComPtr<ID3D11Device1> m_d3d11Device;
  ComPtr<ID3D12Device> m_d3d12Device;
  ComPtr<IMFMediaEngine> m_mediaEngine;

  long m_cRef;
  DWORD m_width = 0, m_height = 0;
  DXGI_FORMAT m_format = DXGI_FORMAT_B8G8R8A8_UNORM;
  std::shared_ptr<DescriptorManager> m_descriptorManager;
  bool m_isPlaying = false;
  bool m_isFinished = false;

  ComPtr<ID3D12Resource> m_videoTexture;
  DescriptorHandle m_srvVideoTexture;
  DescriptorHandle m_srvLuminance, m_srvChrominance;

  HANDLE m_hSharedTexture;
  HANDLE m_hPrepare;
};