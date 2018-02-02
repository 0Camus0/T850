
//3DS Max viewport shader by Laurens Corijn, http://www.laurenscorijn.com
//Big thanks to Ben Cloward for always being the guy to ask shader questions to; http://www.bencloward.com
//thanks to Jeroen Maton and Lumonix ShaderFX, http://www.jeroenmaton.net, http://www.lumonix.net
//thanks to Brice Vandemoortele for informing me about texCUBElod, made IBL easier and reflection gloss possible! http://www.mentalwarp.com
//thanks to Romain Dura aka Romz for the HSL conversion functions! http://blog.mouaif.org/?p=94


string ParamID = "0x003"; //use DXSAS compiler

float4x4 World 					: WORLD;
float4x4 View					: VIEW;
float4x4 Projection				: PROJECTION;
float4x4 WorldInvTrans			: WorldIT;
float4x4 ViewInv				: ViewInverse;
float4 cameraPos				: WORLDCAMERAPOSITION;

//***************************************************************** DATAMEMBERS
//motherload of stuff here, most is straightforward so uncommented

bool Tiled
<
    string gui = "slider";
    string UIName = "Tiled";
> = false;

bool AlphaMask
<
    string gui = "slider";
    string UIName = "Use AlphaMask";
> = false;

bool AlphaGlass
<
    string gui = "slider";
    string UIName = "Use Alpha Glass";
> = false;

bool MirrorUV
<
    string gui = "slider";
    string UIName = "Use Mirrored UV";
> = false;

bool ShadowCaster
<
    string gui = "slider";
    string UIName = "Can Cast Shadows";
> = false;

bool ShadowRenderer
<
    string gui = "slider";
    string UIName = "Shadow can be Projected on me";
> = false;

bool IndependentLights
<
    string gui = "slider";
    string UIName = "Not uniform Lighting";
> = false;

bool NoLighting
<
    string gui = "slider";
    string UIName = "No Lighting at All";
> = false;

bool HDR
<
    string gui = "slider";
    string UIName = " Will be on HDR";
> = false;

bool UsePhysX
<
    string gui = "slider";
    string UIName = "Use PhysX";
> = false;

bool ConvexMesh
<
    string gui = "slider";
    string UIName = "        Is Convex Mesh";
> = false;

bool TriangleMesh
<
    string gui = "slider";
    string UIName = "        Is Triangle Mesh";
> = false;

bool Terrain
<
    string gui = "slider";
    string UIName = "        Is Terrain";
> = false;

bool Compound
<
    string gui = "slider";
    string UIName = "        Is Compound";
> = false;



struct LightStruct  //struct with lightinfo, easier to work with.
{
	float4 LightVec;
	float4 LightColor; //this value is 32 bit: it is NOT clamped by Max to 8bit per channel, so light color also includes the intensity, since they are mutliplied before Max hads them over
};

float4x4 WorldViewProj;					
LightStruct lightsarray[3]; //3-light array


// these elements are required for projected shadows 
//SHADOWCODE
#include <shadowMap.fxh> 

float  ShadowFloats[3];
float  ShadowStrengths[3];


//===========LIGHTS

int numberOfActiveLights
<
string UIName = "Active Lights";
string UIType = "FloatSpinner";
float UIMin = 0.0f;
float UIMax = 3.0f;
float UIStep = 1.0;
> = 1;

float4 light1_Position : POSITION
<
	string UIName = "  Light 1 Position";
	string Object = "PointLight";
	string Space = "World";
	int refID = 1; 								//refID for automatic lightcolor input
>;// = {100.0f, 100.0f, 100.0f, 0.0f};

//lightcolor = lightrgb x lightmultiplier, done by Max
float4 light1Color : LIGHTCOLOR <int LightRef = 1; string UIWidget = "None"; > = { 1.0f, 1.0f, 1.0f, 1.0f}; 	//lightref ID makes sure that max fills in this value automatically



//SHADOWCODE
SHADOW_FUNCTOR(shadowTerm1); //Only 1 shadow supported, more is just not necessary and not worth the effort and trouble. the shader would also become too slow and messy because of it
//SOFTSHADOW
//SOFT_SHADOW_FUNCTOR(SoftShadowTerm1, 000, float4(10,5,0.01,0), false); // NOTE: '000' represents for omni light, '001' is spot, and '002' is direct.

bool bUseShadow1
<
    string gui = "slider";
    string UIName = "     Use Shadow 1";
> = false;

float Shadow1Soft
<
	string UIName = "     Shadow1 Blur";
	string UIWidget = "FloatSpinner";
	float UIMin = 0.0f;
	float UIMax = 50.0f;
> = {0.0f};

float Shadow1Strength
<
	string UIName = "     Shadow1 Intensity";
	string UIWidget = "FloatSpinner";
	float UIMin = 0.0f;
	float UIMax = 1.0f;
> = {1.0f};

float DistanceBlur
<
	string UIName = "     Distance Shadow Blur";
	string UIWidget = "FloatSpinner";
> = {3.0f};



float4 light2_Position : POSITION
<
	string UIName = "  Light 2 Position";
	string Object = "PointLight";
	string Space = "World";
	int refID = 2;
>;// = {100.0f, 100.0f, 100.0f, 1.0f};

float4 light2Color : LIGHTCOLOR <int LightRef = 2; string UIWidget = "None"; > = { 1.0f, 1.0f, 1.0f, 1.0f}; 

float4 light3_Position : POSITION
<
	string UIName = "  Light 3 Position";
	string Object = "PointLight";
	string Space = "World";
	int refID = 3;
>;// = {100.0f, 100.0f, 100.0f, 1.0f};

float4 light3Color : LIGHTCOLOR <int LightRef = 3; string UIWidget = "None"; > = { 1.0f, 1.0f, 1.0f, 1.0f}; 

float HueAdjust
<
	string UIName = "Shaded Hue Adjustment";
	string UIWidget = "FloatSpinner";
	float UIMin = 0.0f;
	float UIMax = 2.0f;
	float UIStep = 0.1f;
> = {0.3f};


bool bUseHalfLambert
<
    string gui = "slider";
    string UIName = "     Half Lambert Shading";
> = false;

