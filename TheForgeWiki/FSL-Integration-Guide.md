# **Forge Shader Language generator tool integration Guide**

## FSL Integration and python tool

When integrating FSL shader generator tool into a project, Python 3.6 is required.
In The Forge, we include a no-install python3.6 in *Tools/python-3.6.0-embed-amd64*.

FSL can be integrated into Visual Studio, XCode and CodeLite projects, or any possible build system.
The generator tool can also be called directly from the python script fsl.py.


### Visual Studio integration :

We provide a target file *fsl.target* found in *Common_3\Tools\ForgeShadingLanguage* to be added to visual studio project files (.vcxproj)

To add the build customization right-click the project in VS and choose "Build Dependencies" -> "Build Customizations..." -> "Find Existing..." and choose the fsl.target file. 

The customization can than be enabled per-project from the same menu.

inside the project file, this section is added:

```
  <ImportGroup Label="ExtensionTargets">
    <Import Project="..\..\..\Common_3\Tools\ForgeShadingLanguage\VS\fsl.targets" />
  </ImportGroup>
```

This adds to the visual studio project support for FSL files *.fsl* which adds to the property pages a new section **FSLShader** that contains configuration properties,
and allows compiling the *.fsl* files using the provided tool.

Once added to a project, any added *.fsl is assigned the `<FSLShader>` item type.

In fsl.target custom target we prepend the provided python path to PATH, such that the build system uses that binary.

### Xcode integration :
For XCode, we use a custom build rule for *.fsl* resources and directly generate the metal shaders
into the the target package. You can find this in a shell script in the `Build Phases` section of the XCode project settings.


### CodeLight integration :

Similarly the integration in Code Light project is by adding a custom build target that invokes the FSL generator tool.

### FSL Tool command:

The generator tool can be called directly from the python script *fsl.py*.

```
usage: fsl.py [-h] -d DESTINATION -b BINARYDESTINATION
              [-i INTERMEDIATEDESTINATION] [-l LANGUAGE]
              [-I INCLUDES [INCLUDES ...]] [--verbose] [--compile] [--debug]
              [--rootSignature ROOTSIGNATURE] [--incremental] [--mp MP]
              [--reloadServerPort RELOADSERVERPORT] [--cache-args]
              fsl_input
```

**-h**                  : Used to display help about the command arguments.

**-d**                  : ***Required*** Specify the destination folder for the generated shaders.

**-b**                  : ***Required*** Specify the destination folder compiled binary shaders that will be used at runtime.

**fsl_input**           : ***Required*** Specifies the input *.fsl* file.

**-i**                  : ***Optional*** Specify the intermediate destination folder.

**-l**                  : ***Optional*** Specify the target language or platform, which will use default if not passed.

**-I**                  : ***Optional*** Specify include paths which may contained shared shader headers.

**--verbose**           : ***Optional*** Outputs detailed information when generating shaders.

**--compile**           : ***Optional*** To compile final binary that will be used at run time.

**--compile**           : ***Optional*** To compile shaders with debug information which allows.

**--rootSignature**     : ***Optional Direct3D12 only*** provide the name of root signature.

**--incremental**       : ***Optional*** compile only non existing files at destination.

**--mp**                : ***Optional*** Specify number of processes to use.

**--reloadServerPort**  : ***Optional*** Specify port that reload server would use.

**--cache-args**        : ***Optional*** Caches arguments used to invoke `fsl.py` and also generate `reload-server.txt` used by ReloadServer client application.


If compilation is requested, the tool will attempt to locate appropriate compilers using env variables:

```

DIRECT3D12: $(FSL_COMPILER_DXC) (if not set, will default to "The-Forge/ThirdParty/OpenSource/DirectXShaderCompiler/bin/x64/dxc.exe")
METAL:      $(FSL_COMPILER_METAL) (if not set, will default to "'C:/Program Files/METAL Developer Tools/macos/bin/metal.exe'")
ORBIS:      $(SCE_ORBIS_SDK_DIR)/host_tools/bin/orbis-wave-psslc.exe
PROSPERO:   $(SCE_PROSPERO_SDK_DIR)/host_tools/bin/prospero-wave-psslc.exe
VULKAN:     $(VULKAN_SDK)/Bin/glslangValidator.exe
XBOX:       $(GXDKLATEST)/bin/XboxOne/dxc.exe
SCARLETT:   $(GXDKLATEST)/bin/Scarlett/dxc.exe
```

### Static Analaysis :
On some of the platforms (Orbis, Prospero, Xbox) a static analysis step is run after compilation. 

`Occupancy: [theoretical]/[max] VGPR: [max live]([requested])/[max] SGPR: [max live]([requested])/[max] LDS: [used] Scratch: [used]`

The sections analyze in order:
- SIMD Occupancy
- Vector General Purpose Register (VGPR) Usage
- Scalar General Purpose Register (SGPR) Usage
- Local Data Share (LDS) Usage
- Scratch Memory (GDS) Usage

