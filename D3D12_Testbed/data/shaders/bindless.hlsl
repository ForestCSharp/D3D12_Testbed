//TODO: HLSL header file for bindless resources
#define myTex2DSpace space1
#define myTexCubeSpace space2

#define BINDLESS_TABLE_SIZE 10000 //TODO Sync up with C++

Texture2D   Texture2DTable[BINDLESS_TABLE_SIZE]   : register(t0, myTex2DSpace);
TextureCube TextureCubeTable[BINDLESS_TABLE_SIZE] : register(t0, myTexCubeSpace);