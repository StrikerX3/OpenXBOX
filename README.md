**This project has been migrated to https://github.com/StrikerX3/viXen**

**If you're looking for a functional Xbox emulator, check out [XQEMU](http://xqemu.com/)
or [Cxbx-Reloaded](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded).**

---

# OpenXBOX
Open-Source (Original) Xbox Emulation Project

The current state of this thing is just a tad bit more tangible than vaporware.
Essentially right now it just initializes an x86 system (courtesy of one of the
various virtualization platforms supported by the emulator) and runs whatever
is in ROM, which is provided by the user.

No networking, no audio, no graphics, no games... yet ;).

The initial goal is to emulate the original Xbox at a low level. The user will
have to provide their own dump of the MCPX and BIOS ROMs from an Xbox machine,
as well as the appropriate game media dump in XISO format or from an extracted
directory.

In the future, OpenXBOX will attempt to provide high level emulation of the
kernel in order to sidestep the ROM requirements. It is a long way off, as
research on the kernel is still incipient and existing implementations are
incomplete, incorrect or straight up copies of illegally obtained code.

How to Build
------------
OpenXBOX uses [CMake](https://cmake.org/) build files to generate projects for
your preferred development platform. OpenXBOX contains multiple modules, as
described in the [Project Structure](#project-structure) section below.

You will need to specify a CPU module for OpenXBOX to run. The CMake option
`CPU_MODULE` gives you a choice of default modules bundled with OpenXBOX that
can be used for development and release builds.

### Windows
You'll need CMake 3.8 or later and [Visual Studio Community 2017](https://www.visualstudio.com/downloads/).

Your choices of CPU modules for this platform are:
- `haxm`: [Intel HAXM](https://software.intel.com/en-us/articles/intel-hardware-accelerated-execution-manager-intel-haxm).
Requires an Intel processor with VT-x and runs on any version of Windows.
- `whvp`: [Windows Hypervisor Platform](https://docs.microsoft.com/en-us/virtualization/api/).
Requires Windows 10 Pro with the [April 2018 Update](https://support.microsoft.com/en-us/help/4028685/windows-10-get-the-update)
or later. You'll also need to go to the Windows Features panel and enable the
Windows Hypervisor Platform feature. Note that in doing so, you'll be unable to
use any other virtualization platform (such as VirtualBox, VMware Player or
HAXM). Disable the feature if you wish to continue using those platforms.

```
> mkdir build
> cd build
> cmake -G "Visual Studio 15 2017" .. -DCPU_MODULE=<cpu module>         # for 32-bit builds
> cmake -G "Visual Studio 15 2017 Win64" .. -DCPU_MODULE=<cpu module>   # for 64-bit builds
```
The .sln file will be generated in the build folder, ready to build.

### Linux
You'll need CMake 3.1 or later. The Linux build uses [KVM](https://www.linux-kvm.org/page/Main_Page).

```
$ sudo apt-get install cmake
$ mkdir build; cd build
$ cmake .. -DCPU_MODULE=kvm && make
$ cd src/cli
$ ./openxbox-cli -c <path-to-MCPX-ROM> -b <path-to-BIOS-ROM> -x <path-to-XBE> -m [debug|retail]
```

### macOS
macOS is currently unsupported. Feel free to submit a pull request to add
support for this platform!

Project Structure
-----------------
OpenXBOX is split into multiple modules:
- `core`: the core of the emulator, providing basic emulation logic and a
module interface for the various pieces of hardware that composes the Xbox.
This is a static library meant to be used by front-end engines.
- `cli`: a command line front-end for OpenXBOX.
- `common`: common code shared across all modules.
- `module-common`: contains common definitions and types for OpenXBOX modules.
- `cpu-module`: defines the interface and basic types for CPU modules.
- `cpu-module-haxm`: Windows-only CPU module implementation using [Intel HAXM](https://github.com/intel/haxm).
- `cpu-module-whvp`: Windows-only CPU module implementation using the [Windows Hypervisor Platform](https://docs.microsoft.com/en-us/virtualization/api/).
- `cpu-module-kvm`: Linux-only CPU module implementation using [KVM](https://www.kernel.org/doc/Documentation/virtual/kvm/api.txt)

Debugging Guest Code
--------------------
The guest can be debugged using the GDB debugger. Once enabled, the emulator
will open a TCP socket upon startup and wait for the GDB debugger to connect.
Once connected, you can examine the CPU state, set breakpoints, single-step
instructions, etc. A sample .gdbinit file is provided with useful GDB default
settings to be loaded when you start GDB in this directory.

Alternatively, on Windows, you can perform kernel debugging of the virtual Xbox
by creating a linked pair of virtual null-modem serial ports with [com0com](http://com0com.sourceforge.net/).
Use a Debug BIOS ROM and attach one side of the pair to the first Super I/O
serial port, then connect [WinDbg or KD](https://docs.microsoft.com/en-us/windows-hardware/drivers/debugger/)
to the other side to begin kernel debugging.
