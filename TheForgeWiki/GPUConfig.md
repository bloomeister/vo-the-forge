
## Summary

- [GPU Configuration system](#GPU-Configuration-system)
  - [Hardware Capabilities](#Hardware-Capabilities)
  - [GPUPresetLevel](#gpupresetlevel)
  - [GPU Selection](#GPU-Selection)
  - [Driver Rejection](#Driver-Rejection)
  - [User Extended Settings](#User-Extended-Settings)

- [Open questions](#Open-questions)
- [List of available properties](#List-of-available-properties)

## GPU Configuration system

Our configuration system allows to:

- Access an exhaustive list of **hardware features** and **capabilities** (see Vulkan hardware capability viewer) 
- Give a **performance rating** to each available gpu (office, low, medium, high, ultra) 
- **Choose a specific gpu** when multiple are available
- Turn on and off certain hardware features (ex: **turn off raytracing support** for a specific vendor)
- Disable certain gpu **depending on the current driver version**
- Set application settings based on the current hardware (ex: disable certain game mechanics if there are no proper support for advanced transparency)

What it is not:

- A full feature configuration system you see in most game, ex: **Graphic Settings** panel
- A system to manage the specific settings of your application.

![](<./img/gpuConf.jpg>)

### Hardware Capabilities

TheForge lets you access various structures that store hardware information about the current device your application is using:

- GPUSettings: storing various flags that indicate the hardware features supported across different platform (**mHDRSupported**, **mTessellationSupported**, **mRaytracingSupported**, **mROVsSupported**, **mTessellationSupported**, **mVRAM**, **mWaveOpsSupportFlags**, ...)
  - The quality index assigned to the gpu is set inside the **mGpuVendorPreset** attribute
- GPUCapBits: storing the list of all the available texture formats (**TinyImageFormat_R32G32_UINT**, **TinyImageFormat_R32_SFLOAT**, **TinyImageFormat_ASTC_8x8_SRGB**, ...)
- RendererContext: storing features and extensions specific to each graphic API
  - GpuInfo attribute can be used to access the native interface directly (**IDXGIAdapter**, **VkPhysicalDeviceProperties2**, **MTLDevice**, ...) 

### GPUPresetLevel

A performance index is assigned to the current gpu during the initialization phases in *initRenderer(*). This value comes from reading the gpu.data file in the **RD_GPU_CONFIG** directory, previously set via **fsSetPathForResourceDir()**.

This file contains a list of available model of graphics card and their manufacturers. You can find the URLs that were used to create this database at the beginning of the file. Feel free to keep this list updated as needed. 

Consoles have been added inside this list:

- **Xbox:** the modelID is obtained by invoking **XsystemGetDevicetype**, which return a enum value you can find on the [msdn](https://learn.microsoft.com/en-us/gaming/gdk/_content/gc/reference/system/xsystem/enums/xsystemdevicetype) documentation page
- **Playstation:** use a proprietary platform macro to assign an identifier for each console
- **Nintendo Switch:** use the the model code of the nvidia tegra device
- **SteamDeck:** use the model code of the amd apu

If the model is missing you can use the **DefaultPresetLevel** property to assign a GPUPresetLevel to any unknown device. This can be usefull on a client machine or for testing your application's quality presets on multiple graphic cards.

*gpu.data:*

```
BEGIN_VENDOR_LIST;
intel; 0x163C, 0x8086, 0x8087;
nvidia; 0x10DE
amd; 0x1002, 0x1022;
qualcomm; 0x5143;
imagination technologies; 0x1010;
samsung; 0x144D;
arm; 0x13b5;
apple; 0x106b;
END_VENDOR_LIST;

BEGIN_DEFAULT_CONFIGURATION;
#if the current gpu doesn't exist in GPU_LIST use this instead
DefaultPresetLevel; Low;
END_DEFAULT_CONFIGURATION;

#VendorId; DeviceId; Classification; Name ; Revision ID (Can be null) ; Codename (can be null)
BEGIN_GPU_LIST;
# --- NVIDIA GPUs --- 
0x10de; 0x0045; Low; NVIDIA GeForce 6800 GT
0x10de; 0x0040; Low; NVIDIA GeForce 6800 Ultra
...
0x10de;	0x2860; Ultra; GeForce RTX 4070 Max-Q / Mobile;
0x10de;	0x2704; Ultra; GeForce RTX 4080;
...
# --- INTEL GPUs ---
0x8086; 0xA780; Medium; Intel(R) Xe Graphics
# --- XBOX ---
0x7a0d; 0x2; Low; Xbox One;
...
END_GPU_LIST;
```

### GPU Selection

When multiple devices are available it's possible to write a set of rules which can be used to select one device against an other. If there are no rules, the first gpu found will be used. Those rules are defined in the *gpu.cfg* file also located in the **RD_GPU_CONFIG** directory.

The rules are in **descending order**, and the first rule that gives a different result for two distinct gpu will make the final decision.

Each gpu are compared one against on another in their discovery order, once one is rejected it will no longer be used as a potential candidate. The discovery order of the gpu can affect the final outcome (imagine a list of rock, paper and scissor, the last remaining candidate will vary depending in the order they are processed)

The possible syntaxes for a rule is:

- *\<property\>;*

```
BEGIN_GPU_SELECTION;
GpuPresetLevel;
DirectXFeatureLevel;
VRAM;
END_GPU_SELECTION;
```

This will choose the gpu with the greatest **GpuPresetLevel**, if they are both equal it will pick the one with the greatest **DirectXFeatureLevel** on windows and finally, if they all return the same, it will pick the one with the maximum amount of **VRAM**.

- *\<property\> \<comparator\> \<value\>**,** \<property\> \<comparator\> \<value\>, ... ;*
```
BEGIN_GPU_SELECTION;
DirectXFeatureLevel < 11;
deviceid == PreferredGPU;
# Intel vendor: 0x8086 && 0x8087 && 0x163C
VendorID != 0x8086, VendorID != 0x8087, VendorID != 0x163C;
END_GPU_SELECTION;
```

This will first eliminate the gpu if the **DirectXFeatureLevel** is lower than 11, then it will use the special variable **PreferredGPU** to choose the gpu with the matching deviceid, if it is correctly set, and finally it will skip intel gpu.

You can combine those different syntax to come down with your own set of rule.

### Driver Rejection

If you want to reject a specific driver for a given manufacturer, you can add the following rule in **gpu.cfg**:

*\<vendorID\>; DriverVersion \<comparator\> \<driverVersion\>; \<reasonStr\>;*

```
BEGIN_DRIVER_REJECTION;
# amd: 0x1002, 0x1022
0x1002; DriverVersion <= 23.10.23.03; 09a unit test artefacts, pixelated and too bright, 15a flickers;
0x1022; DriverVersion <= 23.10.23.03; 09a unit test artefacts, pixelated and too bright, 15a flickers;
END_GPU_SETTINGS;
```

This will reject all amd drivers prior to 23.10.23.03. You can use this to inform your users that they should update their graphic driver.

Driver convention name:

- **Nvidia**: *\<Major\>.\<Minor\>* ex: **537.13**
- **AMD**: \<YEAR\>.\<MONTH\>.\<REVISION\> ex: **23.10.23.03**
- **Intel**:  we only use the **\<BUILD_NUMBER\>**, normally it's supposed to look like this *\<OS\>.0.\<BUILD_NUMBER\>* but Vulkan only return the last part, for instance, 31.0.101.5074 will become **101.5074** see [intel convention](https://www.intel.com/content/www/us/en/support/articles/000005654/graphics.html)

### Configuration Settings

It's possible to turn on and off certain hardware features and gpu properties using specific rules in **gpu.cfg**. This way you can disable functionalities on a specific set of devices. The rule syntax is the following:

*\<sourceProperty\>; \<compProperty\> \<comparator\> \<compValue\>, ... ; \<assignmentValue\>;*

```
BEGIN_GPU_SETTINGS;
# nvidia
maxRootSignatureDWORDS; vendorID == 0x10DE; 64;
# amd
maxRootSignatureDWORDS; vendorID == 0x1002; 13;
# disable tesselation support on arm system
tessellationsupported; vendorID == 0x13B5; 0;
END_GPU_SETTINGS;
```

This will set the maximum size of the **rootSignature** to 64x32bits DWORD on nvidia, and 13 on AMD graphic card. It will also disable tessellation shader on ARM system.


### Texture format

It's possible to remove certain texture capability.
If your code support it, you can then change your texture format at runtime.
One usecase is changing the format for shadow map on some android devices where FORMAT_CAP_LINEAR_FILTER is not properly supported.

```
BEGIN_TEXTURE_FORMAT;
D32_SFLOAT; vendorID == 0x10de; +FORMAT_CAP_LINEAR_FILTER;
R16G16B16A16_UNORM; vendorID == 0x10de; -FORMAT_CAP_READ_WRITE;
R32G32B32A32_SFLOAT; vendorID == 0x10de; -FORMAT_CAP_LINEAR_FILTER;
R32G32_SFLOAT; vendorID == 0x10de; -FORMAT_CAP_LINEAR_FILTER;
R32_SFLOAT; vendorID == 0x10de; -FORMAT_CAP_LINEAR_FILTER;
END_TEXTURE_FORMAT;
```

the synthax is the following one:
*\<textureformat\>; \<property\> \<comparator\> \<comparisonValue\>, ... ; \<+or-\>\<FormatCapability\>*

### User Extended Settings

It's possible to set application wide settings using gpu.cfg. First you will need to register your settings by filling the **ExtendedSettings** attribute of your **RendererDesc** instance. You will have to provide a string literal and an integer variable to store the setting's value:

``` c++
const char* gSettingNames[];
struct ConfigSettings gGpuSettings;
    
ExtendedSettings extendedSettings = {};
extendedSettings.mNumSettings = ESettings::Count;
extendedSettings.pSettings = (uint32_t*)&gGpuSettings;
extendedSettings.ppSettingNames = gSettingNames;

RendererDesc settings;
memset(&settings, 0, sizeof(settings));
settings.pExtendedSettings = &extendedSettings;
```

Once it's done you can add your rules in gpu.cfg, the syntax is the following one:

*\<settingName\>; \<property\> \<comparator\> \<comparisonValue\>, ... ; \<assignmentValue\>*

```
BEGIN_USER_SETTINGS;
EnableAOIT; RasterOrderViewSupport == 1; 1;
END_USER_SETTINGS;
```

This will set the **EnableAOIT** variable to 1 if the hardware supports [razterizer order views](https://learn.microsoft.com/en-us/windows/win32/direct3d11/rasterizer-order-views)


### List of available properties

| Property name                     | Read  | Write |
| --------------------------------- | ----- | ----- |
| allowbuffertextureinsameheap      | :white_check_mark: | :white_check_mark:      |
| builtindrawid                     | :white_check_mark: | :white_check_mark:      |
| cubemaptexturearraysupported      | :white_check_mark: | :white_check_mark:      |
| tessellationindirectdrawsupported | :white_check_mark: | :white_check_mark:      |
| isheadless                        | :white_check_mark: | :white_check_mark:      |
| deviceid                          | :white_check_mark: | :x:   |
| directxfeaturelevel               | :white_check_mark: | :white_check_mark:      |
| geometryshadersupported           | :white_check_mark: | :white_check_mark:      |
| gpupresetlevel                    | :white_check_mark: | :white_check_mark:      |
| graphicqueuesupported             | :white_check_mark: | :white_check_mark:      |
| hdrsupported                      | :white_check_mark: | :white_check_mark:      |
| dynamicrenderingenabled           | :white_check_mark: | :white_check_mark:      |
| indirectcommandbuffer             | :white_check_mark: | :white_check_mark:      |
| indirectrootconstant              | :white_check_mark: | :white_check_mark:      |
| maxboundtextures                  | :white_check_mark: | :white_check_mark:      |
| maxrootsignaturedwords            | :white_check_mark: | :white_check_mark:      |
| maxvertexinputbindings            | :white_check_mark: | :white_check_mark:      |
| multidrawindirect                 | :white_check_mark: | :white_check_mark:      |
| occlusionqueries                  | :white_check_mark: | :white_check_mark:      |
| pipelinestatsqueries              | :white_check_mark: | :white_check_mark:      |
| primitiveidsupported              | :white_check_mark: | :white_check_mark:      |
| rasterorderviewsupport            | :white_check_mark: | :white_check_mark:      |
| raytracingsupported               | :white_check_mark: | :white_check_mark:      |
| rayquerysupported                 | :white_check_mark: | :white_check_mark:      |
| raypipelinesupported              | :white_check_mark: | :white_check_mark:      |
| softwarevrssupported              | :white_check_mark: | :white_check_mark:      |
| tessellationsupported             | :white_check_mark: | :white_check_mark:      |
| timestampqueries                  | :white_check_mark: | :white_check_mark:      |
| uniformbufferalignment            | :white_check_mark: | :white_check_mark:      |
| uploadbuffertexturealignment      | :white_check_mark: | :white_check_mark:      |
| uploadbuffertexturerowalignment   | :white_check_mark: | :white_check_mark:      |
| vendorid                          | :white_check_mark: | :x:   |
| vram                              | :white_check_mark: | :white_check_mark:      |
| wavelanecount                     | :white_check_mark: | :white_check_mark:      |
| waveopssupport                    | :white_check_mark: | :white_check_mark:      |
