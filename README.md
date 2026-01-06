CHIP-8 Emulator
A fully-featured CHIP-8 interpreter written in C with SDL2, implementing all 35 original opcodes with modern enhancements including real-time performance metrics, multiple color themes, and comprehensive debugging capabilities.

🎮 Overview
CHIP-8 is an interpreted programming language developed in the 1970s for vintage microcomputers. This emulator accurately recreates the CHIP-8 virtual machine, allowing classic games and programs to run on modern hardware with enhanced features for a better user experience.
Key Features

✅ Complete CHIP-8 Implementation - All 35 original opcodes fully implemented and tested
🎨 Multiple Color Themes - 4 built-in themes (Classic B&W, Matrix Green, Neon Vaporwave, Gameboy)
📊 Real-time Performance Metrics - FPS counter, instructions per frame, and total instruction count
🐛 Debug Mode - Instruction-by-instruction execution trace with register states
🎵 Audio Support - Square wave audio generation at 440Hz
⚙️ Configurable - CPU speed, window scaling, and quirks modes (CHIP-8 / SUPERCHIP)
🎯 Drag & Drop - Load ROMs by dragging files onto the window
⌨️ Intuitive Keyboard Mapping - QWERTY layout mapped to CHIP-8's hexadecimal keypad

📸 Screenshots
Classic Mode              Matrix Theme              Performance Metrics
┌──────────────┐         ┌──────────────┐         ┌──────────────┐
│   ████████   │         │   ████████   │         │ FPS 60       │
│   ██    ██   │         │   ██    ██   │         │ IPF 11       │
│   ██    ██   │         │   ██    ██   │         │ INS 245k     │
│   ████████   │         │   ████████   │         └──────────────┘
└──────────────┘         └──────────────┘
🚀 Quick Start
Prerequisites

GCC or Clang compiler
SDL2 development libraries
Make (optional)

Installation
Linux (Ubuntu/Debian):
bashsudo apt-get install libsdl2-dev
git clone https://github.com/yourusername/chip8-emulator.git
cd chip8-emulator
gcc main.c -o chip8 -lSDL2 -lm -Wall -Wextra
macOS:
bashbrew install sdl2
git clone https://github.com/yourusername/chip8-emulator.git
cd chip8-emulator
gcc main.c -o chip8 -I/usr/local/include -L/usr/local/lib -lSDL2 -lm
Windows (MinGW):
bash# Download SDL2 development libraries from https://www.libsdl.org/
gcc main.c -o chip8.exe -I./SDL2/include -L./SDL2/lib -lmingw32 -lSDL2main -lSDL2
Usage
bash# Basic usage
./chip8 <rom_file>

# With options
./chip8 roms/pong.ch8 --scale-factor 20 --metrics --debug

# Available options:
#   --scale-factor N  : Set window scale (default: 20)
#   --debug          : Enable instruction tracing
#   --metrics        : Show performance metrics on startup
🎮 Controls
CHIP-8 Keypad Mapping
The original CHIP-8 hexadecimal keypad is mapped to a QWERTY keyboard layout:
CHIP-8 Keypad    →    QWERTY Keyboard
┌─────────────┐      ┌─────────────┐
│ 1  2  3  C  │      │ 1  2  3  4  │
│ 4  5  6  D  │  →   │ Q  W  E  R  │
│ 7  8  9  E  │      │ A  S  D  F  │
│ A  0  B  F  │      │ Z  X  C  V  │
└─────────────┘      └─────────────┘
Emulator Controls
KeyActionSPACEPause / ResumeESCQuit emulator=Reset / Reload ROMMCycle color themesIToggle performance metricsO / PVolume down / up
🏗️ Architecture
Project Structure
chip8-emulator/
├── main.c              # Complete emulator implementation (~1200 lines)
├── roms/              # Sample ROM files
│   ├── IBM_Logo.ch8
│   ├── Pong.ch8
│   └── Tetris.ch8
├── README.md
└── Makefile           # Optional build automation
Core Components

Emulation Core (chip8_t structure)