float HalfLambertPower
<
string UIName = "         Half Lambert Power";
string UIType = "FloatSpinner";
float UIMin = 0.0f;
float UIMax = 10.0f;
float UIStep = 0.1;
> = 2;

float3 ambientcolor
<
	string UIName = "     Ambient Color";
	string UIWidget = "Color";
> = {0.0f, 0.0f, 0.0f};

bool bUseAmbientMap
<
    string gui = "slider";
    string UIName = "Ambient Map";
> = false;

texture ambientMap : AMBIENTMAP
<
	string name ="";
	string UIName = "     Ambient Map";	
	int Texcoord = 0;
	int MapChannel = 1;

>;

sampler2D ambientSampler = sampler_state
{
	Texture = <ambientMap>;
	MinFilter=LINEAR;
	MagFilter=LINEAR;
	MipFilter=LINEAR;
	MipLODBias=-1;
	AddressU = WRAP;
	AddressV = WRAP;
};


bool bUseIBL
<
    string gui = "slider";
    string UIName = "         Use IBL cubemap for Ambient";
> = false;


bool bUseIBLCubeMap
<
    string gui = "slider";
    string UIName = "         Share Reflection Cubemap for IBL";
> = false;

texture IBLcubemap
<
	string ResourceName = "";
	string UIName = "     IBL Cubemap";
	string ResourceType = "Cube";
>;

samplerCUBE IBLcubemapSampler = sampler_state
{
	Texture = <IBLcubemap>;
	MinFilter=LINEAR;
	MagFilter=LINEAR;
	MipFilter=LINEAR;
	MipLODBias=0;
	AddressU = WRAP;
	AddressV = WRAP;
};

float IBLBlur
<
string UIName = "         Ambient Cube Blur";
string UIType = "FloatSpinner";
float UIMin = 0.0f;
float UIMax = 10.0f;
float UIStep = 0.1f;
> = 7.6f;

float IBLmultiplier
<
string UIName = "         Ambient Cube Strength";
string UIType = "FloatSpinner";
float UIMin = 0.0f;
float UIMax = 2.0f;
float UIStep = 0.05;
> = 0.5;

//==========LightMAP

bool bUseLightMap
<
    string gui = "FloatSpinner";
    string UIName = "Use Light Map";
	float UIStep = 1.0f;
	float UIMin = 0.0f;
	float UIMax = 1.0f;
> = 0;

texture LightMap : LIGHTMAP
<
	string name ="";
	string UIName = "     Light Map";	
	int Texcoord = 2;
	int MapChannel = 1;

>;


float LightMapStrength
<
string UIName = "         Lightmap Multiplier";
string UIType = "FloatSpinner";
float UIMin = 0.0f;
float UIMax = 1.0f;
float UIStep = 0.05;
> = 1.0f;


sampler2D lightSampler = sampler_state
{
	Texture = <LightMap>;
	MinFilter=LINEAR;
	MagFilter=LINEAR;
	MipFilter=LINEAR;
	MipLODBias=-1;
	AddressU = WRAP;
	AddressV = WRAP;
};


//==========DIFFUSEMAP

bool bUseDiffuseMap
<
    string gui = "slider";
    string UIName = "Diffuse Map";
> = false;

half4 diffuseColor : DIFFUSECOLOR
<
	string UIWidget = "Color";
    string UIName = "     Diffuse Color";
> = {0.5f, 0.5f, 0.5f, 1.0f};

texture diffuseMap : DIFFUSEMAP
<
	string name ="";
	string UIName = "     Diffuse Map";	
	int Texcoord = 0;
	int MapChannel = 1;

>;


bool bColorDiffuse
<
    string gui = "slider";
    string UIName = "     Color Diffuse Map";
> = false;


sampler2D diffuseSampler = sampler_state
{
	Texture = <diffuseMap>;
	MinFilter=LINEAR;
	MagFilter=LINEAR;
	MipFilter=LINEAR;
	MipLODBias=-1;
	AddressU = WRAP;
	AddressV = WRAP;
};

//==========OPACITYMAP

bool bUseAlpha
<
    string gui = "slider";
    string UIName = "     Enable alpha channel";
> = false;

float GlobalOpacity
<
string UIName = "         Global Opacity Level";
string UIType = "FloatSpinner";
float UIMin = 0.0f;
float UIMax = 1.0f;
float UIStep = 0.05;
> = 1;


//==========SPECULARMAP

bool bUseSpecMap
<
    string gui = "slider";
    string UIName = "Spec Map";
> = false;

half4 specularColor : SPECULARCOLOR
<
	string UIWidget = "Color";
    string UIName = "     Specular Color";
> = {1.0f, 1.0f, 1.0f, 1.0f};

float speclevel
<
string UIName = "     Specular Global Level";
string UIType = "FloatSpinner";
float UIMin = 0.0f;
float UIMax = 10.0f;
float UIStep = 0.05;
> = 1.0f;

texture specularMap : SPECULARMAP
<
	string name ="";
	string UIName = "     Specular Map";
	string ResourceType = "2D";

>;

sampler2D specularSampler = sampler_state
{
	Texture = <specularMap>;
	MinFilter=LINEAR;
	MagFilter=LINEAR;
	MipFilter=LINEAR;
	MipLODBias=-1;
	AddressU = WRAP;
	AddressV = WRAP;
};

//==========GLOSSMAP

bool bUseGlossMap
<
    string gui = "slider";
    string UIName = "Gloss Map";
> = false;

texture glossMap : GLOSSINESS
<
	string name ="";
	string UIName = "     Gloss Map";
	string ResourceType = "2D";

>;

sampler2D glossinessSampler = sampler_state
{
	Texture = <glossMap>;
	MinFilter=LINEAR;
	MagFilter=LINEAR;
	MipFilter=LINEAR;
	MipLODBias=-1;
	AddressU = WRAP;
	AddressV = WRAP;
};

float glossiness
<
	string UIName = "     Glossiness Level";
	string UIType = "FloatSpinner";
	float UIMin = 0.0f;
	float UIMax = 10000.0f;
	float UIStep = 0.05;
> = 25.0f;

float glossoffset
<
	string UIName = "     Glossmap Offset";
	string UIType = "FloatSpinner";
	float UIMin = 0.0f;
	float UIMax = 100.0f;
	float UIStep = 0.05;
