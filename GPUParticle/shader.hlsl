struct VSInput
{
  float4 Position : POSITION;
  float3 Normal : NORMAL;
  float2 UV0 : TEXCOORD0;

  float3 Tangent : TANGENT;
  float3 Binormal :  BINORMAL;
};

struct PSInput
{
  float4 Position : SV_POSITION;
  float3 NormalW : NORMAL;
  float2 UV0 : TEXCOORD0;
  float4 Color : COLOR;
  float3 PositionW: TEXCOORD1;
  float3 TangentW : TEXCOORD2;
  float3 BinormalW: TEXCOORD3;

  float3 ToEyeDirTS : TEXCOORD4;
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
  
  float3 tangentW, normalW, binormalW;
  normalW = mul(In.Normal, mtx);
  tangentW = mul(In.Tangent, mtx);
  binormalW= mul(In.Binormal, mtx);
  result.NormalW = normalW;
  result.TangentW = tangentW;
  result.BinormalW = binormalW;
  result.UV0 = In.UV0;

  result.Color.xyz = In.Tangent * 0.5+0.5;
  result.PositionW = worldPosition.xyz;

  float3 toEyeW;
  toEyeW = normalize(cameraPosition.xyz - worldPosition.xyz);
  float3 binormal2 = cross(normalW, tangentW);
  float3 toEye;
  toEye.x = dot(toEyeW, tangentW);
  toEye.y = dot(toEyeW, binormalW);
  toEye.z = dot(toEyeW, normalW);
  result.ToEyeDirTS = toEye;

  return result;
}

float4  mainPS(PSInput In) : SV_TARGET
{
  float2 texUV = In.UV0;
  float4 color = gBaseColor.Sample(gSampler, texUV);

  return color;
}