4KB RAM
16 8-bit registers (V0-VF)
16-bit index register (I)
16-bit program counter (PC)
12-level stack
64×32 monochrome display
Two 60Hz timers


SDL Integration (sdl_t structure)

Window management
Hardware-accelerated rendering
Event handling (keyboard/mouse)
Audio generation (square wave)


Configuration (config_t structure)

Customizable CPU speed (default: 700 Hz)
Window scaling factor
Color themes
Debug options
Quirks modes (CHIP-8 / SUPERCHIP)



Memory Layout
0x000-0x1FF : Reserved (Font data + Interpreter)
0x200-0xFFF : Program ROM and working RAM
🛠️ Technical Details
Implemented Opcodes
All 35 CHIP-8 opcodes are fully implemented:

0x0NNN - System operations (Clear, Return)
0x1NNN - Jump
0x2NNN - Call subroutine
0x3XNN - Skip if VX == NN
0x4XNN - Skip if VX != NN
0x5XY0 - Skip if VX == VY
0x6XNN - Set VX = NN
0x7XNN - Add NN to VX
0x8XY0-E - Arithmetic and logic operations
0x9XY0 - Skip if VX != VY
0xANNN - Set I = NNN
0xBNNN - Jump to V0 + NNN
0xCXNN - VX = random & NN
0xDXYN - Draw sprite (with collision detection)
0xEX9E/A1 - Skip if key pressed/not pressed
0xFX07-65 - Timer, font, BCD, memory operations

Quirks Support
The emulator supports both original CHIP-8 and SUPERCHIP behaviors:
InstructionCHIP-8SUPERCHIP0x8XY6/E (Shift)Uses VYUses VX0xFX55/65 (Load/Store)Increments IDoesn't increment I0x8XY1/2/3 (Logic)Resets VFPreserves VF
Synchronization
The emulator maintains accurate timing:

CPU: ~700 instructions per second (configurable)
Display: 60 Hz refresh rate
Timers: Decrement at 60 Hz
Audio: 440 Hz square wave generation

Precision timing is achieved using SDL's high-resolution performance counters.
🎨 Color Themes
ThemeForegroundBackgroundStyleClassicWhiteBlackOriginal CHIP-8 aestheticMatrixGreenBlackCyberpunk terminal styleNeonMagentaDark BlueVaporwave aestheticGameboyBeigePurpleNintendo handheld tribute
📊 Performance Metrics
When enabled, the emulator displays:

FPS: Current frames per second (target: 60)
IPF: Instructions executed per frame
INS: Total instructions executed (in thousands)

Metrics use a custom bitmap font rendered directly with SDL for zero external dependencies.
🧪 Testing
The emulator has been tested with:

BC_test.ch8 - Comprehensive opcode test suite
IBM_Logo.ch8 - Basic display and font test
Pong.ch8 - Input and collision detection
Tetris.ch8 - Complex sprite manipulation
Space Invaders.ch8 - Full game logic

All tests pass successfully with accurate timing and display.
🐛 Debug Mode
Enable debug mode to see detailed execution traces:
bash./chip8 roms/test.ch8 --debug
Output format:
Address: 0x0200, Opcode: 0x00E0 Desc: Clear screen
Address: 0x0202, Opcode: 0x6A0F Desc: Set register VA = NN (0x0F)
Address: 0x0204, Opcode: 0xA21E Desc: Set I to NNN (0x021E)
...
🚧 Known Limitations

No SUPERCHIP extended instruction set (128×64 resolution)
No XO-CHIP color support
No save states functionality
Audio is monophonic (single channel)

🔮 Future Enhancements

 SUPERCHIP-1.1 full compatibility (128×64 mode)
 XO-CHIP support (colors, extended memory)
 Save state / Load state functionality
 Rewind feature for debugging
 GUI ROM selector
 Configurable key bindings
 CRT shader effects
 Netplay support

📚 Resources

CHIP-8 Technical Reference
CHIP-8 Wikipedia
Awesome CHIP-8
Test ROMs