> = 10.0f;
 
//==========NORMALMAP
 
bool bUseNormalMap
<
    string gui = "slider";
    string UIName = "Normal Map";
> = false;

bool bUseObjectNormals
<
    string gui = "slider";
    string UIName = "     Object Space";
> = false;


texture normalMap : NORMALMAP
<
	string name ="";
	string UIName = "     Normal Map";
	string ResourceType = "2D";
	int Texcoord = 1;
	int MapChannel = 1;
>;

//========= HEIGHTMAP
texture heightMap : HEIGHTMAP
<
	string name ="";
	string UIName = "     Height Map";
	string ResourceType = "2D";
	int Texcoord = 1;
	int MapChannel = 1;
>;


bool bFlipGreenChannel
<
    string gui = "slider";
    string UIName = "     Flip Green";
> = true;
/*
bool bFlipRG
<
    string gui = "slider";
    string UIName = "     Switch RG Channels ";
> = false;
*/
sampler2D normalSampler = sampler_state
{
	Texture = <normalMap>;
	MinFilter=LINEAR;
	MagFilter=LINEAR;
	MipFilter=LINEAR;
	MipLODBias=-1;
	AddressU = WRAP;
	AddressV = WRAP;
};
//==========REFLECTION

bool bUseReflMap
<
    string gui = "slider";
    string UIName = "Reflection Map";
> = false;

texture ReflMap 
<
	string name ="";
	string UIName = "     Reflect Map";

>;

sampler2D ReflMapSampler = sampler_state
{
	Texture = <ReflMap>;
	MinFilter=LINEAR;
	MagFilter=LINEAR;
	MipFilter=LINEAR;
	MipLODBias=-1;
	AddressU = WRAP;
	AddressV = WRAP;
};

float ReflectThreshold
<
	string UIWidget = "     Reflect Map Threshold";
	float UIMin = 0.0;
	float UIMax = 1.0;
	float UIStep = 0.01;
	string UIName = "     Reflect Map Threshold";
> = 0.0;

bool bUseCubeMap
<
    string gui = "slider";
    string UIName = "Reflect Cubemap";
> = false;

float CubeIntensityLevel<
	string UIName = "     Cube Intensity";
	string UIType = "FloatSpinner";
	float UIMin = 0.0f;
	float UIMax = 1.5f;
	float UIStep = 0.01f;
> = 0.5f;

texture reflcubemap : environment
<
	string ResourceName = "";
	string UIName = "     Cubemap";
	string ResourceType = "Cube";
>;

samplerCUBE reflcubemapSampler = sampler_state
{
	Texture = <reflcubemap>;
	MinFilter=LINEAR;
	MagFilter=LINEAR;
	MipFilter=LINEAR;
	MipLODBias=0;
	AddressU = WRAP;
	AddressV = WRAP;
};

//==========UniMap

bool bUseUnimap
<
    string gui = "slider";
    string UIName = "Uni-Map";
> = false;

texture UniMap 
<
	string name ="";
	string UIName = "     UniMap";

>;

sampler2D UniMapSampler = sampler_state
{
	Texture = <UniMap>;
	MinFilter=LINEAR;
	MagFilter=LINEAR;
	MipFilter=LINEAR;
	MipLODBias=-1;
	AddressU = WRAP;
	AddressV = WRAP;
};

//==========GLOWMAP

bool bUseSIMap
<
    string gui = "slider";
    string UIName = "Self Illumination Map";
> = false;

texture siMap : SIMAP
<
	string name ="";
	string UIName = "     SI Map";
	string ResourceType = "2D";

>;

sampler2D siMapSampler = sampler_state
{
	Texture = <siMap>;
	MinFilter=LINEAR;
	MagFilter=LINEAR;
	MipFilter=LINEAR;
	MipLODBias=-1;
	AddressU = WRAP;
	AddressV = WRAP;
};

float siMapMult<
	string UIName = "     SI Map Multiply";
	string UIType = "FloatSpinner";
	float UIMin = 0.0f;
	float UIMax = 10.0f;
> = 1.0f;

float siLevel<
	string UIName = "     Global SI Level";
	string UIType = "FloatSpinner";
	float UIMin = 0.0f;
	float UIMax = 100.0f;
	float UIStep = 1.0f;
> = 0;

//==========Reflections

bool bUseEdge
<
    string gui = "slider";
    string UIName = "Use Outline";
> = false;

float EdgeStep
<
	string UIWidget = "Spinner";
	float UIMin = 0.0;
	float UIMax = 1.0;
	float UIStep = 0.01;
	string UIName = "     Edge Step";
> = 0.8;

float3 EdgeColor 
<
	string UIWidget = "Color";
    string UIName = "     Edge Color";
> = {0.0f, 0.0f, 0.0f};

//==========Reflections

bool bUseFresnel
<
    string gui = "slider";
    string UIName = "Use Fresnel Reflections";
> = false;

bool bUseFresnelMap
<
    string gui = "slider";
    string UIName = "Use Fresnel Map";
> = false;

texture FresMap : FRESNELMAP
<
	string name ="";
	string UIName = "     Fresnel Map";
	string ResourceType = "2D";

>;

sampler2D FresMapSampler = sampler_state
{
	Texture = <FresMap>;
	MinFilter=LINEAR;
	MagFilter=LINEAR;
	MipFilter=LINEAR;
	MipLODBias=-1;
	AddressU = WRAP;
	AddressV = WRAP;
};

bool bAlphaMasksFresnel
<
    string gui = "slider";
    string UIName = "     Opacity Affects Reflections";
> = false;

float FresnelPower
<
	string UIWidget = "Spinner";
	float UIMin = 0.0;
	float UIMax = 100.0;
	float UIStep = 0.1;
	string UIName = "     Fresnel Power";
> = 3.0;
 
 
float FresnelBias
<
	string UIWidget = "Spinner";
	float UIMin = 0.0;
	float UIMax = 1.0;
	float UIStep = 0.01;
	string UIName = "     Fresnel Bias";
> = 0.0;

float FresnelMult
<
	string UIWidget = "Spinner";
	float UIMin = 0.0;
	float UIMax = 100.0;
	float UIStep = 0.1;
	string UIName = "     Fresnel Multiplier";
