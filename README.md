# Snapmaker 2.0 Controller Firmware - Ultimate_Extruder_Mod

This is a direct fork of official [Official Snapmaker Controller Repo](https://github.com/Snapmaker/Snapmaker2-Controller) to make a version of firmware compatible with Bondtech/SliceEngineering tech.
This will be updated with latest changes introduced by Snapmaker.

Please read more about the firmware at [the main branch readme](https://github.com/Snapmaker/Snapmaker2-Controller/blob/main/README.md)

## Development

### Setup Development Environment

As of recommended in Marlin's development settings, we use **Visual Studio Code** and **PlatformIO IDE** to develop Snapmaker2-Controller. 

- Follow [Setting up Visual Studio Code](https://code.visualstudio.com/docs/setup/setup-overview) to install and setup **VSCode**.

- Follow the [guide](https://platformio.org/install/ide?install=vscode) to install PlatformIO extension in **VSCode**.
- Clone [Snapmaker2-Controller repo](https://github.com/Snapmaker/Snapmaker2-Controller) using Git to your local folder.

```shell
> git clone git@github.com:Snapmaker/Snapmaker2-Controller.git
```

- Open downloaded repo in **VSCode**
  - Use the **Open Folder…** command in the **VSCode** **File** menu
  - Then choose top folder of **Snapmaker2-Controller** in your location
- After opening the source code in **VSCode**, you will see these icons at the bottom status bar，it also indicates PlatformIO has been installed successfully.

![VSCode with PlatformIO](https://user-images.githubusercontent.com/3749551/98325327-814d3200-2029-11eb-9dd8-df9bee2dcbad.png)

### Ensure your changes are loaded

- The machine will not load new firmware if the version string remains the same
- You must update [Marlin/src/inc/Version.h](https://github.com/Snapmaker/Snapmaker2-Controller/blob/main/Marlin/src/inc/Version.h) to change the `SHORT_BUILD_VERSION` or your changes will not be loaded when flashing the firmware.

### Compile the code

- To compile the code, you have two ways:
  - click the **build** icon in status bar
  - click the **terminal** icon to open terminal, then type command ***pio run***

NOTE: if you build the source for first time, PlatformIO will download the relative libraries and toochains. It may take a few minutes.

- After PlatformIO finishing the build, you will get two images:
  - `(PROJECT FOLDER)/.pioenvs/GD32F105/firmware.bin`: image to be programmed into main controller
  - `(PROJECT FOLDER)/.pioenvs/GD32F105/firmware.elf`: image used to debug the firmware (in online debug tools like Ozone) 

- To clean previous build, just click the **clean** icon, or type command ***pio run -t clean*** in the terminal.

### Program compiled firmware to main controller

#### With PlatformIO CLI

After building, type below command in VSCode terminal

```
> pio run -t pack
```

Then you will get below firmwares in the folder `(PROJECT FOLDER)/release`:

- `firmware.bin`: raw binary of firmware.
- `firmware.elf`: firmware with debug information.
- `SM2_MC_APP_{xxx such as V4.0.0}_{xxx such as 20201222}.bin`: minor image of module, can be used to generate major image
- `Snapmaker_{xxx: version such as V4.0.0}_{xxx: date such as 20201222}.bin`: major image which can be used to upgrade modules with USB stick

Finally, copy the major image to your USB stick and upgrade your machine follow the instructions in [How to update Firmware](https://forum.snapmaker.com/t/snapmaker-2-0-firmware-updates-and-downloads/5443/10) section.

#### With Luban

You need to install [Luban](https://github.com/Snapmaker/Luban) to package the compiled firmware.

First, Open **Settings** -> **Firmware Tool** in Luban, upload the compiled `firmware.bin`, click **Compile and Export**. You will get a file with name like `Snapmaker2_V3.2.0_20201117.bin`, this is the packaged update file to be programmed.

Then, Update your firmware via USB follow the instructions in [How to update Firmware](https://forum.snapmaker.com/t/snapmaker-2-0-firmware-updates-and-downloads/5443/10) section.

## License

Snapmaker2-Controller is released under terms of the GPL-3.0 License.

Terms of the license can be found in the LICENSE file or at https://www.gnu.org/licenses/gpl-3.0.en.html.
