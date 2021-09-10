//TODO: HLSL header file for bindless resources
#define myTex2DSpace space1
#define myTexCubeSpace space2

static const int BINDLESS_TABLE_SIZE = 10000;
static const int BINDLESS_INVALID_INDEX = -1;

Texture2D   Texture2DTable[BINDLESS_TABLE_SIZE]   : register(t0, myTex2DSpace);
TextureCube TextureCubeTable[BINDLESS_TABLE_SIZE] : register(t0, myTexCubeSpace);