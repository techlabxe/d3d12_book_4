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
  float3 PositionW: TEXCOORD1;
};

cbuffer SceneParameter : register(b0)
{
  float4x4 view;
  float4x4 proj;
  float4   lightDir;
  float4   cameraPosition;

  uint     animationFrame;
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
  result.PositionW = worldPosition.xyz;
  result.NormalW = normalW;
  result.UV0 = In.UV0;

  return result;
}

float4  mainPS(PSInput In) : SV_TARGET
{
  float2 texUV = In.UV0;
  float3 worldNormal = In.NormalW;

  float dotNL = saturate(dot(worldNormal, normalize(lightDir.xyz)));
  float4 baseColor = gBaseColor.Sample(gSampler, In.UV0);
  float3 color = 0;
  color += dotNL * baseColor.xyz;

  return float4(color, 1);
}
