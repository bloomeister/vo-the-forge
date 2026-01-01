# **Forge Shader Language 2 (FSL 2) Guide**

## **Introduction**

Modern games are aiming to launch on as many platforms as possible at once to reduce the cost of art asset and gameplay development. 

To streamline the development of shaders, a cross-platform shader system is necessary to compile and run on a wide range of platforms originating from one shader source file.

The Forge Shader Language (FSL 2) is a system that can generate shaders for PC, macOS, iOS/Android, all major consoles (XBox, Playstation, Switch), and Steam Deck platforms.

The FSL translator is an external tool to existing compilers such as DXC and other platform specific compilers.

Due to the on-going lack of reliability of DXC and SPIR-V code generation and translations, existing complex compiler based systems are lacking the practical ability to cross-compile or translate shaders.

Even the basic compilation of native shaders with these shader compilers is error prone and quite often generates inefficient code.

To be able to circumvent broken and inefficient compilers, the FSL translator can be extended quickly to support other compilers and platforms.

Integrating a new platform with a new native shader compiler or adding support for newly released features to existing platforms can be done with very little effort, compared to an approach of adding a new shader language/feature to DXC and/or SPIR-V.

FSL 2 is designed to be integrated into existing build systems, and can invoked as a pre build step, which can be integrated in visual studio or xcode or any build system. FSL IDE integration is implemented on projects inside of TheForge.

### **Manual Cross-Platform Shader Maintenance**

When targeting multiple platforms for game development, it is crucial to share as much CPU and GPU code between platforms as possible.

Shader code bases can quickly bloat into a sizeable number of source files. 

Maintaining separate shaders to target different languages can be achieved for small projects to a small extent, but quickly the overhead of keeping separate implementations in sync is not justifiable nor practical.

### **System overview**

The purpose of FSL 2 is to allow the creation of a single shader source file that is used to generate native hlsl/pssl/vk-glsl/metal shader code and can also seamlessly integrate with C++, reducing the iteration time and overall development cost for all platforms.

> FSL 2 supports vertex, pixel, and compute shaders.

The Forge Framework alongside FSL 2 unifies the root signature, eliminating the need to manage them from C++.

There is only one graphics and one compute root signature shared across all shaders in a given project. This improves performance by reducing the number of costly runtime root signature switches and avoids the complexity of multiple shader pipelines having to manage their own root signatures. It also allows the same descriptor sets to be shared between different graphics and compute pipelines.

This unified approach streamlines the shader resource management process, improving both performance and code maintainability.

A central enhancement in FSL 2 over FSL is the introduction of the global Shader Resource Table (SRT), which eliminates the need for runtime shader reflection and facilitates the efficient linkage of shader resources to C++ code by declaring resources in a consistent manner for both shader code and C++ code.

This guide is explores FSL 2, the underlying principles of the SRT system, how resources are declared in FSL, and how these resources are integrated with C++ code.

Will also explain the macros that drive this system and highlight an advanced use case demonstrating how these features can be leveraged for complex shader workflows.

## FSL 2 Syntax

FSL 2 syntax is generally kept close to hlsl, with some modifications added to top to increase functionality.

We define a range of macros to both ease code transformation and let the preprocessor handle the necessary modifications.

>FSL 2 macros are all uppercase letter separated by underscores

>Macro usage should span a single line.

### Root signature declarations

Explicit declaration of root signatures for graphics or compute are required preceeding shader entry points using the macro : ROOT_SIGNATURE, which needs to be passed in a name

```
ROOT_SIGNATURE(DefaultRootSignature)
ROOT_SIGNATURE(ComputeRootSignature)
```

**ROOT_SIGNATURE** : macro to declare a root signature.

*DefaultRootSignature* / *ComputeRootSignature* : variable names given to the root signatures.


### Shader entry

The shader entry point is the main function that defines the input and output interface. It requires annotations and needs to be referenced at runtime during pipeline state object creation. 

Entry points are declared using the macros:

**VS_MAIN** : for vertex shader

**PS_MAIN** : for pixel shader

**CS_MAIN** : for compute shader

The first line in the main function body should be:
```
INIT_MAIN;
```
The translation step replaces **INIT_MAIN** and inserts in its place target language specific initialization.

At the end main function, the **RETURN** macro is used.

Example:

```
RETURN(); // for void main function
float4 Out = (...);
RETURN(Out); // for main function with return type
```

The translation steps expands the **RETURN** macro to insert target language specific exit code.

Shaders which return a result need to return it as paramater to the **RETURN** macro.

Shaders which do not return a value need to have the **RETURN** macro without parameters as last line.

### Parameter Modifiers

FSL 2 supports the usual **in**, **out** and **inout** type modifiers, to allow pass by reference semantics

```
void fn( in(float) param_in,  out(float) param_out,  inout(float) param_inout ) 
{
}
```

In this example the function *fn*, takes as parameter the float *param_in* as read only input, writes a value to the float *param_out*, and can read and write to the float *param_inout*

### Vectors and Matrices

FSL 2 matrices are column major.

Matrices declared inside constant buffers, or structure buffers are initialized from memory in column major order.  

Explicit constructors and accessors are provided.

Example:

```
f3x2 M = make_f3x2_rows(r0, r1, r2); 
```

Initializes a 3 cols by 2 rows matrix *M* from three 2-component rows.

```
setElem(M, 0, 1, 42.0f); 
```
Sets the element from matrix *M* at col 0, row 1 to 42

```
float3 col0 = getCol0(M);
float2 row1 = getRow1(M);
```
Gets column 0 from matrix *M*  into the 3 component vector *col0* and row 1 from matrix *M*  into 2 component vector *row1*

```
f2x2 M = make_f2x2_rows( 0, 1,
                         2, 3 );
```
Create a 2x2 matrix *M* from the scalars 0, 1, 2, 3 provided in row-major order

We also provide overloaded Identity constructors and helpers to initialize vectors with identical components:

Examples:

```
f4x4 id = Identity();
```
Creates a 4x4 matrix *id* and initializes it as an Identity

```
float4 color = f4(1); 
```
Create a vector of 4 floats named *color*, initializing all them to 1.

### **Shader Resource Table (SRT)**

The **Shader Resource Table (SRT)** in FSL 2 serves as a global registry that defines all the shader resources for an application.
These resources are grouped into sets, each corresponding to how often they get updated, and this is calculated at compile time.

The SRT system enables a consistent method for declaring and managing resources, and eliminates the need for overhead of runtime reflection.

A typical SRT consists of multiple resource sets, each corresponds to its update frequency:

1. **Persistent Set**: Resources that do not change during the lifetime of the application.
2. **Per Frame Set** : Resources that are updated once per frame.
3. **Per Batch Set** : Resources that are updated for each batch of draw calls.
4. **Per Draw Set**  : Resources that are updated for each individual draw call.

#### SRT Macros

