#pragma once
#include "D3D12AppBase.h"
#include "DirectXMath.h"
#include "Camera.h"

#include <array>
#include <unordered_map>

// for MediaFoundation
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include "MoviePlayer.h"
#include "ManualMoviePlayer.h"

#include "Model.h"

class MovieTextureApp : public D3D12AppBase {
public:
  MovieTextureApp();

  virtual void Prepare();
  virtual void Cleanup();
  virtual void Render();

  virtual void OnMouseButtonDown(UINT msg);
  virtual void OnMouseButtonUp(UINT msg);
  virtual void OnMouseMove(UINT msg, int dx, int dy);

  struct ShaderParameters
  {
    DirectX::XMFLOAT4X4 view;
    DirectX::XMFLOAT4X4 proj;
    DirectX::XMFLOAT4 lightDir;
    DirectX::XMFLOAT4 cameraPosition;

    UINT animationFrame;
  };
  ShaderParameters m_sceneParameters;

private:
  struct ShaderDrawMeshParameter {
    DirectX::XMMATRIX mtxWorld;
    DirectX::XMFLOAT4 diffuse; // xyz: diffuseRGB, w: specularShininess
    DirectX::XMFLOAT4 ambient; // xyz: ambientRGB
  };

  void CreateRootSignatures();

  void PreparePipeline();

  void RenderHUD();
  
  void DrawModel();

private:
  Camera m_camera;

  ComPtr<ID3D12RootSignature> m_rootSignature;
  std::vector<Buffer> m_sceneParameterCB;

  using PipelineState = ComPtr<ID3D12PipelineState>;
  std::unordered_map<std::string, PipelineState> m_pipelines;

  enum RootParameterList {
    RP_SCENE_CB = 0,
    RP_MATERIAL = 1,
    RP_BASE_COLOR = 2,
  };

  model::ModelAsset m_model;

  enum PlayerType {
    Mode_PlayerStd = 0,
    Mode_PlayerManual,
  };
  PlayerType m_playerType = Mode_PlayerStd;

  const std::string PSO_DEFAULT = "PSO_DEFAULT";
  
  UINT64 m_frameCount = 0;

  std::unique_ptr<MoviePlayer> m_moviePlayer;
  std::shared_ptr<ManualMoviePlayer> m_moviePlayerManual;
};