The analysis of register usage (VGPR/SGPR) differentiates between the maximum number of live registers for any instruction and the number of registers requested for the shader. It does not account for allocation granularity.

To warn of spilling, an error occurs during compilation if any scratch memory is used.

# ReloadServer

ReloadServer allows you to dynamically recompile FSL shaders at runtime by clicking the `Reload shaders` button in the Debug UI. 

It works by running a socket server on the host PC that is waiting for the device to send a shader recompile request.

Upon receiving this request, ReloadServer only recompiles shaders that have been modified for the requested project, and sends them back to the device where they are reloaded after being received.

In the case of a compilation/connection error, the message will be printed to the device logs so that the issue can quickly be inspected.

## How to use ReloadServer

ReloadServer is intended to work automatically, and will at most require the user to run a script once per session in order to use it.
It is already integrated into all of our projects on all platforms, so no setup is required. See [Manually running ReloadServer](#manually-running-ReloadServer) for details on how to integrate ReloadServer into a new project.

### PC
ReloadServer is run automatically on PC during App init, and killed during App exit. There is no input required from the user, it works completely automatically.

### Console/Mobile
For Console/Mobile projects, ReloadServer must be run manually on the host PC in order to allow dynamic recompilation of shaders on the device. See [Manually running ReloadServer](#manually-running-ReloadServer) for details on how to run the server manually.

The basic workflow is as follows:
1. (Non-PC only) Run `Common_3/Tools/ReloadServer/ReloadServer.sh` or `ReloadServer.bat` in a terminal
2. Run `App`
3. Modify FSL file
4. Click `Reload shaders` button
5. (if success) Observe updated shaders in `App`<br>
   (if failure) Error is printed in `App` logs

## Configuring ReloadServer
ReloadServer has only one configuration option - the server port. The default port is 6543. The port can also be configured in the following ways:

### Visual Studio
You can configure ReloadServer port using the `DevicePort` option in the `FSLShader` IDE configuration panel of your project settings. If `DevicePort` is empty, then the default port is used.

### XCode/CodeLite
On XCode/CodeLite, ReloadServer can be configured via the invocation of `fsl.py` in the build script located in the project settings. If no port is provided, then the default port is used. See [fsl.py integration](#integration-and-python-tool) for details.


## Platform details

### Android
ReloadServer on Android uses `adb reverse tcp:PORT tcp:PORT` in order to forward recompile requests from the device to the host PC via the USB cable, which avoids requiring a network connection. This is done automatically and requires no user input.

### iOS
ReloadServer on iOS requires the device to be connected to the same network as the host PC on which the ReloadServer daemon is running.

## Manually running ReloadServer

### Batch/shell script
ReloadServer can easily be run manually by using the platform-specific batch/shell script located at `Common_3/Tools/ReloadServer`.

#### Windows
```
.\Common_3\Tools\ForgeShadingLanguage\server\ReloadServer.bat
```
#### MacOS/Linux
```
./Common_3/Tools/ReloadServer/ReloadServer.sh
```

### Python script
The ReloadServer python script can be run from any directory, and is located at `Common_3/Tools/ReloadServer/ReloadServer.py`.
```
usage: ReloadServer.py [--port PORT] [--daemon] [--kill]
```
**--port PORT** : Choose port used by ReloadServer

**--kill**      : Kill currently running ReloadServer (**--daemon** is ignored if this is passed).

**--daemon**    : Run ReloadServer as a daemon process instead of directly in the terminal.

Only one server can run on a given port (regardless if running as daemon process or not). 

If there is already a server running on the given port, the server script will print a message and exit instead of running another server on that port. 

This can be useful when debugging potential issues.


## ReloadServer errors and debugging

When an error occurs during shader recompilation, the error message is sent to 3 different locations:
1. Printed to `stdout` in `ReloadServer.py` script - useful for debugging when running directly from terminal
2. Written to `server-log.txt` next to `ReloadServer.py` - useful for debugging the daemon process
3. Sent to `App` and printed to device/IDE logs - useful for debugging errors in shader code

This error message can be one of two things:
1. Generic error message returned by `ReloadServer.py` (i.e. path sent by device does not exist).
2. Shader compile error returned by `fsl.py`. In this case the entire output `stdout` is the error message.

ReloadServer is designed to be as fast and responsive as possible, so errors in shader compilation do not cause `App` to stop running. 
The reasoning is that compiling/running `App` might take very long, whereas fixing ReloadServer issues can be done very quickly (and maybe can be done several times before `App` can restart even once).
In the case of every failure, ReloadServer prints a detailed message to the `App` logs about what might have gone wrong and how to fix it, which the developer can use to fix the issue (often in much less time than it takes to restart `App`). The following most common issues can all quickly be fixed by glancing at device logs:
- User programming error in shader code (most commonly a typo)
- ReloadServer is not running when requesting shader recompile (usually only on XCode/Codelite where it needs to be run manually)