- **BEGIN_SRT(TableName) / END_SRT(TableName)**  : marks the beginning and end of a shader resource table, name of the SRT should be passed as parameter
- **BEGIN_SRT_SET(Freq) / END_SRT_SET(Freq)**   : marks the beginning and end of a shader resource set, Freq is the name of the set which may correspond to how often it may be updated

#### Resource declarations

declaring resources inside the SRT set is done a set of macros which takes the general form:

```
DECL_XXX( SetName, Type, Name )
DECL_ARRAY_XXX( SetName, Type, Name, Count )
```

- ***SetName*** : name of the enclosing set in which this resource is being declared.
- ***Type***    : type of the resource, which can be sampler, buffer, constant buffer, structured buffer or a texture
- ***Name***    : variable name of the resource, used on shaders and C++ side interchangeably
- ***Count***   : For array resources (Buffers of Textures) the count param defines the number of resources in the array


This table lists the SRT macros and their possible values for parameters:


| Macro         | Type                      | DataType  | Array  |    Notes                |
|---------------|-----------------------------------|-----------|--------|-------------------------|
| DECL_SAMPLER  | SampleState / SamplerComparisonState| N/A       |   No   | First set only|
| DECL_CBUFFER  | CBUFFER(DataType)                   | struct | No ||
| DECL_BUFFER   | ByteBuffer/Buffer(DataType) | uint/float/float4/struct | No ||
| DECL_RWBUFFER   | RWByteBuffer/RWBuffer(DataType)/RWCoherentBuffer(DataType) | uint/float/float4/struct | No |Read-Write|
| DECL_TEXTURE  | Tex1D / Tex2D / Tex2DMS / Tex2DArray / Tex3D / TexCube / Depth2D / Depth2DMS   | uint/float/float2/float3/float4  |   No   |  |
| DECL_RWTEXTURE|RWTex2D/RWTex2DArray/RTex2D/RWTex3D/RTex3D|uint/float/float2/float3/float4  | No | Read-Write|
| DECL_ARRAY_BUFFERS|ByteBuffer/Buffer(DataType) | uint/float/float4/struct| Yes ||
| DECL_ARRAY_TEXTURES|Tex1D / Tex2D / Tex2DMS / Tex2DArray / Tex3D / TexCube / Depth2D / Depth2DMS |uint/float/float2/float3/float4  | Yes ||
| DECL_ARRAY_RWTEXTURES|Tex1D / Tex2D / Tex2DMS / Tex2DArray / Tex3D / TexCube / Depth2D / Depth2DMS |uint/float/float2/float3/float4 | Yes |Read-Write|
| DECL_ARRAY_RWBUFFERS| RWByteBuffer/RWBuffer(DataType)/RWCoherentBuffer(DataType) | uint/float/float4/struct | Yes |Read-Write|

**Samplers** :

Samplers are declared inside the SRT using the macro **DECL_SAMPLER** :

```
DECL_SAMPLER(SetName, Type, Name)
```
- ***SetName*** : name of the enclosing set
- ***Type***    : type of the sampler, which can be **SamplerState** or **SamplerComparisonState**
- ***Name***    : variable name of the sampler

Example :

```
DECL_SAMPLER(Persistent, SamplerComparisonState, gShadowSampler)
```

This declares the sampler named *gShadowSampler*, which is a comparison sampler used for shadow mapping, and it is in the persistent set

>Samplers can only be declared inside the first set in the SRT.

**Textures** :

Textures are declared inside the SRT using the macro **DECL_TEXTURE** :

```
DECL_TEXTURE(SetName, TextureType(PixelDataType), Name)
```
- ***SetName***       : Name of the enclosing set.
- ***TextureType***   : Type of the textures, which can be : **Tex1D** / **Tex2D** / **Tex2DArray** / **Tex3D**
- ***PixelDataType*** : Type of each pixel in the textures which can be **uint** / **half** / **float** / **float2** / **float3** / **float4**.
- ***Name***          : Name of the texture.

Textures types map to hlsl `Texture#D` types, glsl `texture#D` and metal `texture#d<T, access::sample>` types.  


Examples:

```
DECL_TEXTURE(PerDraw, Tex2D(float4), gNormalMap)
```
This declares the 2D texture *gNormalMap*, which can change per draw call.

```
DECL_TEXTURE(Persistent, TexCube(float4), gSkybox)
```
This declares the persistent cube texture *gSkyBox*.


**Constant Buffers**:

Constant buffers are declared inside the SRT using the macro **DECL_CBUFFER** :

```
DECL_CBUFFER(SetName, CBUFFER(StructType), Name)
```

- ***SetName***       : Name of the enclosing set.
- ***StructType***    : name of the struct type used for the constant buffer, the struct type should be declared on top of the SRT.
- ***Name***          : Name of the constant buffer.

Example :

```
DECL_CBUFFER(PerFrame, CBUFFER(PointLight), gLight)
```

This declares a constant buffer *gLight*, which is of the structure type PointLight, and is updated per frame

**Buffers**

Read only buffers are declared inside the SRT using the macro **DECL_BUFFER** :

```
DECL_BUFFER(SetName, BufferType(DataType), Name)
```

- ***SetName***       : Name of the enclosing set.
- ***BufferType***    : Type of the buffer, which can be : *ByteBuffer* or *Buffer* with a data type.
- ***DataType***      : For non byte buffers, this can be the data type of element of the buffer, which can be a basic data type *int/uint/float* or a struct type.
- ***Name***          : Name of the buffer.

Example :

```
DECL_BUFFER(PerFrame, Buffer(uint), gLightClusters)
```

This declares the buffer *gLightClsuters* which consists of unisgned integers and is part of the set of resources which get updated on a per frame basis.

**Read-Write Textures** :

Read-Write Textures are declared inside the SRT using the macro **DECL_RWTEXTURE** :

```
DECL_RWTEXTURE(SetName, TextureType(PixelDataType), Name)
```
- ***SetName***       : Name of the enclosing set
- ***TextureType***   : Type of the textures, which can be : *RWTex2D* / *RWTex3D* / *RWTex2DArray* / *RWTex1D*
- ***PixelDataType*** : Type of each pixel in the textures which can be *float* / *float2* / *float3* / *float4* / *uint*
- ***Name***          : Name of the texture

>Read-Write textures internally map to hlsl `RWTexture#D` types, glsl `image#D` types and metal `texture#d<T, access::read_write>` types.

Example:

```
DECL_RWTEXTURE(PerDraw, RWTex2D(float), gDestinationTexture)
```
This declares the 2D read-write texture *gDestinationTexture*, which can change per draw call, and stores a single float per pixel.

**Read-write Buffers**

Read-write buffers are declared inside the SRT using the macro **DECL_RWBUFFER** :

```
DECL_RWBUFFER(PerFrame, BufferType(ElementType), Name)
```

- ***SetName***       : Name of the enclosing set.
- ***BufferType***    : Type of the buffer, which can be : *RWByteBuffer* or *RWBuffer* with a data type.
- ***ElementType***   : For non byte buffers, this can be the data type of element of the buffer, which can be a basic data type *int/uint/float* or a struct type.
- ***Name***          : Name of the buffer.

