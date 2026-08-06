/*
 * Embedded stock CoD2 assets for built-in engine materials.
 * Used on web when IWD lookup fails (stale OPFS cache, partial install, index miss).
 * Source: iw_00.iwd / iw_01.iwd from trimmed MP web pack (stock CoD2 content).
 */
#include "common_types.h"
#include "web/builtin_engine_assets.h"

static const char s_colorChannelMixerTech[] =
    "{\r\n"
    "\tstateMap \"nofog\";\r\n"
    "\r\n"
    "\tvertex.position = code.position;\r\n"
    "\tvertex.color[0] = code.color;\r\n"
    "\tvertex.texcoord[0] = code.texcoord[0];\r\n"
    "\r\n"
    "\tvertexShader 2.0 \"color_channel_mixer.hlsl\"\r\n"
    "\t{\r\n"
    "\t}\r\n"
    "\r\n"
    "\tpixelShader 2.0 \"color_channel_mixer.hlsl\"\r\n"
    "\t{\r\n"
    "\t\tcolorMapSampler = sampler.feedback;\r\n"
    "\t\tmaterialColor = constant.materialColor;\r\n"
    "\t}\r\n"
    "}\r\n";

static const char s_colorChannelMixerTechset[] =
    "\"unlit no fog\":\r\n"
    "\"unlit linear fog\":\r\n"
    "\"unlit exp fog\":\r\n"
    "\tcolor_channel_mixer;\r\n";

static const char s_globalsHlsl[] =
    "float4x4 worldViewProj;\r\n"
    "float4 materialColor;\r\n"
    "sampler colorMapSampler;\r\n";

static const char s_transformHlsl[] =
    "float4 Transform_ObjectToClip(float4 pos)\r\n"
    "{\r\n"
    "\treturn mul(worldViewProj, pos);\r\n"
    "}\r\n";

static const char s_colorChannelMixerHlsl[] =
    "#include \"lib/globals.hlsl\"\r\n"
    "\r\n"
    "struct VertexInput\r\n"
    "{\r\n"
    "\tfloat4 position : POSITION;\r\n"
    "\tfloat2 texCoords : TEXCOORD0;\r\n"
    "};\r\n"
    "\r\n"
    "\r\n"
    "struct PixelInput\r\n"
    "{\r\n"
    "\tfloat4 position : POSITION;\r\n"
    "\tfloat2 texCoords : TEXCOORD0;\r\n"
    "};\r\n"
    "\r\n"
    "\r\n"
    "struct PixelOutput\r\n"
    "{\r\n"
    "\tfloat4 color : COLOR;\r\n"
    "};\r\n"
    "\r\n"
    "\r\n"
    "#include \"lib/transform.hlsl\"\r\n"
    "\r\n"
    "\r\n"
    "PixelInput vs_main( const VertexInput vertex )\r\n"
    "{\r\n"
    "\tPixelInput pixel;\r\n"
    "\r\n"
    "\tpixel.position = Transform_ObjectToClip( vertex.position );\r\n"
    "\tpixel.texCoords = vertex.texCoords;\r\n"
    "\r\n"
    "\treturn pixel;\r\n"
    "}\r\n"
    "\r\n"
    "\r\n"
    "PixelOutput ps_main( const PixelInput pixel )\r\n"
    "{\r\n"
    "\tPixelOutput fragment;\r\n"
    "\r\n"
    "\tfloat4 diffuseColor = tex2D( colorMapSampler, pixel.texCoords );\r\n"
    "\r\n"
    "\tfragment.color.r = diffuseColor.r * materialColor.r + diffuseColor.a * materialColor.a;\r\n"
    "\tfragment.color.g = diffuseColor.g * materialColor.g + diffuseColor.a * materialColor.a;\r\n"
    "\tfragment.color.b = diffuseColor.b * materialColor.b + diffuseColor.a * materialColor.a;\r\n"
    "\tfragment.color.a = diffuseColor.a;\r\n"
    "\r\n"
    "\treturn fragment;\r\n"
    "}\r\n";

static const byte s_colorChannelMixerMaterial[] = {
    0x58, 0x00, 0x00, 0x00, 0x6c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x12, 0x88, 0x12, 0x28, 0x03,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00,
    0x44, 0x00, 0x00, 0x00, 0x63, 0x6f, 0x6c, 0x6f, 0x72, 0x5f, 0x63, 0x68, 0x61, 0x6e, 0x6e, 0x65,
    0x6c, 0x5f, 0x6d, 0x69, 0x78, 0x65, 0x72, 0x00, 0x63, 0x6f, 0x6c, 0x6f, 0x72, 0x5f, 0x63, 0x68,
    0x61, 0x6e, 0x6e, 0x65, 0x6c, 0x5f, 0x6d, 0x69, 0x78, 0x65, 0x72, 0x00, 0x24, 0x77, 0x68, 0x69,
    0x74, 0x65, 0x00,
};

static const WebBuiltinAsset s_webBuiltinAssets[] = {
    { "materials/techniques/color_channel_mixer.tech", (const byte *)s_colorChannelMixerTech,
      sizeof(s_colorChannelMixerTech) - 1 },
    { "materials/techniquesets/color_channel_mixer.techset", (const byte *)s_colorChannelMixerTechset,
      sizeof(s_colorChannelMixerTechset) - 1 },
    { "materials/shaders/color_channel_mixer.hlsl", (const byte *)s_colorChannelMixerHlsl,
      sizeof(s_colorChannelMixerHlsl) - 1 },
    { "materials/shaders/lib/globals.hlsl", (const byte *)s_globalsHlsl, sizeof(s_globalsHlsl) - 1 },
    { "materials/shaders/lib/transform.hlsl", (const byte *)s_transformHlsl, sizeof(s_transformHlsl) - 1 },
    { "materials/color_channel_mixer", s_colorChannelMixerMaterial, sizeof(s_colorChannelMixerMaterial) },
};

static int FS_WebBuiltinPathCompare(const char *a, const char *b)
{
    unsigned char ca;
    unsigned char cb;

    do {
        ca = (unsigned char)tolower(*a++);
        cb = (unsigned char)tolower(*b++);
        if (ca == '\\') {
            ca = '/';
        }
        if (cb == '\\') {
            cb = '/';
        }
        if (ca != cb) {
            return (int)ca - (int)cb;
        }
    } while (ca && cb);

    return 0;
}

const WebBuiltinAsset *FS_WebFindBuiltinAsset(const char *qpath)
{
    int i;

    if (!qpath || !qpath[0]) {
        return NULL;
    }

    for (i = 0; i < (int)(sizeof(s_webBuiltinAssets) / sizeof(s_webBuiltinAssets[0])); i++) {
        if (!FS_WebBuiltinPathCompare(s_webBuiltinAssets[i].path, qpath)) {
            return &s_webBuiltinAssets[i];
        }
    }

    return NULL;
}

qboolean FS_WebIsEngineBuiltinPath(const char *qpath)
{
    return FS_WebFindBuiltinAsset(qpath) != NULL;
}
