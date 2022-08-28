struct VSInput
{
  float4 Position : POSITION;
  float3 Normal : NORMAL;
  float2 UV0 : TEXCOORD0;
};

struct PSInput
{
  float4 Position : SV_POSITION;
  float3 NormalW : NORMAL;
  float2 UV0 : TEXCOORD0;
  float4 Color : COLOR;
};

cbuffer SceneParameter : register(b0)
{
  float4x4 view;
  float4x4 proj;
  float4   lightDir;
  float4   cameraPosition;

  float4 forceCenter1;

  uint  MaxParticleCount;
  float frameDeltaTime;
  uint padd0;
  uint padd1;

  float4 particleColors[8];

}

cbuffer ShaderDrawMeshParameter : register(b1) {
  float4x4 mtxWorld;
  float4 diffuse;
  float4 ambient;
}

Texture2D gBaseColor : register(t0);
SamplerState gSampler : register(s0);


PSInput mainVS(VSInput In)
{
  float4x4 mtxVP = mul(view, proj);
  float4 worldPosition = mul(In.Position, mtxWorld);
  PSInput result = (PSInput)0;
  result.Position = mul(worldPosition, mtxVP);

  float3x3 mtx = (float3x3)mtxWorld;
  float3 normalW;
  normalW = mul(In.Normal, mtx);
  result.NormalW = normalW;
  result.UV0 = In.UV0;

  return result;
}

float4  mainPS(PSInput In) : SV_TARGET
{
  float2 texUV = In.UV0;
  float4 color = gBaseColor.Sample(gSampler, texUV);

  return color;
}