> = 2.0;

float3 FresnelColor 
<
	string UIWidget = "Color";
    string UIName = "     Rim Color";
> = {1.0f, 1.0f, 1.0f};

float FresnelMaskHardness
<
	string UIWidget = "Spinner";
	float UIMin = 0.0;
	float UIMax = 100.0;
	float UIStep = 0.1;
	string UIName = "     Fresnel Mask Hardness";
> = 4.0;

bool bUseWorldMask
<
    string gui = "slider";
    string UIName = "     Use Hard-Type World Masking";
> = true;

float CubeMapBlur
<
	string UIWidget = "Spinner";
	float UIMin = 0.0;
	float UIMax = 10.0;
	float UIStep = 0.1;
	string UIName = "     Cubemap Blur";
> = 0.0;
bool bUseReflGloss
<
    string gui = "slider";
    string UIName = "         Use Glossmap for blur";
> = false;



//----------------------------------VS & PS structs 


// input from application for Vertex Shader
struct VS_InputStruct {
	half4 position		: POSITION;
	half2 texCoord		: TEXCOORD0;
	half2 texCoord2		: TEXCOORD1;
	half2 texCoord3		: TEXCOORD2;
	half3 tangent		: TANGENT;
	half3 binormal		: BINORMAL;
	half3 normal		: NORMAL;
};

// Vertex Shader output for Pixel Shader
struct VS_To_PS_Struct {
        half4 position   	: POSITION;
		half2 texCoord    	: TEXCOORD0;
		half3 eyeVec		: TEXCOORD1;
		half3 worldNormal	: TEXCOORD2;
		half3 worldTangent	: TEXCOORD3;
		half3 worldBinormal	: TEXCOORD4;
		half4 worldSpacePos	: TEXCOORD5;
		half2 texCoord2		: TEXCOORD6;
		half2 texCoord3		: TEXCOORD7;
		
};


//==============Custom Functions

void SetupMatrices()
{
	WorldViewProj = mul(mul(World, View), Projection); //just create WVP by multipying
}

//standard diffuse lighting by dot product
float diffuselight( float3 normal, float3 lightvec)
	{
		normal = normalize(normal);
		lightvec = normalize(lightvec);
		
		return saturate(dot(normal,lightvec)); //dot product between surface and light returns how lit the pixel is. clamp between 0 and 1 because intensity is multiplied later
	}

//Half Lambert lighting, function by Valve Software.
//see http://www.valvesoftware.com/publications/2007/NPAR07_IllustrativeRenderingInTeamFortress2.pdf for more info
float halflambert(float3 normal, float3 lightvec) 
{
	normal = normalize(normal);
	lightvec = normalize(lightvec);
	
	float NdotL = dot(lightvec,normal);		//dot product for base diffuse light
	float HalfLamb = (NdotL * 0.5f)+0.5f;	//magic formula!
	HalfLamb = pow(HalfLamb,HalfLambertPower);		//power halflambert 

	return  saturate(HalfLamb); 
}

float halflambert(float value) 
{
	
	float HalfLamb = (value * 0.5f)+0.5f;	//magic formula!
	HalfLamb = pow(HalfLamb,HalfLambertPower);		//power halflambert 

	return  saturate(HalfLamb); 
}

//seperate specular calculation to make my life easier coding this thing
//color and masking is NOT done here; this is just for pure, raw specular calculation
//thanks to http://wiki.gamedev.net/index.php/D3DBook:(Lighting)_Blinn-Phong for the very clean and understandable explanation
float blinnspecular(float3 normal, float3 lightvec, float3 eyevec, float glossiness)
{
	normal = normalize(normal);
	lightvec = normalize(lightvec);
	eyevec = normalize(eyevec);
	
	float3 halfvector = normalize(eyevec+lightvec); //add eye and light together for half vector (Blinn)
	
	float specular;
	specular = dot(halfvector, normal); //dot between half and normal (Blinn)
	specular = pow(specular, glossiness); //power specular to glossiness to sharpen highlight
	specular *= saturate(dot(normal,lightvec) * 4); //fix for Specular through surface bug. what this does is just make sure no specular happens on unlit parts. the multiplier works as a bias
	
	return specular;
	
}

//Fresnel falloff function for all round application
float fresnel(float3 normal, float3 eyevec, float power, float bias)
{
	normal = normalize(normal);
	eyevec = normalize(eyevec);
	
	float fresnel = saturate(abs(dot(normal,eyevec))); //get fallof by dot product between normal and eye, absolute to prevent falloff to go negative on backside of object 
	fresnel = 1-fresnel; //invert falloff to get white instead of black on edges
	fresnel = pow(fresnel, power); //power falloff to sharpen effect
	fresnel+=bias; // add bias to falloff, this is mostly for cubemap reflections like in carpaint
	
	return saturate(fresnel);
}

//desaturate/luminance value function
float desaturate(float3 color)
{
	float luminance;
	luminance = dot(color,float3(0.299,0.587,0.114)); //desaturate by dot multiplying with luminance weights.
	return luminance;
}

//Fill light array function
void CreateLights( float4 worldspacepos )
{
	
		lightsarray[0].LightVec = light1_Position - worldspacepos; // transform light position to world space position, put into array
		lightsarray[0].LightColor = light1Color; // set light color to array. light color is automatically updated by Max because we use RefId's.
		
		lightsarray[1].LightVec = light2_Position - worldspacepos;
		lightsarray[1].LightColor = light2Color;
		
		lightsarray[2].LightVec = light3_Position - worldspacepos;
		lightsarray[2].LightColor = light3Color;
	
}