Example :

```
DECL_RWBUFFER(PerBatch, RWBuffer(uint), gIndirectDrawClearArgsRW)
```

This declares the read-write buffer *gIndirectDrawClearArgsRW*, which can change on a per batch basis, and stores a single unisgned integer per element.


**Arrays of textures** :

Arrays of textures are declared inside the SRT using the macros **DECL_ARRAY_TEXTURES**:

```
DECL_ARRAY_TEXTURES(SetName, TextureType(PixelDataType), Name, Count)
```
- ***SetName***       : Name of the enclosing set.
- ***TextureType***   : Type of the textures, which can be : *Tex1D* / *Tex2D* / *Tex3D*.
- ***PixelDataType*** : Type of each pixel in the textures which can be *uint* / *half* / *float* / *float2* / *float3* / *float4*/
- ***Name***          : Name of the array of textures.
- ***Count***         : Number of textures in the array.

Example:

```
DECL_ARRAY_TEXTURES(Persistent, Tex2D(float4), gDiffuseMaps, 256)
```
This declares the array of 256 2D textures named *gDiffuseMaps*, which is persistently bound and does not change.



**Arrays of read-write textures** :

Arrays of textures are declared inside the SRT using the macros **DECL_ARRAY_RWTEXTURES**:

```
DECL_ARRAY_RWTEXTURES(SetName, TextureType(PixelDataType), Name, Count)
```
- ***SetName***       : Name of the enclosing set.
- ***TextureType***   : Type of the textures, which can be : *RWTex2D* / *RWTex3D* / *RWTex2DArray* / *RWTex1D*
- ***PixelDataType*** : Type of each pixel in the textures which can be *uint* / *half* / *float* / *float2* / *float3* / *float4*.
- ***Name***          : Name of the array of textures.
- ***Count***         : Number of textures in the array.

Example:

```
DECL_ARRAY_RWTEXTURES(PerDraw, RWTex2D(float4), gShadowMapTextures, 2)
```
This declares a array of 2 2D textures named *gShadowMapTextures*, which can be changed on a per draw basis


**Arrays of read-write buffers**

Read-write buffers are declared inside the SRT using the macro **DECL_ARRAY_RWBUFFERS** :

```
DECL_ARRAY_RWBUFFERS(PerFrame, BufferType(ElementType), Name, Count)
```

- ***SetName***       : Name of the enclosing set.
- ***BufferType***    : Type of the buffer, which can be : *RWByteBuffer* or *RWBuffer* with a data type.
- ***ElementType***   : For non byte buffers, this can be the data type of element of the buffer, which can be a basic data type *int/uint/float* or a struct type.
- ***Name***          : Name of the array of Buffers.
- ***count***         : Number of buffers in the array.

Example :

```
DECL_ARRAY_RWBUFFERS(PerBatch, RWByteBuffer, gFilteredIndicesBufferRW, 2)
```

This declares an array of 2 read-write raw byte buffers named *gFilteredIndicesBufferRW*, which is updated per batch of draw or dispatch calls.


#### SRT Example 1:
Here’s an example of a basic SRT declaration:

```
BEGIN_SRT(SrtData)
    BEGIN_SRT_SET(Persistent)
        DECL_TEXTURE(Persistent, Tex2D(float4), gRightTexture)
        DECL_TEXTURE(Persistent, Tex2D(float4), gLeftTexture)
        DECL_TEXTURE(Persistent, Tex2D(float4), gTopTexture)
        DECL_TEXTURE(Persistent, Tex2D(float4), gBotTexture)
        DECL_TEXTURE(Persistent, Tex2D(float4), gFrontTexture)
        DECL_TEXTURE(Persistent, Tex2D(float4), gBackTexture)
        DECL_SAMPLER(Persistent, SamplerState, gSampler)
    END_SRT_SET(Persistent)
    BEGIN_SRT_SET(PerFrame)
        DECL_CBUFFER(PerFrame, CBUFFER(UniformData), gUniformBlock)
    END_SRT_SET(PerFrame)
END_SRT(SrtData)
```

**BEGIN_SRT(SrtData)/BEGIN_SRT(SrtData)**: These macros mark the beginning and end of the SRT, the name *SrtData* is used in C++ code to reference the resources and sets

**BEGIN_SRT_SET(Persistent)**/**END_SRT_SET(Persistent)**: These macros mark the beginning and end of the persistent set of resources, Which consists of a sampler and six textures used for skybox sides.

**BEGIN_SRT_SET(PerFrame)**/**END_SRT_SET(PerFrame)**: These macros mark the beginning and end of the set of resources that are updated per frame, which consists of constant buffer of type UniformData, named gUniformBlock.


#### SRT Example 2: Using Shared Sets of resources for Multiple SRTs

Another SRT example demonstrates how the system can handle advanced scenarios.

In this case, a persistent set and a per frame updated set are declared in separate header files, 
this way multiple SRTs can include these shared descriptor sets.

This approach allows us to reduce the number of descriptor sets across the project by having only the third and fourth sets varying in each SRT.

```
BEGIN_SRT(SrtHairData)
    #include "PersistentSet.h"
    #include "PerFrameSet.h"
    BEGIN_SRT_SET(PerBatch)
        DECL_ARRAY_TEXTURES(PerBatch, Tex2D(float), gHairDirectionalLightShadowMaps, MAX_NUM_DIRECTIONAL_LIGHTS)
        DECL_CBUFFER(PerBatch, CBUFFER(DirectionalLightShadowCameras), gHairDirectionalLightShadowCameras)
    END_SRT_SET(PerBatch)
    BEGIN_SRT_SET(PerDraw)
        DECL_CBUFFER(PerDraw, CBUFFER(HairData), gHair)
        DECL_BUFFER(PerDraw, Buffer(float4), gGuideHairVertexPositions)
        DECL_BUFFER(PerDraw, Buffer(float), gHairThicknessCoefficients)
        DECL_RWBUFFER(PerDraw, RWBuffer(float4), gHairVertexTangents)
        DECL_RWBUFFER(PerDraw, RWBuffer(uint), gDepthsBuffer)
        DECL_RWTEXTURE(PerDraw, RWTex2DArray(uint), gDestDepthsTexture)
    END_SRT_SET(PerDraw)
END_SRT(SrtHairData)
```

**BEGIN_SRT(SrtHairData)/END_SRT(SrtHairData)**: These macros enclose the begin and end of the SRT named *SrtHairData*.

**#include "PersistentSet.h"**: Include the definition of persistent resources used across multiple SRTs in the project.

**#include "PerFrameSet.h"**:   Include the definition of resources which get updated per frame and are used across multiple SRTs in the project.

**BEGIN_SRT_SET(PerBatch)/END_SRT_SET(PerBatch)**: 
These macros mark the beginning and end of the set of resources that are updated per batch of draw calls.

Which consists of:

- **gHairDirectionalLightShadowMaps**    : An array of textures used to store shadow maps for hair, used with the directional lights used in the scene.
- **gHairDirectionalLightShadowCameras** : A constant buffer which contains the cameras used for the shadow maps.


**BEGIN_SRT_SET(PerDraw)/END_SRT_SET(PerDraw)**: 
These macros mark the beginning and end of the set of resources that are updated per draw call.

Which consists of:

- **gHair**                       : A constant buffer containing information for hair.
- **gGuideHairVertexPositions**   : A buffer containing the positions of hair vertices.
- **gHairThicknessCoefficients**  : A buffer containing the hair coeficients.
- **gHairVertexTangents**         : A read-write buffer containing the tangents of each hair vertex.
- **gDestDepthsTexture**          : An array of read-write depth textures.

By separating the persistent and per frame resources into their own header files and reusing them across different SRTs, we reduce the overall number of descriptor sets in the project.
This approach enhances memory usage efficiency and reduces the overhead of managing multiple descriptor sets.


#### SRT details:

SRT Resources: buffers, textures, constant buffers and samplers are made available to shaders including the srt header and can be accessed from any function.  

According to target platform, each of the resource sets is mapped to a low level container, for D3D12, they are mapped to resource tables in the root signature,
for vulkan they are mapped into vulkan descriptor sets, and in case of Metal, they are mapped into argument buffers.

For Metal, argument buffers are generated for each set automatically, with the exception of samplers and read/write resources which are passed as parameters to shader entry points.

In case Argument buffers are not required, the whole SRT can be declared NO AB.

Example:

```
BEGIN_SRT_NO_AB(SrtData)
    BEGIN_SRT_SET(Persistent)
        DECL_TEXTURE(Persistent, Tex2D(float4), gTexture)
        DECL_SAMPLER(Persistent, SamplerState, gSampler)
    END_SRT_SET(Persistent)
    BEGIN_SRT_SET(PerFrame)
        DECL_CBUFFER(PerFrame, CBUFFER(UniformData), gUniformBlock)
    END_SRT_SET(PerFrame)
END_SRT(SrtData)
```

This SRT declaration consists of a persistent set that contain a texture and a sampler, and a per frame set that contains a uniform buffer,
and when targeting metal, the generated shader is not going to be using argument buffers, and the resources are passed as paramaters to the shader entry point.

Additionally for metal, when the feature flags FT_RAYTRACING or FT_ICB are used, the set is forced into an argument buffer, which allows having more than 8 RW buffers, as Metal
shaders are limited to 8 RWBuffers as shader params, but not when argument buffer members.

### Static samplers

Shaders can directly use static samplers which are used in the common cases:

| Name                         | Sample result |
|------------------------------|---------------|
|***gSamplerPointClamp***      | Nearest pixel value, clamping sampling coordinates between 0 and 1 |
|***gSamplerPointWrap***       | Nearest pixel value, wrapping around when coordinates are out of 0 to 1 range|
|***gSamplerBilinearClamp***   | Bilinearly intepolated pixel with its neighbours pixels, clamping sampling coordinates between 0 and 1|
|***gSamplerBilinearWrap***    | Bilinearly intepolated pixel with its neighbours pixels, wrapping around when coordinates are out of 0 to 1 range|
|***gSamplerTrilinearClamp***  | Both neighbour pixels and with nearest mip map, clamping sampling coordinates between 0 and 1|
|***gSamplerTrilinearWrap***   | Both neighbour pixels and with nearest mip map, wrapping around when coordinates are out of 0 to 1 range|
|***gSamplerPointMirror***     | Nearest pixel value, mirroring coordinate values out of 0 to 1 range|
|***gSamplerPointBorder***     | Nearest pixel value, returning zero when coordinates out of 0 to 1 range|
|***gSamplerTrilinearMirror*** | Both neighbour pixels and with nearest mip map, mirroring coordinate values out of 0 to 1 range|
|***gSamplerTrilinearBorder*** | Both neighbour pixels and with nearest mip map, returning zero when coordinates out of 0 to 1 range|
|***gSamplerAnisotropic***     | Anisotropic sampler, wrapping around when coordinates are out of 0 to 1 range|

>Static samplers are declared in "Common_3/Graphics/FSL/defaults.h"
and they do not require management in C++ code:

### Sampling Textures

FSL 2 texture are fundamentally split between 2 categories :

1 - Readonly types for sampling  
* **Tex#D**, **Tex#DArray**, **Tex2DMS**, **TexCube**, **Depth2D**, **Depth2DMS**

2 - Read-write types:
* **RTex#D** (readonly), **WTex#D** (writeonly), **RWTex#D** (read-write)  

Sampling types map to hlsl `Texture#D` types, glsl `texture#D` and metal `texture#d<T, access::sample>` types.  
Read-Write types map to hlsl `RWTexture#D` types, glsl `image#D` types and metal `texture#d<T, access::read_write>` types.

Sampling is performed using `SampleTex#` functions.  
Load access is performed using `LoadTex#` functions for sampling types, and `LoadRWTex#` for read-write types.  
Writing is performed using `Write#D` functions.

This table list the texture sampling functions:

| Function                 | Parameters |
|--------------------------|--------------|
|**SampleTex1D**               | **Tex1D**, **SamplerState**, **Coord** |
|**SampleTex2D**             | **Tex2D**, **SamplerState**, **Coord** |
|**SampleTex2DArray**          | **Tex2DArray**, **SamplerState**, **Coord** |
|**SampleTex2DProj**           | **Tex2D**, **SamplerState**, **Coord** |
|**SampleTexCube**             | **TexCube**, **SamplerState**, **Coord** |
|**SampleUTexCube**            | **TexCube**, **SamplerState**, **Coord** |
|**SampleITexCube**            | **TexCube**, **SamplerState**, **Coord** |
|**SampleTex3D**               | **Tex3D**, **SamplerState**, **Coord** |
|**SampleCmp2D**               | **Tex2D**, **SamplerComparisonState**, **Coord** |
|**SampleGradTex2D**           | **Tex2D**, **SamplerState**, **Coord**, **Dx**, **Dy** |
|**SampleLvlTex2D**            | **Tex2D**, **SamplerState**, **Coord**, **Level** |
|**SampleLvlTex3D**            | **Tex3D**, **SamplerState**, **Coord**, **Level** |
|**SampleLvlTex2DArray**       | **Tex2DArray**, **SamplerState**, **Coord**, **Level** |
|**SampleLvlTexCube**          | **TexCube**, **SamplerState**, **Coord**, **Level** |
|**SampleLvlTexCubeArray**     | **TexCubeArray**, **SamplerState**, **Coord**, **Level** |
|**SampleLvlOffsetTex2D**      | **Tex2D**, **SamplerState**, **Coord**, **Level**, **Offset** |
|**SampleLvlOffsetTex2DArray** | **Tex2DArray**, **SamplerState**, **Coord**, **Level**, **Offset** |
|**SampleLvlOffsetTex3D**      | **Tex3D**, **SamplerState**, **Coord**, **Level**, **Offset** |



Example:

```
float4 value = SampleLvlTexCube( gSkyboxTexture, gSkyboxSampler, float3(1.0f, 0.0f, 0.0f ), 0 );
Write3D(gDstTexture, int3(0,0,0), value);
```

Thie code samples from a cube texture *gSkyboxTexture* using the sampler *gSkyboxSampler* and writes the result to an read write array of 2D textures *gDstTexture*


Texture dimensions can be retrieved using **GetDimensions**

Example:

```
int2 size = GetDimensions( gTextureA, gSampler );

int2 size2 = GetDimensions(gTextureB, NO_SAMPLER);
```
This code gets the dimensions of a texture using a sampler, and the dimensions of another texture without using a sampler.

Alternatively the macro **NO_SAMPLER** can be used when no sampler is provided.

### Atomic functions

The following atomic functions are supported:

- **AtomicAdd**/**AtomicOr**/**AtomicAnd**/**AtomicXor**
- **AtomicMin**/**AtomicMax**
- **AtomicLoad**/**AtomicStore**
- **AtomicExchange**/**AtomicCompareExchange**

Examples:

```
AtomicAdd(gBufferRW[0], 42, prev_val);
```
Atomic add of value 42 at location 0 of the read write buffer *gBufferRW*, previous value is written to last argument.


```
val = AtomicLoad(gBufferRW[0]);
AtomicStore(gBufferRW[0], 42);
```
Atomic load value at location 0 the atomic store of value 42 at location 0 of the buffer *gBufferRW*.

```
AtomicMin(gBufferRW[0], 42);
AtomicMax(gBufferRW[1], 42);
```
Store in location 0 of buffer *gBufferRW* the minimum of its value and 42, then store in location 1 the maximum of its value and 42.


### Shader IO

Shader input and output structuress are declared using the **STRUCT** macro.

Each data member of the struct is declared using the **DATA** macro.

Example:
```
STRUCT(VSInput)
{
    DATA(float4, position, SV_Position);
};
```
This code declares a vertex shader input struct named *VSInput*, which consists of a position vector stored in 4 floats.


Datatypes are normally passed to the shader entry main function.

Example:
```
VSOutput VS_MAIN(VSInput In)
```
This line declares the main entry point for a vertex shader that takes as input *In*, a variable of struct type **VS_Input** and returns another of type **VSOutput**

The following semantics are supported:
```
SV_Position
SV_VertexID
SV_Depth
SV_InstanceID
SV_GroupID
SV_DispatchThreadID
SV_GroupThreadID
SV_GroupIndex
SV_SampleIndex
SV_PrimitiveID
SV_DomainLocation
SV_Target[N]          // SV_Target, SV_Target0, SV_Target1, ..., SV_Target7
```

For regular main entry inputs, the semantic is used as a case-insensitive decoration around the variable type.

Example:

```
void CS_MAIN(SV_GroupIndex(uint) groupIndex)
```
This is a compute shader entry function that takes an unisgned integer named *groupIndex* of semantic **SV_GroupIndex**.


### Binary Declarations

Some shaders may use features not available on all devices. FSL 2 automatically compiles different shader variants for each unique feature set and creates a single unified binary that contains within the binaries of all of the variants.

This means a single "translation unit" can result in multiple binary shaders. At shader load time the correct binary will be chosen depending on the available platform/feature set.

To declare a single binary output named "myshader.frag" we use the following syntax :

```
#frag myshader.frag
// Code only active for myshader.frag translation
#end
```

### Feature Flags
Feature flags can be added to binary declarations to control the following features:

**FT_PRIM_ID**     : Necessary for use of SV_PrimitiveID.

**FT_RAYTRACING**  : Enables ray query extensions/headers and necessary msl/spirv/hlsl targets.

**FT_VRS**         : Variable Rate Shading.

**FT_MULTIVIEW**   : Necessary for multiview rendering for VR.

**FT_NO_AB**       : Suppression of argument buffer generation for MSL.


They are inserted into the declaration as follows:
```
#frag FT_PRIMT_ID FT_MULTIVIEW myshader.frag
```
This results in a shader binary that supports both primitive id and multi view.

### Non Uniform Resource Index

For accessing elements of resource arrays, special syntax is using the pair of functions **BeginNonUniformResourceIndex** and **EndNonUniformResourceIndex** which are necessary when the index is divergent:

Example:
```
uint index = (...);
float4 texColor = f4(0);
BeginNonUniformResourceIndex(index, 256); // 256 is the max possible index
    texColor = SampleLvlTex2D(gTextures[index], gTextureSampler, uv, 0);
EndNonUniformResourceIndex();
```
This code uses an index between 0 and 256 to sample using the sampler *gTextureSampler* from a texture in an array of textures *gTextures*, using the texture coordinate *uv*, sampling from mipmap 0.


Implementation details:

For Vulkan, the enclosed block gets replaced based on the availability of the following extensions:  
* VK_EXT_DESCRIPTOR_INDEXING_EXTENSION: wraps the index inside the block with nonuniformEXT(...)
* VK_FEATURE_TEXTURE_ARRAY_DYNAMIC_INDEXING: code inside the block is left untouched
* if no extension is available, a switch construct is used

For other platforms, a loop with lane masking is being used as necessary.


### Example: Skybox Vertex Shader

Below is a complete example of a vertex shader for rendering a skybox. Each line is explained in detail to highlight the use of the root signature, entry point, and shader logic.

```
STRUCT(VSInput) // Vertex shader input struct - an argument to the nain function
{
    DATA(float4, Position, POSITION); // Vertex input attributes created for each separate member
};

STRUCT(VSOutput) // Vertex shader output struct - the return type of the main function
{
    DATA(float4, Position, SV_Position);
    DATA(float4, TexCoord, TEXCOORD0);
};

ROOT_SIGNATURE(DefaultRootSignature)  // Declare the root signature used by this shader.

VSOutput VS_MAIN(VSInput In)  // Define the entry point for the vertex shader.
{
    INIT_MAIN;  // Initialize the main function for the shader.
    VSOutput Out;  // Declare the output structure.

    // Scale the input position by a factor of 9.0 to expand the skybox.
    float4 p = float4(In.Position.xyz * 9.0, 1.0);

#if FT_MULTIVIEW
    // Transform the position using the multi-view skybox MVP matrix if multi-view rendering is enabled.
    p = mul(gUniformBlock.skyMvp[VR_VIEW_ID], p);
#else
    // Transform the position using the single-view skybox MVP matrix.
    p = mul(gUniformBlock.skyMvp, p);
#endif

    // Assign the transformed position to the output's position with adjusted W coordinates.
    Out.Position = p.xyww;

    // Pass through the input position as the texture coordinate for the skybox.
    Out.TexCoord = float4(In.Position.x, In.Position.y, In.Position.z, In.Position.w);

    RETURN(Out);  // Return the output structure.
}
```


### Explanation of the Code:

***Root Signature Declaration***
```
ROOT_SIGNATURE(DefaultRootSignature)
```

