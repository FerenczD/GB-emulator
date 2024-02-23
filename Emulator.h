#pragma once
#ifndef _EMULATOR_H
#define _EMULATOR_H

/* Type definitions */
typedef unsigned char BYTE;
typedef char SIGNED_BYTE;
typedef unsigned short WORD;
typedef signed short SIGNED_WORD;

typedef bool (*PauseFunc)();
typedef void (*RenderFunc)();

/* Constants */
#define CARTRIDGE_SIZE      0x200000  // Game cartridge memory size
#define ROM_SIZE            0x10000   // CPU ROM size
#define SCREEN_X_AXIS_SIZE  160       // X-Axis resolution
#define SCREEN_Y_AXIS_SIZE  144       // Y-axis resolution
#define SCREEN_RGB_SIZE     3         // Pixel RGB code 

#define FLAG_MASK_Z 128               // Register F flags
#define FLAG_MASK_N 64
#define FLAG_MASK_H 32
#define FLAG_MASK_C 16
#define FLAG_Z 7
#define FLAG_N 6
#define FLAG_H 5
#define FLAG_C 4

#define TIMA  0xFF05   // Timer registers
#define TMA   0xFF06
#define TMC   0xFF07

class Emulator
{
  public:
    Emulator (void);
    ~Emulator(void);

    bool LoadRom(const std::string& romName);
    void Update();
		void StopGame();
		BYTE ExecuteNextOpcode();

		void DoTimers(int cycles);
		void DoGraphics(int cycles);
		void DoInput();
		void DoInterupts();

    BYTE m_ScreenData[SCREEN_X_AXIS_SIZE][SCREEN_Y_AXIS_SIZE][SCREEN_RGB_SIZE];

  private:

    // Registers can be used as single 8-bit reg or 16-bit reg combined
    union Register
    {
      WORD reg;
      struct {
        BYTE lo;      // Little-Endianess
        BYTE hi;
      };
    };

		enum COLOUR
		{
			WHITE,
			LIGHT_GRAY,
			DARK_GRAY,
			BLACK
		};

    BYTE ExecuteNextOpcode();
    bool ResetCPU();
    void ResetScreen();
    void WriteByte(WORD address, BYTE data);
    BYTE ReadMemory(WORD address) const;
    void CreateRamBanks(int numBanks);
		void RequestInterupt(int bit);
    void DoInterrupts();
    void DoGraphics(int cycles);
    void ServiceInterrupt(int num);
    void PushWordOntoStack(WORD word);
    BYTE GetJoypadState() const;
    void KeyPressed(int key);
    void KeyReleased(int key);
    BYTE GetJoypadState() const;

    void IssueVerticalBlank();
    void DrawCurrentLine();
    void SetLCDStatus();
    BYTE GetLCDMode() const ;
    void DrawScanLine();
    void RenderSprites(BYTE lcdControl);
    void RenderBackground(BYTE lcdControl);
    COLOUR GetColour(BYTE colourNum, WORD address) const;

		unsigned long long	m_TotalOpcodes;

		BYTE				m_JoypadState;
		bool				m_GameLoaded;
		BYTE				m_Rom[ROM_SIZE];
    BYTE				m_GameBank[CARTRIDGE_SIZE];   // Game cartridge can only store 0x200000
    std::vector<BYTE*>	m_RamBank ;
    WORD				m_ProgramCounter;
		bool				m_EnableRamBank;

    Register    m_RegisterAF;
    Register    m_RegisterBC;
    Register    m_RegisterDE;
    Register    m_RegisterHL;

		Register    m_StackPointer;

		bool				m_EnableInterupts ;
		bool        m_PendingInteruptDisabled ;
		bool				m_PendingInteruptEnabled ;

		int					m_CurrentRomBank;
		int					m_CurrentRamBank;
    bool				m_UsingMemoryModel16_8;
		int					m_RetraceLY;
		int					m_CyclesThisUpdate;

    // Debug variables
		bool				m_DebugPause;
		bool				m_DebugPausePending;
		bool				m_DoLogging;
		BYTE				m_DebugValue;

		PauseFunc		m_TimeToPause;
		RenderFunc  m_RenderFunc;

		bool				m_UsingMBC1;  // Bank switching 1 (Most common type)
    bool        m_UsingMBC2;  // Not too many games use this 
		int					m_TimerVariable;
		int					m_CurrentClockSpeed;
		int					m_DividerVariable;
		bool				m_Halted;

};


#endif

/*
  General Memory Map
  0000-3FFF   16KB ROM Bank 00     (in cartridge, fixed at bank 00)
  4000-7FFF   16KB ROM Bank 01..NN (in cartridge, switchable bank number)
  8000-9FFF   8KB Video RAM (VRAM) (switchable bank 0-1 in CGB Mode)
  A000-BFFF   8KB External RAM     (in cartridge, switchable bank, if any)
  C000-CFFF   4KB Work RAM Bank 0 (WRAM)
  D000-DFFF   4KB Work RAM Bank 1 (WRAM)  (switchable bank 1-7 in CGB Mode)
  E000-FDFF   Same as C000-DDFF (ECHO)    (typically not used)
  FE00-FE9F   Sprite Attribute Table (OAM)
  FEA0-FEFF   Not Usable
  FF00-FF7F   I/O Ports
  FF80-FFFE   High RAM (HRAM)
  FFFF        Interrupt Enable Register
*/