float3 RGBToHSL(float3 color) //Romain's conversion function
{
	float3 hsl; // init to 0 to avoid warnings ? (and reverse if + remove first part)
	
	float fmin = min(min(color.r, color.g), color.b);    //Min. value of RGB
	float fmax = max(max(color.r, color.g), color.b);    //Max. value of RGB
	float delta = fmax - fmin;             //Delta RGB value

	hsl.z = (fmax + fmin) / 2.0; // Luminance

	if (delta == 0.0)		//This is a gray, no chroma...
	{
		hsl.x = 0.0;	// Hue
		hsl.y = 0.0;	// Saturation
	}
	else                                    //Chromatic data...
	{
		if (hsl.z < 0.5)
			hsl.y = delta / (fmax + fmin); // Saturation
		else
			hsl.y = delta / (2.0 - fmax - fmin); // Saturation
		
		float deltaR = (((fmax - color.r) / 6.0) + (delta / 2.0)) / delta;
		float deltaG = (((fmax - color.g) / 6.0) + (delta / 2.0)) / delta;
		float deltaB = (((fmax - color.b) / 6.0) + (delta / 2.0)) / delta;

		if (color.r == fmax )
			hsl.x = deltaB - deltaG; // Hue
		else if (color.g == fmax)
			hsl.x = (1.0 / 3.0) + deltaR - deltaB; // Hue
		else if (color.b == fmax)
			hsl.x = (2.0 / 3.0) + deltaG - deltaR; // Hue

		if (hsl.x < 0.0)
			hsl.x += 1.0; // Hue
		else if (hsl.x > 1.0)
			hsl.x -= 1.0; // Hue
	}

	return hsl;
}

float HueToRGB(float f1, float f2, float hue)
{
	if (hue < 0.0)
		hue += 1.0;
	else if (hue > 1.0)
		hue -= 1.0;
	float res;
	if ((6.0 * hue) < 1.0)
		res = f1 + (f2 - f1) * 6.0 * hue;
	else if ((2.0 * hue) < 1.0)
		res = f2;
	else if ((3.0 * hue) < 2.0)
		res = f1 + (f2 - f1) * ((2.0 / 3.0) - hue) * 6.0;
	else
		res = f1;
	return res;
}


float3 HSLToRGB(float3 hsl)
{
	float3 rgb;
	
	if (hsl.y == 0.0)
		rgb = float3(hsl.z, hsl.z, hsl.z); // Luminance
	else
	{
		float f2;
		
		if (hsl.z < 0.5)
			f2 = hsl.z * (1.0 + hsl.y);
		else
			f2 = (hsl.z + hsl.y) - (hsl.y * hsl.z);
			
		float f1 = 2.0 * hsl.z - f2;
		
		rgb.r = HueToRGB(f1, f2, hsl.x + (1.0/3.0));
		rgb.g = HueToRGB(f1, f2, hsl.x);
		rgb.b= HueToRGB(f1, f2, hsl.x - (1.0/3.0));
	}
	
	return rgb;
}

//SHADOWCODE
//shadow blur lookup function
//special multisample function made by Laurens
//HEAVY ON PERFORMANCE due to crappy 3DS max shadow API
float ShadowLookUp(float4 position, float3 tangent, float3 binormal, float blur)
{
	float blurscale = sqrt(blur);
	float shadowleftup = shadowTerm1(position + float4((tangent*blurscale).xyz,0)+ float4((binormal*blurscale).xyz,0)); // sample shadows around pixel location, sampling depth determined by blur scale
	float shadowleftdown = shadowTerm1(position + float4((tangent*blurscale).xyz,0)- float4((binormal*blurscale).xyz,0));
	float shadowrightup = shadowTerm1(position - float4((tangent*blurscale).xyz,0)+ float4((binormal*blurscale).xyz,0));
	float shadowrightdown = shadowTerm1(position - float4((tangent*blurscale).xyz,0)- float4((binormal*blurscale).xyz,0));
	float shadowtotal = (shadowleftup+shadowleftdown+shadowrightup+shadowrightdown)/4; //average 4 samples
	return shadowtotal;
}


//Vertex And Pixel Shaders

//VERTEX SHADER
VS_To_PS_Struct vs_main(VS_InputStruct In) //vertexshader gets input struct from application, all automatically
{
	SetupMatrices(); //create WVP
	
	VS_To_PS_Struct Out; //define output struct
	
    Out.worldNormal = mul(In.normal, (float3x3)World).xyz; //transform normal to worldspace
	Out.worldTangent = mul(In.tangent, (float3x3)World).xyz; //transform tangent to worldspace
	Out.worldBinormal = mul(In.binormal, (float3x3)World).xyz; //transform binormal to worldspace
    
	Out.worldSpacePos = mul(In.position, World);	 //transform position to worldspace
  
	Out.texCoord.xy = In.texCoord; //pass texcoord to output
	Out.texCoord2.xy = In.texCoord2; //pass texcoord2 to output
	Out.texCoord3.xy = In.texCoord3; //pass texcoord2 to output
    Out.eyeVec = ViewInv[3].xyz -  Out.worldSpacePos; //calculate eyevector
    Out.position = mul(In.position, WorldViewProj); //special worldspacepos in texcoord slot for shadow code, will error otherwise

    return Out;
}