- This macro declares that the shader uses the root singature named *DefaultRootSignature*, which is used across all graphics shaders.

- The root signature links the shader to the descriptor sets used for resource binding.

***Shader Entry Point***

```
VSOutput VS_MAIN(VSInput In)
```

- The function **VS_MAIN** is the entry point for the vertex shader.

- **VSInput**: Struct type which consists of the vertex attributes such as position and normal, variable name is *In*

- **VSOutput**: Struct type that contains the processed vertex data passed to the next pipeline stage.

***Main Function Initialization***

```
INIT_MAIN;
```

- This macro initializes the main function, ensuring that all necessary internal states are set up correctly.

- For each diffirent target platform, this declaration is translated to what fits it.

***Vertex Transformation***

```
float4 p = float4(In.Position.xyz * 9.0, 1.0);
```

- Scales the vertex position by a factor of 9.0, effectively expanding the size of the skybox.

***Multi-View Transformation***

```
#if FT_MULTIVIEW
    p = mul(gUniformBlock.skyMvp[VR_VIEW_ID], p);
#else
    p = mul(gUniformBlock.skyMvp, p);
#endif
```


- Depending on whether multi-view rendering is enabled:
  - Transforms the vertex position using either the multi-view or single-view skybox MVP matrix.
- **gUniformBlock.skyMvp**: A matrix declared in the SRT that is used for transformation.

***Output Position Assignment***

```
Out.Position = p.xyww;
```

- Assigns the transformed vertex position to the output. The W coordinate is set to maintain the correct perspective in the skybox rendering.

***Texture Coordinate Assignment***

```
Out.TexCoord = float4(In.Position.x, In.Position.y, In.Position.z, In.Position.w);
```

- The input vertex position is directly used as the texture coordinate for sampling the skybox texture.

***Return Statement***

```
RETURN(Out);
```

- The **RETURN** macro finalizes the output and ensures it is properly passed to the next stage of the pipeline.


### Example: Pixel Shader

Below is an example of a basic pixel shader that forwards the color received as an output from the vertex shader.

```
STRUCT(PSOutput) // Pixel shader output struct - the return type of the main function
{
    DATA(float4, Color, SV_Target);
};

ROOT_SIGNATURE(DefaultRootSignature)  // Declare the root signature used by this shader.

PSOutput PS_MAIN(VSOutput In)  // Define the entry point for the pixel shader
{
    INIT_MAIN;  // Initialize the main function for the shader.
    PSOutput Out;  // Declare the output structure.
    
    // Forward the color received from the vertex shader output
    Out.Color = In.Color;

    RETURN(Out);  // Return the output structure.
}
```

### Explanation of the Code:

***Root Signature Declaration***
```
ROOT_SIGNATURE(DefaultRootSignature)
```

- This macro declares that the shader uses the root singature named *DefaultRootSignature*, which is used across all graphics shaders.

- The root signature links the shader to the descriptor sets used for resource binding.

***Shader Entry Point***

```
float4 PS_MAIN(VSOutput In)
```

- The function **PS_MAIN** is the entry point for the pixel shader.

- **VSOutput**: Struct type which consists of the vertex shader output, variable name is *In*

- **PSOutput**: return type of pixel shader, which contains a single member `Color` - a color in 4 components red, green, blue and alpha.

***Main Function Initialization***

```
INIT_MAIN;
```

- Initializes main entry function, ensuring that all necessary internal states are set up correctly.

- For each diffirent target platform, this declaration is translated to the target platform equivalent.

```
PSOutput Out;
Out.Color = In.Color;
RETURN(Out);
```

- The **RETURN** macro returns the same color that was calulated in the vertex shader and came into pixel shader as input.



### Example Compute Shader: Screenshot Processing

***Code Example***

```
ROOT_SIGNATURE(ComputeRootSignature) // Define the root signature for compute resources.
NUM_THREADS(8, 8, 1) // Configure thread group size: 8x8 threads per group.

void CS_MAIN(SV_DispatchThreadID(uint3) DTid) // Entry point for the compute shader.
{
    INIT_MAIN; // Initialize the shader's internal state.

    // Fetch input texture dimensions.
    uint2 inputDims = uint2(GetDimensions(gInputTexture, NO_SAMPLER));

    // Skip threads outside the valid input dimensions.
    if (DTid.x >= inputDims.x || DTid.y >= inputDims.y)
    {
        RETURN(); // Exit the thread early if it's outside bounds.
    }

    // Calculate UV coordinates for sampling the input texture.
    float2 uv = float2(float(DTid.x + 0.5f) / inputDims.x, float(DTid.y + 0.5f) / inputDims.y);
    float4 color = SampleLvlTex2D(gInputTexture, gSamplerPointClamp, uv, 0); // Sample the texture.
    uint offset = DTid.x + DTid.y * inputDims.x; // Compute linear offset for output storage.

    // Optional: Flip red and blue channels based on constants.
    if (gScreenShotConstants.flipRedBlue > 0)
    {
        float temp = color.r;
        color.r = color.b;
        color.b = temp;
    }

    // Optional: Convert color to sRGB space if required.
    if (gScreenShotConstants.convertToSrgb > 0) 
    {
        color = linear4ToSrgb4(color);
    }

    // Save output as HDR or packed color based on configuration.
    if (gScreenShotConstants.saveAsHDR > 0)
    {
        uint4 uintColor = asuint(color);
        StoreByte(gOutputBuffer, (offset * 4 + 0) << 2, uintColor.r);
        StoreByte(gOutputBuffer, (offset * 4 + 1) << 2, uintColor.g);
        StoreByte(gOutputBuffer, (offset * 4 + 2) << 2, uintColor.b);
        StoreByte(gOutputBuffer, (offset * 4 + 3) << 2, uintColor.a);
    }
    else
    {
        uint packedColor = packUnorm4x8(color); // Pack color into 32-bit format.
        StoreByte(gOutputBuffer, offset << 2, packedColor); // Write packed color to output.
    }

    RETURN(); // Finalize the thread execution.
}
```

### Explanation of the Code:


***Root Signature Declaration***
```
ROOT_SIGNATURE(ComputeRootSignature)
```

- This macro declares that the shader uses the root signature named *ComputeRootSignature*, which is used across all compute shaders

- Links resources  *gInputTexture*, *gSamplerPointClamp*, and *gOutputBuffer* to the GPU pipeline.


***Thread Group Configuration***
```
NUM_THREADS(8, 8, 1)
```

- The macro **NUM_THREADS** defines the thread group size as 8x8x1, meaning each thread group contains 64 threads.

- The compute shader is executed for each thread in the group, enabling parallel processing.

***Entry Point***

```
void CS_MAIN(SV_DispatchThreadID(uint3) DTid)
```


- The entry point **CS_MAIN** is a void function because compute shaders do not need to return a value. Their purpose is to process data directly.

- **SV_DispatchThreadID(uint3 DTid)**: A system-generated ID uniquely identifying the thread within the dispatch.

***Early Exit for Out-of-Bounds Threads***

