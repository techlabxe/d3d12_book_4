struct VSInput
{
  float4 Position : POSITION;
  float3 Normal : NORMAL;
  float2 UV0 : TEXCOORD0;
  uint4  BlendIndices : BLENDINDICES;
  float4 BlendWeights : BLENDWEIGHTS;
};
struct GSInput
{
  float4 Position : POSITION;
  float3 Normal : NORMAL;
  float2 UV0 : TEXCOORD0;
};

struct GSOutput {
  float4 Position : POSITION;
  float3 Normal : NORMAL;
  float2 UV0 : TEXCOORD0;
};


cbuffer SceneParameter : register(b0)
{
  float4x4 view;
  float4x4 proj;
  float4   lightdir;
}
cbuffer DrawBatchParameter : register(b1)
{
  uint4 offsetInfo; // x: ベース頂点インデックス.
  float4x4 boneMatrices[512];
}


float4 TransformPosition(VSInput In)
{
  float4 pos = 0;
  float4 inPosition = In.Position;
  uint indices[4] = (uint[4])In.BlendIndices;
  float weights[4] = (float[4])In.BlendWeights;

  for (int i = 0; i < 4; ++i)
  {
    float4x4 mtx = boneMatrices[indices[i]];
    float w = weights[i];
    pos += mul(inPosition, mtx) * w;
  }
  pos.w = 1;
  return pos;
}

float3 TransformNormal(VSInput In)
{
  float3 nrm = 0;
  float3 inNormal = In.Normal;
  uint indices[4] = (uint[4])In.BlendIndices;
  float weights[4] = (float[4])In.BlendWeights;

  for (int i = 0; i < 4; ++i)
  {
    float4x4 mtx = boneMatrices[indices[i]];
    float w = weights[i];
    nrm += mul(inNormal, (float3x3)mtx) * w;
  }
  return normalize(nrm);
}

// スキニング変形を適用して GS へ送信.
GSInput mainVS(VSInput In)
{
  GSInput result = (GSInput)0;

  result.Position = TransformPosition(In);
  result.Normal = TransformNormal(In);
  result.UV0 = In.UV0;

  return result;
}

// 頂点シェーダーから出てきた値をストリーム出力.
[maxvertexcount(3)]
void mainGS(
  triangle GSInput In[3],
  inout TriangleStream<GSOutput> stream)
{
  [unroll]
  for (int i = 0; i < 3; ++i) {
    GSOutput v;
    v.Position = In[i].Position;
    v.Normal = In[i].Normal;
    v.UV0 = In[i].UV0;

    stream.Append(v);
  }
  stream.RestartStrip();
}