//PIXEL SHADER
float4 ps_main(VS_To_PS_Struct In) : COLOR 
{ 
	//Map Checks
	//these just check if a map is used to overwrite the default value.
		
	//diffuse
	float4 color = float4(diffuseColor); //default diffusecolor
	if(bUseDiffuseMap)
	{
		color = tex2D(diffuseSampler, In.texCoord.xy);
		
		
		if(bColorDiffuse) color*=diffuseColor; //multiply diffuse texturesample with color, useful for recoloring grayscale diffusemaps
	} 
	
	//opacity
	float opacity = GlobalOpacity; //default is global opacity
	if(bUseAlpha)
	{
		opacity = color.a; //sampled alpha from diffuse map
	}
	
	
	//specular
	float4 speccolor = specularColor; //default is solid specular color
	if(bUseSpecMap)
	{
		speccolor = tex2D(specularSampler, In.texCoord.xy);
	}
	
	float3 Unimap = 0.0f;
	
	if(bUseUnimap)
		Unimap = tex2D(UniMapSampler, In.texCoord.xy).rgb;
	
	
	//glossiness
	float gloss = glossiness;
	
	if(bUseGlossMap){
	
		if(bUseUnimap){
			gloss =	(Unimap.r) * (glossiness - glossoffset);
			gloss += glossoffset;
		}else{
				gloss = tex2D(glossinessSampler, In.texCoord.xy) * (glossiness - glossoffset); //overwrite glossiness global with texsample, multiplied with level
				gloss += glossoffset; //offset gloss to prevent zero glossiness, allows for better control over glossiness map range
		}
	
	}
	
	
	//normal
	float3 normal = In.worldNormal; //default worldspace vertex normal
	if(bUseNormalMap)
	{
		normal = tex2D(normalSampler, In.texCoord2.xy).rgb;
		normal = (2*normal)-1; //expand into -1 to 1 range, since a normalmap is always returned in 0 to 1 space
		if (bFlipGreenChannel) normal.g =-normal.g; //flip green for texture instead of flipping tangent, works for object space normals also
		/*
		if (bFlipRG) 
		{
			float temp = normal.r;
			normal.r = normal.g;
			normal.g = temp;
		}*/
	}
	
	//self illumination
	float4 selfillumination = (0.0f,0.0f,0.0f,0.0f); //default no self illum
	if(bUseSIMap)
	{
		selfillumination = tex2D(siMapSampler, In.texCoord.xy);
	}
	
	
	
	//Create Light Calculations
	CreateLights( In.worldSpacePos ); // fill lightstruct array DOES NOT WORK IN VERTEX SHADER, MUST HAPPEN IN PIXEL SHADER! ~0_o>
	
	//normal application
	if(!bUseObjectNormals && bUseNormalMap) //tangentspace normalmap
	{
		normal = (In.worldNormal * normal.z) + (normal.x * In.worldBinormal + normal.y * -In.worldTangent); //transform every vertex normal component by the tangentspace normalmap
	}
	if(bUseObjectNormals && bUseNormalMap) //if object space, pure sampling equals the normal
	{
		normal = mul(normal, WorldInvTrans).xyz; // put objectspace normal in worldspace
	}
	
	normal = normalize( normal );
	
	//SHADOWCODE
	//Shadow calculations
	ShadowFloats[0]=ShadowStrengths[0]=ShadowFloats[1]=ShadowStrengths[1]=ShadowFloats[2]=ShadowStrengths[2]=1.0f; //set all shadow values to 1, fully lit no shadow as default
	
	if(bUseShadow1) //if shadow 1 is on, only shadow 1 will be filled, the rest just stay at one
	{
		if(Shadow1Soft !=0.0f) //if blurring is not turned off
		{
			float diffuse = saturate(dot(normal,lightsarray[0].LightVec)); //calculate diffuse for area light effect
			float blurscale = (Shadow1Soft* (1/(diffuse*diffuse*(1/(DistanceBlur)*5)))); // calculate blur scale based on diffuse term and global blur
			ShadowFloats[0] = ShadowLookUp(float4(In.worldSpacePos.xyz,1),In.worldTangent,In.worldBinormal,blurscale); // lookup shadows with special function
		}
		else
			ShadowFloats[0] = shadowTerm1(float4(In.worldSpacePos.xyz,1)); //if no blur, just do simple hard shadow lookup
			
		ShadowStrengths[0] = ShadowFloats[0] + ((1-Shadow1Strength) * (1-ShadowFloats[0])); //calculate final shadow strength based on user settings
	
	}
	
	
	//Light Loop
	float4 ambient;
	
		if(bUseIBLCubeMap && bUseIBL)
			ambient = texCUBElod(reflcubemapSampler, float4(normal.x,normal.z,normal.y,IBLBlur)) * IBLmultiplier;
		else if(bUseIBL && !bUseIBLCubeMap)
			ambient = texCUBElod(IBLcubemapSampler, float4(normal.x,normal.z,normal.y,IBLBlur)) * IBLmultiplier;
		else 
		{
			if(bUseAmbientMap)
				ambient = tex2D(ambientSampler, In.texCoord.xy);
			else
				ambient = float4(ambientcolor,1.0f);
		}
	
	float4 totaldiffuse =  ambient; //start off with ambient color
	float4 totalspecular = (0.0f,0.0f,0.0f,0.0f); //if the alpha value is not zero, messed up specular happens
	

	for(int i = 0; i < numberOfActiveLights; ++i) //for loop to iterate over our 3 lights
	{
		//diffuse term
		float diffuse;
		if(bUseHalfLambert) 
		{
			diffuse = halflambert(normal,lightsarray[i].LightVec);
			diffuse *= saturate(ShadowStrengths[i]+(1-(diffuselight(normal,lightsarray[i].LightVec)))); // apply shadow into diffuse, this is done for each light to prevent performance slowdown with IF statements due to varying instructioncount
			//this formula attempts to fix shadow bias issues with Half Lambert. the formula isn't perfect but it's better with than without...
		}
		else //regular blinn/phong diffuse
		{
			diffuse = diffuselight(normal,lightsarray[i].LightVec);
			diffuse *= ShadowStrengths[i]; // apply shadow into diffuse
		}
		
		totaldiffuse += (diffuse*lightsarray[i].LightColor); //add every light to the total diffuse sum (lights are additive towards each other), multiplied with light color 

		
		//specular term
		float4 specular = (1.0,1.0,1.0,1.0);
		specular *= blinnspecular(normal,lightsarray[i].LightVec, In.eyeVec, gloss);
		specular *= speccolor; //apply specular color map
		
		specular *= ShadowFloats[i]; // reduce specular by shadow, since you don't want specular where light can't go. we use raw specular value because this effect is absolute
		
		specular *= lightsarray[i].LightColor; //multiply by light color
		totalspecular += specular; //add every light specular to total specular level
	}
	totalspecular *= speclevel; // apply global specular multiplier to increase global strength
	
	
	//Apply Lighting
	
	float4 ret = color; // our final returned color starts as unlit diffuse color
	if(HueAdjust != 0.0f) //if Hue adjust is not off
	{
		float adjust = HueAdjust/10; //value is divided since it's not very userfriendly to work with 0.01 increments
		float3 rethsl = RGBToHSL(ret.rgb); //convert to HSL
		rethsl.x -= adjust*(1-totaldiffuse); //Shift Hue
		rethsl.y += adjust*(1-totaldiffuse); //increase saturation
		rethsl.z -= adjust*(1-totaldiffuse); //decrease lightness, not too sure if this is all correct, with the multiplication following this...
		ret.rgb = HSLToRGB(rethsl);
	}
	ret.rgb*=(totaldiffuse); //multiply diffuse with color
	ret+=totalspecular; // ADD specular to colored diffuse
	

	if(bUseLightMap)
    	ret.rgb *= LightMapStrength*tex2D(lightSampler, In.texCoord3.xy).rgb;
	
		
	//Edge effect
	
	if(bUseEdge)
	{
		//calculate dot for falloff
		float edge = fresnel(normal, In.eyeVec, 1.0f, 0.0f);
		edge = step(edge, EdgeStep);
		
		ret.rgb = lerp(EdgeColor, ret.rgb, edge);
	}
	
	if(bUseCubeMap && !bUseFresnel){
		float3 reflectmap = speccolor.rgb;
		
		if(bUseReflMap){
			
			if(bUseUnimap)
			  reflectmap.rgb = (1.0f-ReflectThreshold)*Unimap.g; 
			else
			  reflectmap = (1.0f-ReflectThreshold)*tex2D(ReflMapSampler, In.texCoord.xy).rgb; 
		}
			
			
		float3 ReflectVector = reflect(In.eyeVec, normal); 
		ReflectVector.xyz = ReflectVector.xzy;  
		ReflectVector.xyz = -ReflectVector.xyz;
	
		float3 reflcubemap = texCUBE(reflcubemapSampler, ReflectVector ); 
		
	
		ret.rgb += reflectmap*reflcubemap*CubeIntensityLevel;
	}
	
	//Self Illumination
	
	if(bUseSIMap)
	{
		//get total SI amount by adding (desaturated) map contribution to global level and the clamping between 0 and 1
		float SItotal = saturate(desaturate(selfillumination.rgb) + (siLevel/100)); 
		
		ret = lerp( ret, color * siMapMult,  SItotal); //lerp blend between normal shaded model and unlit multiplied by SImapmultiplier. no color contribution of glowmap yet....
	}
	
	
	
	ret.a = tex2D(diffuseSampler, In.texCoord.xy).a; // finally set opacity

	return ret; //all done, return result :)
} 


