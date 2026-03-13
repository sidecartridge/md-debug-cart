# SidecarTridge Debug Cart

Repurposes the SidecarTridge Multi-device as a debug-output device for Atari ST, STE, Mega ST, and Mega STE software through the cartridge port. It captures debug output by reading from the cartridge address space and forwards it to a USB serial connection.

## 🚀 Installation

To install the Debug Cart app on your SidecarTridge Multi-device:

1. Launch the **Booster App** on your SidecarTridge Multi-device.
2. Open the Booster web interface.
3. In the **Apps** tab, select **"Debug Cart"** from the list of available apps.
4. Click **"Download"** to install the app to your SidecarTridge’s microSD card.
5. Once installed, select the app and click **"Launch"** to activate it.

After launching, the app will automatically run every time your Atari computer is powered on.

## 🧠 How It Works

Debug output is transmitted by reading from the cartridge address space at `0xFBxxxx`. Characters are encoded into address lines `A8-A1`. For example, the following code snippet sends the character `A`:

```c
#define CARTRIDGE_ROM3 0xFB0000ul
(void)(*((volatile short*)(CARTRIDGE_ROM3 + ('A'<<1))));
```

To receive the debug output, a USB serial port is exposed by the Raspberry Pi Pico on the SidecarTridge Multi-device. On modern operating systems, no special drivers are required. Any terminal program that supports serial ports can be used. Because this is not a physical UART, the configured baud rate does not matter.

When using USB debug output, connecting the Pico to a laptop running on battery power can help avoid ground loop issues.

This is usually not required, but may improve stability if you see unreliable serial output.

### 🐾 Debug Cart in the wild: EmuTOS

The [EmuTOS](https://emutos.sourceforge.io/) operating system for Atari computers includes built-in support for this debug output method. To enable it, set the `CARTRIDGE_DEBUG_PRINT` macro to `1` in the EmuTOS configuration and recompile the OS.

If you would rather not compile the operating system yourself, you can use the [EmuTOS Building Tool by Thorsten Otto](https://tho-otto.de/emutos/) and enable the `CARTRIDGE_DEBUG_PRINT` option in the configuration.

Then, open a terminal program on your computer, connect to the USB serial port exposed by the SidecarTridge Multi-device, and power on your Atari. You should see debug output from EmuTOS in the terminal.


## ✨ Benefits

- It is fast. Accessing the cartridge port only takes a few CPU cycles, so the timing impact is much lower than with MFP serial output or on-screen debug output.
- No hardware initialization is needed. Cartridge-port access is available without prior setup.

## 🕹️ Runtime Behavior

The firmware starts capturing debug bytes as soon as the app is launched and forwards them to the USB serial connection.

### 🔁 System Reset Behavior

The app is resistant to Atari system resets. Pressing the reset button on the Atari does not stop the firmware.

### 🔌 Power Cycling

When you power off and on your Atari, the app starts again with the SidecarTridge Multi-device.

### ❌ SELECT Button Behavior

A short press on the SELECT button jumps back to the Booster menu. A long press resets the device.

## 🛠️ Setting Up the Development Environment

This project is based on the [SidecarTridge Multi-device Microfirmware App Template](https://github.com/sidecartridge/md-microfirmware-template).  
To set up your development environment, please follow the instructions provided in the [official documentation](https://docs.sidecartridge.com/sidecartridge-multidevice/programming/).

## 🙏 Acknowledgements

This project is a direct migration of the Atari ST Debug Cart by and based on the work of [Christian Zietz](https://github.com/czietz/atari-debug-cart). The migration was done with OpenAI 5.3-Codex, so the implementation remains closely aligned with the original code, with only the changes needed to fit the SidecarTridge Multi-device microfirmware layout. The original code is licensed under the GNU General Public License v3.0, and this project continues under the same terms.

## 📄 License

This project is licensed under the **GNU General Public License v3.0**.  
See [LICENSE](LICENSE) for full terms.

## 🤝 Contributing

Made with ❤️ by [SidecarTridge](https://sidecartridge.com)