```
if (DTid.x >= inputDims.x || DTid.y >= inputDims.y)
{
    RETURN();
}
```

- Ensures that threads outside the valid texture dimensions do not perform unnecessary operations.

- Uses the **RETURN()** macro to terminate these threads early.

***Texture Sampling***

```
float2 uv = float2(float(DTid.x + 0.5f) / inputDims.x, float(DTid.y + 0.5f) / inputDims.y);
float4 color = SampleLvlTex2D(gInputTexture, gSamplerPointClamp, uv, 0);
```

- Calculates normalized UV coordinates for sampling.

- Samples the input texture using a point-clamp sampler.

***Channel Swapping Option***

```
if (gScreenShotConstants.flipRedBlue > 0)
{
    float temp = color.r;
    color.r = color.b;
    color.b = temp;
}
```

- Conditionally swaps the red and blue channels to adjust the image format if the *flipRedBlue* flag is greater than 0.

***optionalsRGB Conversion***

```
if (gScreenShotConstants.convertToSrgb > 0) 
{
    color = linear4ToSrgb4(color);
}
```

- Converts the linear color to sRGB format if enabled by the constants if the *convertToSrgb* flag is greater than 0.

**Output Storage**

***Saving as HDR***

```
uint4 uintColor = asuint(color);
StoreByte(gOutputBuffer, (offset * 4 + 0) << 2, uintColor.r);
StoreByte(gOutputBuffer, (offset * 4 + 1) << 2, uintColor.g);
StoreByte(gOutputBuffer, (offset * 4 + 2) << 2, uintColor.b);
StoreByte(gOutputBuffer, (offset * 4 + 3) << 2, uintColor.a);
```

- Saves the output as HDR by storing each channel as a 32-bit value in the output buffer.


***Saving as Packed Color***

```
uint packedColor = packUnorm4x8(color);
StoreByte(gOutputBuffer, offset << 2, packedColor);
```
- Packs the color into a single 32-bit value using **packUnorm4x8** and stores it in the output buffer.

***Thread Completion***

```
RETURN();
```

- Finalizes the thread execution without requiring a returned value.


## Initializing root signature in C++

Application uses a single root signature for graphics, and another for compute.

The unified root signature needs to be created at load time.

> No referencing to root signatures is required, and retreiving resource indices by name is no longer required.

Example:

```
/// C++ code
RootSignatureDesc rootDesc = {};
INIT_RS_DESC(rootDesc, "default.rootsig", "compute.rootsig");
initRootSignature(pRenderer, &rootDesc);
```
This C++ code initializes the root signatures used by the application, the macro **INIT_RS_DESC** populates the descriptor *rootDesc* with the names of the graphics and Compute
root signature : *default.rootsig* , and the compute root signature *compute.rootsig*.

Then a call to **initRootSignature** is passed in the descriptor.