//Fresnel effects PS pass function. Main reason for split is to avoid hitting the dynamic branching limit
float4 ps_fresnel(VS_To_PS_Struct In) : COLOR 
{ 
	float4 ret =(0,0,0,0);
	
	//alpha
	float opacity = GlobalOpacity; //default is global opacity
	if(bUseAlpha)
	{
		opacity = tex2D(diffuseSampler, In.texCoord.xy).a; //sampled alpha from diffuse map
	}
	
	//specular
	float3 speccolor = specularColor.rgb; //default is solid specular color
	if(bUseSpecMap)
	{
		speccolor = tex2D(specularSampler, In.texCoord.xy).rgb;
	}
	
	float3 Unimap = 0.0f;
	
	if(bUseUnimap)
		Unimap = tex2D(UniMapSampler, In.texCoord.xy).rgb;
	
	//glossiness
	float gloss = 1.0f;
	float glosscorrect = 1.0f;
	
	if(bUseGlossMap){
	
		if(bUseUnimap){
			gloss =	desaturate(Unimap.r);
			glosscorrect += (glossoffset/glossiness);
		}else{
			if(bUseReflGloss)
			{
				gloss = desaturate(tex2D(glossinessSampler, In.texCoord.xy)); //overwrite glossiness global with texsample
				glosscorrect = (glossoffset/glossiness);
			}
		}
	
	}
	
	
	
	//normal
	float3 normal = In.worldNormal; //default worldspace vertex normal
	if(bUseNormalMap)
	{
		normal = tex2D(normalSampler, In.texCoord2.xy).rgb;
		normal = (2*normal)-1; //expand into -1 to 1 range, since a normalmap is always returned in 0 to 1 space
		if (bFlipGreenChannel) normal.g =-normal.g; //flip green for texture instead of flipping tangent, works for object space normals also
		/*
		if (bFlipRG) 
		{
			float temp = normal.r;
			normal.r = normal.g;
			normal.g = temp;
		}*/
	}
	
	//reflection
	float3 reflectmap = speccolor.rgb; //use desaturated specmap for reflections if reflectmap is not enabled
	
	if(bUseReflMap){
	
		if(bUseUnimap)
			reflectmap.rgb = Unimap.g;
		else
			reflectmap = (1.0f-ReflectThreshold)*tex2D(ReflMapSampler, In.texCoord.xy).rgb; 
	
	}
		
	
	//normal application
	if(!bUseObjectNormals && bUseNormalMap) //tangentspace normalmap
	{
		normal = (In.worldNormal * normal.z) + (normal.x * In.worldBinormal + normal.y * -In.worldTangent); //transform every vertex normal component by the tangentspace normalmap
	}
	if(bUseObjectNormals && bUseNormalMap) //if object space, pure sampling equals the normal
	{
		normal = mul(normal, WorldInvTrans).xyz; // put objectspace normal in worldspace
	}
	
	if(bUseFresnel)
	{
		float3 reflectionamount = reflectmap * fresnel(normal, In.eyeVec, FresnelPower, FresnelBias); //start off by getting fresnel falloff
		//reflectmap *= reflectionamount; //mask reflectmap by fresnelrim, if no reflectmap is used, the value will be the specmap.
		
		float3 ReflectVector = reflect(In.eyeVec, normal); //compute reflection vector
		ReflectVector.xyz = ReflectVector.xzy;  //swizzle required for Max 
	
		float worldmask =1.0f;
		if(!bUseWorldMask)
		{
			worldmask = 1 - saturate(dot(float3(0.0,1.0,0.0),-normal.xzy)); // invert, clamped dot product of normal and upward vector => only normals pointing upward will work
			worldmask = pow(worldmask, FresnelMaskHardness); // power to hardness factor to control effect. 0 equals no control
		}
		else
		{
			//hard type masking activated for reflection faking
			worldmask = (1 - saturate(dot(float3(0.0,(1.0/FresnelMaskHardness /*<=this softens the effect*/),0.0),ReflectVector))); // invert, clamped dot product of reflection and upward vector => only reflections tracing upward will work
			float limit =  saturate(dot(float3(0.0,1,0.0),normal.xzy)); //prevent top from reflecting,m effectively creating a studio HDRi in code only.
			worldmask -= saturate(pow(limit,0.8)); //subtract top mask from reflect mask, powered to 0.8 to soften the effect a bit
		}
		
		//final lerps, to blend between fresnel effect and lit color
		if(!bUseCubeMap) //if no cubemap, fake fresnel rim used
			ret.rgb = float4(FresnelColor,0.0f)*FresnelMult * reflectionamount * worldmask; //fresnel color masked by specmap, masked by worldmask, multiplied by intensity
		if(bUseCubeMap)
		{
			ReflectVector.yz = -ReflectVector.yz; //invert reflectionvector for cubemap sampling
			float reflectiongloss = lerp((CubeMapBlur*glosscorrect),CubeMapBlur,gloss); //calculate final reflection blur factor by rescaling gloss to max blur range
			float3 reflcubemap = texCUBElod(reflcubemapSampler, float4(ReflectVector.x,ReflectVector.y,ReflectVector.z,reflectiongloss)); //sample from cubemap with reflection vector, LOD forced by reflection blur factor
																		//NOTE: using texCUBElod with ( ReflectVector.xyz, LODlevel) DOES NOT WORK AS IT SHOULD, split in components to fix ~0_o>
				ret.rgb = float4(FresnelMult*reflcubemap*reflectionamount,0); //add cubemap, multiplied with fresnel multiplier, masked by fresnel
		}
		
		if(bUseFresnelMap){
			
			if(bUseUnimap)
				ret.rgb *= Unimap.b;
			else
				ret.rgb *= tex2D(FresMapSampler, In.texCoord.xy).rgb;
		
		}		
	}
	
	if(bUseEdge)
	{
		//calculate dot for falloff
		float edge = fresnel(normal, In.eyeVec, 1.0f, 0.0f);
		edge = step(edge, EdgeStep);
		
		ret.rgb *= edge;
	}
	
	if(bAlphaMasksFresnel) ret.rgb *= opacity; //if opacity is used, you can reduce the fresnel effects strength to match the opacity. The is off by default since glass effects are better like this

	return ret;
}

