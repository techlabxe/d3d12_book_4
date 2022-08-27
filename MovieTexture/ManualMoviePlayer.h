#pragma once

#include <vector>
#include <list>
#include <filesystem>

#include <wrl.h>

// for MediaFoundation
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include "DescriptorManager.h"


class ManualMoviePlayer {
public:
  template<typename T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;
  using Path = std::filesystem::path;

  ManualMoviePlayer();
  ~ManualMoviePlayer();

  void Initialize(
    ComPtr<ID3D12Device> device12, 
    std::shared_ptr<DescriptorManager> descManager);
  void Terminate();

  void Update(ComPtr<ID3D12GraphicsCommandList> commandList);

  void SetMediaSource(const Path& fileName);


  void Play();
  void Stop();

  bool IsPlaying() const { return m_isPlaying; }
  bool IsFinished() const { return m_isFinished; }

  void SetLoop(bool enable);

  DescriptorHandle GetMovieTexture();

private:
  void DecodeFrame();
  void UpdateTexture(ComPtr<ID3D12GraphicsCommandList> commandList);
  void ResetPosition();
private:
  IMFSourceReader* m_mfSourceReader = nullptr;
  static const int DecodeBufferCount = 2;

  DXGI_FORMAT m_format;
  bool m_isPlaying = false;
  bool m_isFinished = false;
  bool m_isDecodeFinished = false;
  bool m_isLoop = false;

  struct MovieFrameInfo {
    double timestamp;
    int bufferIndex;
  };
  int m_writeBufferIndex = 0;

  std::list<MovieFrameInfo> m_decoded;
  ComPtr<ID3D12Resource> m_frameDecoded[DecodeBufferCount];

  LARGE_INTEGER m_startedTime;
  ComPtr<ID3D12Resource> m_movieTextureRes;
  DescriptorHandle m_srvMovieTexture;
  ComPtr<ID3D12Device> m_device;
  std::shared_ptr<DescriptorManager> m_descriptorManager;

  UINT m_width = 0, m_height = 0;
  double m_fps = 0;
  UINT m_srcStride = 0;

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_layouts;
  UINT64 m_rowPitchBytes = 0;
};
