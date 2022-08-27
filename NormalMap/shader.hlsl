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

  uint drawFlag;
  float heightScale;
}

cbuffer ShaderDrawMeshParameter : register(b1) {
  float4x4 mtxWorld;
  float4 diffuse;
  float4 ambient;
}

Texture2D gBaseColor : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gHeightMap : register(t2);

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

float3x3 MakeTBN(float3 normal, float3 tangent, float3 binormal)
{
  float3x3 mTangentTransform = float3x3(tangent, binormal, normal);
  return mTangentTransform;
}

float3 ToWorldSpace2(float3 tangent, float3 binormal, float3 normal, float3 v) {
  return tangent* v.x + binormal * v.y + normal * v.z;
}

float3 ToWorldSpaceFromTangentSpace(
  float3 normal, float3 tangent, float3 binormal, float3 vect)
{
  float3x3 mTangentTransform = MakeTBN(normal, tangent, binormal);
  float3 vectorTangentSpace = mul(vect, mTangentTransform);
  return vectorTangentSpace;
}

float3 ToTangentSpace(
  float3 normal, float3 tangent, float3 binormal, float3 vect)
{
  float3x3 mtxTangentTransform = transpose(MakeTBN(normal, tangent, binormal));
  float3 vectorTangentSpace = mul(vect, mtxTangentTransform);
  return vectorTangentSpace;
}

float3 FetchNormalMap(float2 uv) {
  float3 normal = gNormalMap.Sample(gSampler,uv).xyz;
  normal = normalize(normal * 2.0 - 1.0);
  return normal;
}
float FetchHeightMap(float2 uv) {
  return gHeightMap.Sample(gSampler, uv).r;
}

float3 ComputeBinormal(PSInput In) {
  return cross(In.NormalW.xyz, In.TangentW.xyz);
}

float3 GetWorldNormal(PSInput In, float2 uv) {
  // 法線を取得.
  float3 normal = FetchNormalMap(uv);
  //normal.y *= -1; // DirectX用法線マップ使用時には反転が必要.

  // 接空間での法線をワールド空間に変換.
  float3 binormal = ComputeBinormal(In);

  float3 v = normal;
  float3 normalW = In.TangentW.xyz * v.x + binormal * v.y + In.NormalW.xyz * v.z;
  return normalize(normalW);
}

float3 GetEyeDirectionTS(PSInput In) {
  float3 toEyeDir = normalize(cameraPosition.xyz - In.PositionW.xyz);
  float3 tangentW = In.TangentW.xyz;
  float3 normalW = In.NormalW.xyz;
  float3 binormalW = ComputeBinormal(In);
  float3 toEyeDirTS;
  toEyeDirTS.x = dot(toEyeDir, tangentW);
  toEyeDirTS.y = dot(toEyeDir, binormalW);
  toEyeDirTS.z = dot(toEyeDir, normalW);
  return toEyeDirTS;
}

// 視差マッピングを適用した UV を返却.
float2 ParallaxMapping(PSInput In) {
  float height = FetchHeightMap(In.UV0);

  float heightBias = 0;
  height = height * heightScale + heightBias;

  // 接空間での視線ベクトルへ変換.
  float3 binormal = cross(In.NormalW.xyz, In.TangentW.xyz);
  
  binormal = ComputeBinormal(In);

  float3 toEye;
  toEye = GetEyeDirectionTS(In);

  // オフセットを計算.
  float2 uv = In.UV0.xy + height * toEye.xy;
  return uv;
}

// 視差遮蔽マッピングを適用して UV を返却.
float2 ParallaxOcclusionMapping(PSInput In) {
  // 接空間での視線ベクトルへ変換.
  float3 binormal = cross(In.NormalW.xyz, In.TangentW.xyz);
  binormal = ComputeBinormal(In);

  float3 toEye;
  toEye = GetEyeDirectionTS(In);

  float2 rayDirectionTS = -toEye.xy / toEye.z;
  float  heightScale = 0.2;
  // 進むことが出来る最大の長さ.
  float2 maxParallaxOffset = rayDirectionTS * heightScale;

  const int sampleCount = 32;
  float zStep = 1.0f / sampleCount;
  float2 texStep = maxParallaxOffset * zStep;

  float rayZ = 1.0;
  int sampleIndex = 0;

  float2 texUV = In.UV0;
  float height = FetchHeightMap(texUV);
  float   prevHeight = height;
  float2  prevUV = texUV;
  while (height < rayZ && sampleIndex < sampleCount) {
    prevHeight = height;
    prevUV = texUV;

    texUV += texStep;
    height = FetchHeightMap(texUV);
    rayZ -= zStep;
    sampleIndex++;
  }

  float   prevRayZ = rayZ + zStep;
  float t = (prevHeight - prevRayZ) / (prevHeight - height + rayZ - prevRayZ);
  texUV = prevUV + t * texStep;

  return texUV;
}

float4  mainPS(PSInput In) : SV_TARGET
{
  float2 texUV = In.UV0;
  if (drawFlag == 1) {
    texUV = ParallaxMapping(In);
  }
  if (drawFlag == 2) {
    texUV = ParallaxOcclusionMapping(In);
  }
  float3 worldNormal = GetWorldNormal(In, texUV);
  float dotNL = saturate(dot(worldNormal, normalize(lightDir.xyz)));

  float4 baseColor = gBaseColor.Sample(gSampler, texUV);
  float3 color = 0;
  color += dotNL * baseColor.xyz;

  return float4(color, 1);
}