float4 ps_glowmask(VS_To_PS_Struct In) : COLOR 
{ 
	float4 ret =(0,0,0,0);
	
	//self illumination
	float4 selfillumination = (0.0f,0.0f,0.0f,0.0f); //default no self illum
	if(bUseSIMap)
	{
		selfillumination = tex2D(siMapSampler, In.texCoord.xy);
	}
	
	ret.rgb = saturate(selfillumination.rgb + (siLevel/100));

	return ret;
}


technique Default  
{  
	pass  P0
    {		 
		ZEnable = true; 
		ZWriteEnable = true; 
		CullMode = none; //clockwise culling
		AlphaBlendEnable = true; //alphablend allows for full range, smooth opacity masking
		AlphaTestEnable = false;
		VertexShader = compile vs_3_0 vs_main( );
		PixelShader = compile ps_3_0 ps_main( );
	}  
	pass P1
	{		 
		ZEnable = true; 
		ZWriteEnable = true; 
		CullMode = none; //clockwise culling
		AlphaBlendEnable = true; //alphablend allows for full range, smooth opacity masking
		AlphaTestEnable = false; 
		SrcBlend = One; // additive blening of fresnel stuff
		DestBlend = One;
		VertexShader = compile vs_3_0 vs_main( );
		PixelShader = compile ps_3_0 ps_fresnel( );
	}

} 

technique TwoSided  
{  
	pass  P0
    {		 
		ZEnable = true; 
		ZWriteEnable = true; 
		CullMode = none; //clockwise culling
		AlphaBlendEnable = true; //alphablend allows for full range, smooth opacity masking
		AlphaTestEnable = false;
		VertexShader = compile vs_3_0 vs_main( );
		PixelShader = compile ps_3_0 ps_main( );
	}  
	pass P1
	{		 
		ZEnable = true; 
		ZWriteEnable = true; 
		CullMode = none; //clockwise culling
		AlphaBlendEnable = true; //alphablend allows for full range, smooth opacity masking
		AlphaTestEnable = false; 
		SrcBlend = One; // additive blening of fresnel stuff
		DestBlend = One;
		VertexShader = compile vs_3_0 vs_main( );
		PixelShader = compile ps_3_0 ps_fresnel( );
	}

} 
/*
technique Glow
<
	string script = 
			"RenderColorTarget0=ModelMap;"
			"RenderDepthStencilTarget=DepthTex;"
			"ClearSetColor=ClearColor;"
			"ClearSetDepth=ClearDepth;"
			"ScriptSignature=color;"
			"Pass=Model;"				
			"Pass=Reflections;"
			"Pass=GlowMask;";				
>   
{  
	pass  Model
	<
	   	string Script= "RenderColorTarget0=;"
						"Draw=Geometry;";
    >

    {		 
		ZEnable = true; 
		ZWriteEnable = true; 
		CullMode = none; //clockwise culling
		AlphaBlendEnable = true; //alphablend allows for full range, smooth opacity masking
		AlphaTestEnable = false;
		VertexShader = compile vs_3_0 vs_main( );
		PixelShader = compile ps_3_0 ps_main( );
	}  
	pass Reflections
	<
       	string Script= "RenderColorTarget0=;"
			//"Clear=Color;"
			"Draw=Geometry;";        	
    >
	{		 
		ZEnable = true; 
		ZWriteEnable = false; 
		CullMode = none; //clockwise culling
		AlphaBlendEnable = true; //alphablend allows for full range, smooth opacity masking
		AlphaTestEnable = false; 
		SrcBlend = One; // additive blening of fresnel stuff
		DestBlend = One;
		VertexShader = compile vs_3_0 vs_main( );
		PixelShader = compile ps_3_0 ps_fresnel( );
	}
	
	pass GlowMask
	<
       	string Script= "RenderColorTarget0=GlowMap;"
						"Clear=Color;"
						"Clear=Depth;"
						"Draw=Buffer;";        	
    >
	{		 
		ZEnable = true; 
		ZWriteEnable = false; 
		CullMode = none; //clockwise culling
		AlphaBlendEnable = true; //alphablend allows for full range, smooth opacity masking
		AlphaTestEnable = false; 
		SrcBlend = One; // additive blening of fresnel stuff
		DestBlend = One;
		VertexShader = compile vs_3_0 vs_main( );
		PixelShader = compile ps_3_0 ps_glowmask( );
	}

} */