> One of the most recent improvements in The Forge is the removal of the RootSignature interfaces (**addRootSignature**/**removeRootSignature**)

A call to **exitRootSignature** needs to be in application cleanup code.


## Descriptors binding interface in C++

Descriptor binding is handled through the **DescriptorSet** interface.

Descriptor sets should be defined in a header that is included from both shaders that are using the resources declared inside it, and the C++ code in which the descriptor sets and created and used.

### Using Descriptor sets based on update Frequencies

To optimize performance for graphics resources are grouped into descriptor sets based on the frequency at which they are updated.

In The Forge, there are 4 descriptor sets, which correspond to matching update frequencies :

  - **Persistent** : Consists of resources which hardly get updated in the lifetime of the application.
  - **PerFrame**   : Consists of resources which get updated once per frame.
  - **PerBatch**   : Consists of resources which get updated per arbitrary batch (batch is application dependent. commonly this is per material)
  - **PerDraw**    : Consists of resources which get updated per draw call. (information which changes per draw call, ... usually with modern APIs, it is better to avoid this update frequency when not necessary)


Example:

```
/// Global.srt.h included from FSL shader and C++
BEGIN_SRT(SrtData)
    BEGIN_SRT_SET(Persistent)
        DECL_TEXTURE(Persistent, Tex2D(float4), gTexture)
        DECL_SAMPLER(Persistent, SamplerState, gSampler)
    END_SRT_SET(Persistent)
    BEGIN_SRT_SET(PerFrame)
        DECL_CBUFFER(PerFrame, CBUFFER(UniformData), gUniformBlock)
    END_SRT_SET(PerFrame)
END_SRT(SrtData)
```

This header declares the SRT *SrtData*, which consists of :
 - A persistent set that consists of a 2D texture *gTexture* and a sampler *gSampler*.
 - A per frame set that consists of a uniform buffer *gUniformBlock*.


### Allocating Descriptor sets

Descriptor sets are allocated using the api function **addDescriptorSet** and freed using **removeDescriptorSet**

> According to target platform, a descriptor set abstracts a **VkDescriptorSet** in Vulkan or a **D3D12_GPU_DESCRIPTOR_HANDLE** int Direct3D12, or a **MTLArgumentBuffer** in Metal.


Example:
```
/// C++ code :

#include "../../../../Common_3/Graphics/FSL/defaults.h"
#include "./Shaders/FSL/Global.srt.h"

...

/// globals
DescriptorSet* pDescriptorSetPersistent = { NULL };
DescriptorSet* pDescriptorSetPerFrame = { NULL };


void addDescriptorSets()
{
    DescriptorSetDesc descPersisent = SRT_SET_DESC(SrtData, Persistent, 1, 0);
    addDescriptorSet(pRenderer, &descPersisent, &pDescriptorSetPersistent);
    DescriptorSetDesc descPerFrame = SRT_SET_DESC(SrtData, PerFrame, gDataBufferCount, 0);
    addDescriptorSet(pRenderer, &descPerFrame, &pDescriptorSetPerFrame);
}
```

This code includes the shared SRT header, a declaration for the persistent and per frame descriptor sets,
and the function *addDescriptorSets* uses the macro **SRT_SET_DESC** to initialize the descriptor set descriptors *descPersistent* and *descPerFrame*.
the descriptor sets' descriptor are then passed into the calls of **addDescriptorSet** with the renderer and is used to allocate the descriptor sets *pDescriptorSetPersistent* and *pDescriptorSetPerFrame*


### Freeing up Descriptor sets

Descriptor sets are freed up using the api function **removeDescriptorSet**

Example:

```
/// C++ code :
void removeDescriptorSets()
{
    removeDescriptorSet(pRenderer, &pDescriptorSetPersistent);
    removeDescriptorSet(pRenderer, &pDescriptorSetPerFrame);
}
```

This code frees the persistent descriptor set *pDescriptorSetPersistent* and the per frame descriptor set *pDescriptorSetPerFrame*


### Updating descriptor sets to link to resources

As samplers, textures and buffers are created in C++, they need to be linked to descriptor sets, the struct **DescriptorData**  consist of a resource index **mIndex**
and a pointer to resources ( samplers, textures or buffers ), and a count which represents the number of array elements.

The index for each resource is retreived with the macro **SRT_RES_IDX**, and the api function **updateDescriptorSet** is used to update the descriptor set elements to be linked to resources.

Example:

```
/// C++ code :
// Prepare persistent sets
DescriptorData persistentParams[2] = {};
persistentParams[0].mIndex = SRT_RES_IDX(SrtData, Persistent, gTexture);
persistentParams[0].ppTextures = &pSkyBoxTextures[0];
persistentParams[1].mIndex = SRT_RES_IDX(SrtData, Persistent, gSampler);
persistentParams[1].ppSamplers = &pSkyBoxSampler;
updateDescriptorSet(pRenderer, 0, pDescriptorSetPersistent, 2, persistentParams);

for (uint32_t i = 0; i < gDataBufferCount; ++i)
{
    DescriptorData perFrameParam = {};
    perFrameParam.mIndex = SRT_RES_IDX(SrtData, PerFrame, gUniformBlock);
    perFrameParam.ppBuffers = &pUniformBuffer[i];
    updateDescriptorSet(pRenderer, i, pDescriptorSetPerFrame, 1, &perFrameParam);
}
```

This code initializes an array of 2 persistent descriptors params *persistentParams*.

The first one uses the index to the texture, which is retreived from the macro **SRT_RES_IDX** by passing in the name of the SRT : *SrtData*, the name of the set : *Persistent* and the name of the resource : *gTexture*,
then it links it to the runtime texture created in C++ : *pSkyBoxTextures[0]*.

The second one uses the index to the sampler, which is retreived from the macro **SRT_RES_IDX** by passing in the name of the SRT : *SrtData*, the name of the set : *Persistent* and the name of the resource : *gSampler*,
then it links it to the runtime sampler created in C++ : *pSkyBoxSampler*.

> Using **SRT_RES_IDX** results in resolving the link between resources names and their ids happens at compilation time, which means there is no longer need to search for resouces ids using string or hashes comparisons.

Then the actual linking is done by the api call **updateDescriptorSet**, to which the renderer is passed, the binding index 0, the descriptor set *pDescriptorSetPersistent*, the number of descriptors to update which is 2 in this case,
and last parameter is the array containing the 2 descriptors.

Then similarly, the per frame set is updated :

In this case there is a loop that repeats the following steps for each number of buffers per frame *gDataBufferCount*:

Create one data descriptor *perFrameParam* and set its index to uniform block which is retreived from the macro **SRT_RES_IDX** by passing in the name of the SRT : *SrtData*, the name of the set : *PerFrame* and the name of the resource : *gUniformBlock*,
then it links it to the runtime buffers created in C++ in the array *pUniformBuffer* , each cycle in the loop taking one by index.

Then the actual linking is done by the api call **updateDescriptorSet**, to which the renderer is passed, the binding index *i*, the descriptor set *pDescriptorSetPerFrame*, the number of descriptors to update which is 1 in this case,
and last parameter is the pointer to the descriptor data.


### Binding Descriptor Sets

During command buffer generation, after binding pipelines, descriptor sets are bound using the api function **cmdBindDescriptorSet**.

Example:

```
// Graphics:
/// C++ code :
cmdBindPipeline(cmd, pGraphicsPipeline);
cmdBindDescriptorSet(cmd, 0, pDescriptorSetPersistent);
cmdBindDescriptorSet(cmd, gFrameIndex, pDescriptorSetPerFrame);
cmdBindVertexBuffer(cmd, 1, &pVertexBuffer, &pStrides, NULL);
cmdDraw(cmd, numVerts, 0);
```

This code is part of drawing, during command buffer *cmd* generation, here the pipeline *pGraphicsPipeline* is bound by a call to **cmdBindPipeline**, passing into it the command buffer and the pipeline,
then the descriptor set *pDescriptorSetPersistent* is bound on index 0 by calling **cmdBindDescriptorSet**
then the descriptor set *pDescriptorSetPerFrame* is bound on index *gFrameIndex* by calling **cmdBindDescriptorSet**
then a vertex buffer is bound and a draw call is done which would be using the resources from the just bound descriptor sets.

### Descriptors binding interface

**Functions:**

**void addDescriptorSet(Renderer\* pRenderer, const DescriptorSetDesc\* pDesc, DescriptorSet\*\* ppDescriptorSet)**
- Allocates a descriptor set with the given description
- Sets all the descriptor handles to their default values (default texture, default buffer, ...)

**void removeDescriptorSet(Renderer\* pRenderer, DescriptorSet\* pDescriptorSet)**
- Frees the descriptor set

**void updateDescriptorSet(Renderer\* pRenderer, uint32_t index, DescriptorSet\* pDescriptorSet, uint32_t paramCount, DescriptorData\* pParams)**
- Updates the descriptor set at index with the provided array of descriptor data
- Safe to call from any thread as long as the the threads are accessing different index

**void cmdBindDescriptorSet(Cmd\* pCmd, uint32_t index, DescriptorSet\* pDescriptorSet)**
- Binds the descriptor set at index to the command buffer
- Safe to call from any thread


**Macros:**

**SRT_SET_DESC(srt, freq, maxSets, nodeIndex)**
- Used to retreive the descriptor sets from the shared SRT headers, to pass into addDescriptorSet
- srt : name of the SRT 
- freq : update frequency (Persistent, PerFrame, PerBatch or PerDraw )
- maxSets : how often it is updated, should be 1 for Persistent, 2 for per frame for double buffering, for the other will be case dependant
- nodeIndex : only applicable for multi GPU scenarios.
  
**SRT_SET_DESC_LARGE_RW(srt, freq, maxSets, nodeIndex)**
- For Metal use only in case of RayTracing, or when more than 8 Read/Write textures are required.
- internally maps to an argument buffer.
  
  
**SRT_RES_IDX**
- Used to link resources into descriptor set from code side.
- Typically used to prepare params for calls to updateDescriptorSet.
  
**PIPELINE_LAYOUT_DESC**
- Requied for Vulkan
- Should be used when creating pipelines 
- Allows passing the descriptor table information when calling vkCreatePipeline which is required for Vulkan

Example :
```
    PipelineDesc desc = {};
    desc.mType = PIPELINE_TYPE_GRAPHICS;
    PIPELINE_LAYOUT_DESC(desc, SRT_LAYOUT_DESC(SrtData, Persistent), SRT_LAYOUT_DESC(SrtData, PerFrame), NULL, NULL);
    // ... fill pipelineSettings
    addPipeline(pRenderer, &desc, &pSpherePipeline);
```
This code creates an empty pipeline descriptor *desc*, sets its type to be a graphics pipeline, then uses **PIPELINE_LAYOUT_DESC** to initialize the desc with the descriptors layout of *SrtData*, 
passing it the descriptor set layouts for the persistent and per frame sets, and passing nulls for the 3rd and 4th as they are not used in this pipeline
then populated the rest of members of desc, then calls **addPipeline** to allocate a new pipeline with the descriptor layouts.

