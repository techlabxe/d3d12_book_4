struct VSInput
{
  float4 Position : POSITION;
  float3 Normal : NORMAL;
  float2 UV0 : TEXCOORD0;
};
struct PSInput
{
  float4 Position : SV_POSITION;
  float3 Normal : NORMAL;
  float2 UV0 : TEXCOORD0;
};

cbuffer SceneParameter : register(b0)
{
  float4x4 view;
  float4x4 proj;
  float4   lightdir;
}

Texture2D gAlbedo : register(t0);
SamplerState gSampler : register(s0);


PSInput mainVS(VSInput In)
{
  PSInput result = (PSInput)0;
  float4x4 mtxVP = mul(view, proj);
  result.Position = mul(In.Position, mtxVP);
  result.Normal = In.Normal;
  result.UV0 = In.UV0;

  return result;
}

float4  mainPS(PSInput In) : SV_TARGET
{
  float4 color = gAlbedo.Sample(gSampler, In.UV0);
  if (color.a < 0.5) {
    discard;
  }
  return color;
}
