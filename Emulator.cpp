/*http://www.codeslinger.co.uk/pages/projects/gameboy/lcd.html*/

#include "Config.h"
#include "Emulator.h"
#include "LogMessages.h"
#include <string.h>

/* Local constants */
#define RETRACE_START 456
#define VERTICAL_BLANK_SCAN_LINE 0x90
#define VERTICAL_BLANK_SCAN_LINE_MAX 0x99
#define RETRACE_START 456

//////////////////////////////////////////////////////////////////

Emulator::Emulator(void) :
	m_GameLoaded(false)
	,m_CyclesThisUpdate(0)
	,m_UsingMBC1(false)
	,m_EnableRamBank(false)
	,m_UsingMemoryModel16_8(true)
	,m_EnableInterupts(false)
	,m_PendingInteruptDisabled(false)
	,m_PendingInteruptEnabled(false)
	,m_RetraceLY(RETRACE_START)
	,m_JoypadState(0)
	,m_Halted(false)
	,m_TimerVariable(0)
	,m_CurrentClockSpeed(1024)
	,m_DividerVariable(0)
	,m_CurrentRamBank(0)
	,m_DebugPause(false)
	,m_DebugPausePending(false)
	,m_TimeToPause(NULL)
	,m_TotalOpcodes(0)
	,m_DoLogging(false)
{
	ResetScreen( );
}

//////////////////////////////////////////////////////////////////

Emulator::~Emulator(void)
{
	for (std::vector<BYTE*>::iterator it = m_RamBank.begin(); it != m_RamBank.end(); it++)
		delete[] (*it) ;
}

//////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////

static int hack = 0;
static long long counter9 = 0;

// remember this update function is not the same as the virtual update function. This gets specifically
// called by the Game::Update function. This way I have control over when to execute the next opcode. Mainly for the debug window
void Emulator::Update()
{
  hack++;

  m_CyclesThisUpdate = 0;
  const int m_TargetCycles = 70221; // Complete screen refresh.. NOTE: Shouldnt it be 70224 based	on doc?

  while ((m_CyclesThisUpdate < m_TargetCycles))
  {
    if (m_DebugPause)
      return;

    if (m_DebugPausePending)
    {
      if (m_TimeToPause && (m_TimeToPause() == true))
      {
        m_DebugPausePending = false;
        m_DebugPause = true;

        return;
      }
    }

    int currentCycle = m_CyclesThisUpdate;
    BYTE opcode = ExecuteNextOpcode();
    int cycles = m_CyclesThisUpdate - currentCycle;

    DoTimers(cycles);
		DoGraphics(cycles);
		DoInput();
		DoInterupts();
  }

	counter9 += m_CyclesThisUpdate;
	m_RenderFunc();
}

//////////////////////////////////////////////////////////////////

void Emulator::DoInput( )
{

}

//////////////////////////////////////////////////////////////////

bool Emulator::InitGame(RenderFunc func)
{
  m_RenderFunc = func;
  return ResetCPU();
}

//////////////////////////////////////////////////////////////////

void Emulator::DoGraphics(int cycles)
{
  SetLCDStatus();

	// count down the LY register which is the current line being drawn. When reaches 144 (0x90) its vertical blank time
  if (TestBit(ReadMemory(0xFF40), 7))
    m_RetraceLY -= cycles;  //It takes 456 CPU clock cycles to draw one line

  // if gone past scanline 153 reset to 0
  if (m_Rom[0xFF44] > VERTICAL_BLANK_SCAN_LINE_MAX) // 153
    m_Rom[0xFF44] = 0;

  if (m_RetraceLY <= 0)
    DrawCurrentLine();
}

//////////////////////////////////////////////////////////////////
static int timerhack = 0;

void Emulator::DoTimers(int cycles)
{
  BYTE timerAtts = m_Rom[TMC];

  m_DividerVariable += cycles;

  // Clock must be enabled to update the clock
  if (TestBit(timerAtts, 2))
  {
    m_TimerVariable += cycles;

    // Time to increment the timer
    if (m_TimerVariable >= m_CurrentClockSpeed)
    {
      m_TimerVariable = 0;

      bool overflow = false;

      if(m_Rom[TIMA] == 0xFF)
        overflow = true;

      m_Rom[TIMA]++;

      if (overflow)
      {
        timerhack++;

        m_Rom[TIMA] = m_Rom[TMA];

        // Request the interrupt
        RequestInterupt(2);
      }
    }
  }

  if (m_DividerVariable >= 256)
  {
    m_DividerVariable = 0;
    m_Rom[0xFF04];
  }

}

//////////////////////////////////////////////////////////////////
void Emulator::DoInterupts()
{
  /* Check interrupts are enabled */
  if (m_EnableInterupts)
  {
    // Has any interrupt occurred?
    BYTE requestFlag = ReadMemory(0xFF0F);
    if (requestFlag > 0)
    {
      // Which requested interrupt has the highest priority?
      for (int bit = 0; bit < 8; bit++)
      {
        if (TestBit(requestFlag, bit))
        {
          // Interrupt requested but is it enabled? 
          BYTE enableReg = ReadMemory(0xFFFF);
          if (TestBit(enableReg, bit))
          {
            ServiceInterrupt(bit);
          }
        }
      }
    }
  }
}

//////////////////////////////////////////////////////////////////

void Emulator::ServiceInterrupt(int num)
{
  // Save current program counter
  PushWordOntoStack(m_ProgramCounter);
  m_Halted = false;

  char buffer[200];
	sprintf(buffer, "servicing interupt %d", num);
	LogMessage::GetSingleton()->DoLogMessage(buffer, false);

  switch(num)
  {
    case 0: m_ProgramCounter = 0x40; break; // V-Blank
    case 1: m_ProgramCounter = 0x48; break; // LCD-State
    case 2: m_ProgramCounter = 0x50; break; // Timer
    case 4: m_ProgramCounter = 0x60; break; // JoyPad
    default: assert(false); break;
  }

  m_EnableInterupts = false;
  m_Rom[0xFF0F] = BitReset(m_Rom[0xFF0F], num);
}

//////////////////////////////////////////////////////////////////

void Emulator::PushWordOntoStack(WORD word)
{
  BYTE hi = word >> 8;
  BYTE lo = word && 0xFF;

  m_StackPointer.reg--;
  WriteByte(m_StackPointer.reg, hi);
  m_StackPointer.reg--;
  WriteByte(m_StackPointer.reg, lo); 
}

//////////////////////////////////////////////////////////////////

WORD Emulator::PopWordOffStack( )
{
	WORD word = ReadMemory(m_StackPointer.reg+1) << 8;
	word |= ReadMemory(m_StackPointer.reg);
	m_StackPointer.reg+=2;

	return word ;
}

//////////////////////////////////////////////////////////////////

bool Emulator::LoadRom(const std::string& romName)
{
  if (m_GameLoaded)
    StopGame();

  m_GameLoaded = true;

  memset(m_Rom, 0, sizeof(m_Rom));
  memset(m_GameBank, 0 , sizeof(m_GameBank));

  FILE *in;
  in = fopen(romName.c_str(), "rb");
  fread(m_GameBank, 1, CARTRIDGE_SIZE, in);
  fclose(in);

  memcpy(&m_Rom[0], &m_GameBank[0], 0x8000);  // ROM only stores first 0x8000 from cartridge

  m_CurrentRomBank = 1;

  return true;
}

void Emulator::ResetScreen()
{
  for (int x = 0; x < SCREEN_X_AXIS_SIZE; x++)
  {
    for (int y = 0; y < SCREEN_Y_AXIS_SIZE; y++)
    {
      m_ScreenData[x][y][0] = 255;
      m_ScreenData[x][y][1] = 255;
      m_ScreenData[x][y][2] = 255;
    }
  }
}

//////////////////////////////////////////////////////////////////

void Emulator::StopGame( )
{
	m_GameLoaded = false ;
}

//////////////////////////////////////////////////////////////////

bool Emulator::ResetCPU()
{
	ResetScreen( );
	m_DoLogging = false;
	m_CurrentRamBank = 0;
	m_TimerVariable = 0;
	m_CurrentClockSpeed = 1024;
	m_DividerVariable = 0;
	m_Halted = false;
	m_TotalOpcodes = 0;
	m_JoypadState = 0xFF;
	m_CyclesThisUpdate = 0;
	m_ProgramCounter = 0x100;
	m_RegisterAF.hi = 0x1;
	m_RegisterAF.lo = 0xB0;
	m_RegisterBC.reg = 0x0013;
	m_RegisterDE.reg = 0x00D8;
	m_RegisterHL.reg = 0x014D;
	m_StackPointer.reg = 0xFFFE;

	m_Rom[0xFF00] = 0xFF;
	m_Rom[0xFF05] = 0x00;
	m_Rom[0xFF06] = 0x00;
	m_Rom[0xFF07] = 0x00;
	m_Rom[0xFF10] = 0x80;
	m_Rom[0xFF11] = 0xBF;
	m_Rom[0xFF12] = 0xF3;
	m_Rom[0xFF14] = 0xBF;
	m_Rom[0xFF16] = 0x3F;
	m_Rom[0xFF17] = 0x00;
	m_Rom[0xFF19] = 0xBF;
	m_Rom[0xFF1A] = 0x7F;
	m_Rom[0xFF1B] = 0xFF;
	m_Rom[0xFF1C] = 0x9F;
	m_Rom[0xFF1E] = 0xBF;
	m_Rom[0xFF20] = 0xFF;
	m_Rom[0xFF21] = 0x00;
	m_Rom[0xFF22] = 0x00;
	m_Rom[0xFF23] = 0xBF;
	m_Rom[0xFF24] = 0x77;
	m_Rom[0xFF25] = 0xF3;
	m_Rom[0xFF26] = 0xF1;
	m_Rom[0xFF40] = 0x91;
	m_Rom[0xFF42] = 0x00;
	m_Rom[0xFF43] = 0x00;
	m_Rom[0xFF45] = 0x00;
	m_Rom[0xFF47] = 0xFC;
	m_Rom[0xFF48] = 0xFF;
	m_Rom[0xFF49] = 0xFF;
	m_Rom[0xFF4A] = 0x00;
	m_Rom[0xFF4B] = 0x00;
	m_Rom[0xFFFF] = 0x00;
	m_RetraceLY = RETRACE_START;

	m_DebugValue = m_Rom[0x40];

	m_EnableRamBank = false;

	m_UsingMBC2 = false;

  // what kinda rom switching are we using, if any?
	switch(ReadMemory(0x147))
	{
		case 0: m_UsingMBC1 = false; break; // not using any memory swapping
		case 1:                               // Bank switching 1
		case 2:
		case 3 : m_UsingMBC1 = true; break; 
		case 5 : m_UsingMBC2 = true; break; // Bank switching 2
		case 6 : m_UsingMBC2 = true; break; 
		default: return false; // unhandled memory swappping, probably MBC2
	}

	// how many ram banks do we neeed, if any?
	int numRamBanks = 0;
	switch (ReadMemory(0x149))
	{
		case 0: numRamBanks = 0;break;
		case 1: numRamBanks = 1;break;
		case 2: numRamBanks = 1;break;
		case 3: numRamBanks = 4;break;
		case 4: numRamBanks = 16;break;
	}

	CreateRamBanks(numRamBanks);

	return true;
}

//////////////////////////////////////////////////////////////////

BYTE Emulator::ExecuteNextOpcode()
{
  BYTE opcode = m_Rom[m_ProgramCounter];

  // Valid addresses
	if ((m_ProgramCounter >= 0x4000 && m_ProgramCounter <= 0x7FFF) || (m_ProgramCounter >= 0xA000 && m_ProgramCounter <= 0xBFFF))
		opcode = ReadMemory(m_ProgramCounter);

	if (!m_Halted)
	{
		if (false)
		{
			char buffer[200];
			sprintf(buffer, "OP = %x PC = %x\n", opcode, m_ProgramCounter);
			LogMessage::GetSingleton()->DoLogMessage(buffer,false);
		}

		m_ProgramCounter++;
		m_TotalOpcodes++;

		ExecuteOpcode( opcode );
	}
	else
	{
		m_CyclesThisUpdate += 4;
	}

	// we are trying to disable interupts, however interupts get disabled after the next instruction
	// 0xF3 is the opcode for disabling interupt
	if (m_PendingInteruptDisabled)
	{
		if (ReadMemory(m_ProgramCounter-1) != 0xF3)
		{
			m_PendingInteruptDisabled = false;
			m_EnableInterupts = false;
		}
	}

	if (m_PendingInteruptEnabled)
	{
		if (ReadMemory(m_ProgramCounter-1) != 0xFB)
		{
			m_PendingInteruptEnabled = false;
			m_EnableInterupts = true;
		}
	}

	return opcode;
}
//////////////////////////////////////////////////////////////////

void Emulator::SetLCDStatus()
{
  // TODO: Change direct m_Rom reads to ReadMemory for consistency

  // LCD status register
  BYTE lcdStatus = m_Rom[0xFF41];

  // Check LCD is actually enabled. If not properly set LCD for IDLE
  if (TestBit(ReadMemory(0xFF40), 7) == false)
  {
    m_RetraceLY = RETRACE_START;    // Scanline back to 0
    m_Rom[0xFF44] = 0;

    // Mode gets set to 1 when disabled screen
    lcdStatus &= 0xFC;
    lcdStatus = BitSet(lcdStatus, 0);
    WriteByte(0xFF41, lcdStatus);

    return;
  }

  // LCD is enabled proceed with status update
  BYTE lY = ReadMemory(0xFF44);     // Current line
  BYTE currentMode = GetLCDMode();  // Current mode
  int  mode = 0;
  bool reqInt = false;

  // Set mode as vertical line if passed 144 (V-Blank = mode 1)
  if (lY >= VERTICAL_BLANK_SCAN_LINE)
  {
    // mode 1
    mode = 1;
    lcdStatus = BitSet(lcdStatus, 0);
    lcdStatus = BitReset(lcdStatus, 1);
    reqInt    = TestBit(lcdStatus, 4);
  }
  // Test for modes 2 and 3
  else
  {
    int mode2Bounds = (RETRACE_START - 80);   // Takes 80 cycles
    int mode3Bounds = (mode2Bounds - 172);    // Takes 172 cycles

    // mode 2
    if (m_RetraceLY >= mode2Bounds)
    {
      mode = 2;
      lcdStatus = BitSet(lcdStatus, 1);
      lcdStatus = BitReset(lcdStatus, 0);
      reqInt = TestBit(lcdStatus, 5);
    }
    // mode 3
    else if (m_RetraceLY >= mode3Bounds)
    {
      mode = 3;
      lcdStatus = BitSet(lcdStatus, 1);
      lcdStatus = BitSet(lcdStatus, 0);
    }
    // mode 0
    else
    {
      mode = 0;
			lcdStatus = BitReset(lcdStatus,1);
			lcdStatus = BitReset(lcdStatus,0);
			reqInt = TestBit(lcdStatus,3);
    }
  }

  // just enetered a new mode so request interrupt
  if (reqInt && (currentMode != mode))
    RequestInterupt(1);

  // Check for coincidence flag TODO: Undertsand better how this behaves
  if (lY == ReadMemory(0xFF45))
  {
    lcdStatus = BitSet(lcdStatus, 2);

    if (TestBit(lcdStatus, 6))
      RequestInterupt(1);
  }
  else
  {
    lcdStatus = BitReset(lcdStatus, 2);
  }

  WriteByte(0xFF41, lcdStatus);
}
//////////////////////////////////////////////////////////////////

BYTE Emulator::GetLCDMode() const
{
  BYTE lcdStatus = m_Rom[0xFF41];
  return lcdStatus & 0x3; // Only first 2 bits are mode
}

//////////////////////////////////////////////////////////////////
// writes a byte to memory. Remember that address 0 - 07FFF is rom so we cant write to this address

// Note that most of the switching mechanism can be found here https://web.archive.org/web/20140410105329/http://nocash.emubase.de/pandocs.htm#soundcontroller
// in the doc referred by the tutorial
void Emulator::WriteByte(WORD address, BYTE data)
{
	// writing to memory address 0x0 to 0x1FFF this disables writing to the ram bank. 0 disables, 0xA enables
  if (address <= 0x1FFF)
  {
    // Bank switching 1
    if (m_UsingMBC1)
    {
      if ((data & 0xF) == 0xA)
        m_EnableRamBank = true;
      else if (data == 0x0)
        m_EnableRamBank = false;
    }
    // Bank switching 2
    else if (m_UsingMBC2)
    {
      // bit 0 of upper byte must be 0
      if (TestBit(address, 8) == false)
      {
        if ((data & 0xF) == 0xA)
          m_EnableRamBank = true;
        else if (data == 0x0)
          m_EnableRamBank = false;
      }
    }
  }

	// if writing to a memory address between 2000 and 3FFF then we need to change rom bank
  else if ((address >= 0x2000) && (address <= 0x3FFF))
  {
    if (m_UsingMBC1)
    {
      if (data == 0x00)
        data++;
      
      data &= 0x1F;

      // Turn off the lower 5-bits
      m_CurrentRomBank &= 0xE0;

      // Combine the written data with the register
      m_CurrentRomBank |= data;

      // TODO: define log messages
			char buffer[256];
			sprintf(buffer, "Chaning Rom Bank to %d", m_CurrentRomBank);
			LogMessage::GetSingleton()->DoLogMessage(buffer, false);
    }
    else if (m_UsingMBC2)
		{
            data &= 0xF;
            m_CurrentRomBank = data;
		}
  }

  // writing to address 0x4000 to 0x5FFF switches ram banks (if enabled of course)
  else if ( (address >= 0x4000) && (address <= 0x5FFF))
	{
		if (m_UsingMBC1)
		{
			// are we using memory model 16/8
			if (m_UsingMemoryModel16_8)
			{
				// in this mode we can only use Ram Bank 0
				m_CurrentRamBank = 0;

				data &= 3;
				data <<= 5;

				if ((m_CurrentRomBank & 0x1F) == 0)
				{
					data++;
				}

				// Turn off bits 5 and 6, and 7 if it somehow got turned on.
				m_CurrentRomBank &= 0x1F;

				// Combine the written data with the register.
				m_CurrentRomBank |= data;

        // TODO: define log messages
				char buffer[256];
				sprintf(buffer, "Chaning Rom Bank to %d", m_CurrentRomBank);
				LogMessage::GetSingleton()->DoLogMessage(buffer, false);

			}
			else
			{
				m_CurrentRamBank = data & 0x3;

        // TODO: define log messages  
				char buffer[256];
				sprintf(buffer, "=====Chaning Ram Bank to %d=====", m_CurrentRamBank);
				LogMessage::GetSingleton()->DoLogMessage(buffer, false);

			}
		}
	}

	// writing to address 0x6000 to 0x7FFF switches memory model
	else if ( (address >= 0x6000) && (address <= 0x7FFF))
	{
		if (m_UsingMBC1)
		{
			// we're only interested in the first bit
			data &= 1;
			if (data == 1)
			{
				m_CurrentRamBank = 0;
				m_UsingMemoryModel16_8 = false;
			}
			else
				m_UsingMemoryModel16_8 = true;
		}
	}

  
	// from now on we're writing to cartirdge RAM

 	else if ((address >= 0xA000) && (address <= 0xBFFF))
 	{
 		if (m_EnableRamBank)
 		{
 		    if (m_UsingMBC1)
 		    {
          WORD newAddress = address - 0xA000;
          m_RamBank.at(m_CurrentRamBank)[newAddress] = data;
 		    }
 		}
 		else if (m_UsingMBC2 && (address < 0xA200))
 		{
        WORD newAddress = address - 0xA000;
        m_RamBank.at(m_CurrentRamBank)[newAddress] = data;
 		}


    // Internal WRAM
    else if ((address >= 0xC000) && (address <= 0xDFFF))
    {
      m_Rom[address] = data;
    }

    // echo memory. Writes here and into the internal RAM as above
    else if ((address >= 0xE000) && (address <= 0xFDFF))
    {
      m_Rom[address] = data;
      m_Rom[address - 0x2000] = data;   // Per docs this memory section ECHOs to RAM
    }

    // This area is restricted for LCD and other memory registers
    else if ((address >= 0xFEA0) && (address <= 0xFEFF))
    {
      // Nothing to do here
    }

    // reset the divider register as GB doesnt allow to write directly to its memory
    else if (address == 0xFF04)
    {
      m_Rom[0xFF04] = 0;
      m_DividerVariable = 0;
    }

    else if (address == TMC)
    {
      m_Rom[data] = data;

      int timerVal = data & 0x03; // Register is 3 bits only

      int clockSpeed = 0;

      switch (timerVal)
      {
        case 0x00: clockSpeed = 1024; break;
        case 0x01: clockSpeed = 16; break;
        case 0x02: clockSpeed = 64; break;
        case 0x03: clockSpeed = 256; break;
        default: assert(false); break; // weird timer val
      }

      // Set the new clock speed
      if (clockSpeed != m_CurrentClockSpeed)
      {
        m_TimerVariable = 0;
        m_CurrentClockSpeed = clockSpeed;
      }
    }

    // FF44 shows which horizontal scanline is currently being draw. Writing here resets it
    else if (address == 0xFF44)
    {
      m_Rom[0xFF44] = 0;
    }

    else if (address == 0xFF45)
    {
      m_Rom[address] = data;
    }
    
    // DMA transfer. Requested by game
    else if (address == 0xFF46)
    {
      WORD newAddress = (data << 8);  // Mult by 100
      for (int i = 0; i < 0xA0; i++)
      {
        m_Rom[0xFE00 + i] = ReadMemory(newAddress + i);
      }
    }
    // No control needed over this area so write to memory
    else
    {
      m_Rom[address] = data;
    }
  }
}

//////////////////////////////////////////////////////////////////
// all reading of rom should go through here so I can trap it.

BYTE Emulator::ReadMemory(WORD address) const
{
  // Reading from ROM bank using switching technique based of m_CurrentRomBank
  if (address >= 0x4000 && address <= 0x7FFF)
  {
    unsigned int newAddress = address;
    newAddress += ((m_CurrentRomBank - 1)*0x4000);
    return m_GameBank[newAddress];
  }
  // Reading from RAM bank using switching techinique as well
  else if (address >= 0xA000 && address <= 0xBFFF)
  {
    WORD newAddress = address - 0xA000;
    return m_RamBank.at(m_CurrentRamBank)[newAddress];
  }
  // Trying to read joypad state
  else if (address == 0xFF00)
    return GetJoypadState();

  // return memory
  return m_Rom[address];
}

//////////////////////////////////////////////////////////////////

void Emulator::KeyPressed(int key)
{
  // this function CANNOT call ReadMemory(0xFF00) it must access it directly from m_Rom[0xFF00]
	// because ReadMemory traps this address

  bool previouslyUnset = false;

  // if setting from 1 to 0 we may have to request an interupt
  if (TestBit(m_JoypadState, key) == false)
    previouslyUnset = true;

  // remember if a keypressed its bit is 0 not 1
  m_JoypadState = BitReset(m_JoypadState, key);

  // Button pressed
  bool button = true;

  // Button key requested
  if (key > 3)
    button = true;
  // Directional button requested
  else
    button = false;

  BYTE keyReq = m_Rom[0xFF00];
  bool requestInterupt = false;

	// player pressed button and programmer interested in button
	if (button && !TestBit(keyReq,5))
		requestInterupt = true;
	// player pressed directional and programmer interested in directional
	else if (!button && !TestBit(keyReq,4))
		requestInterupt = true;

	if (requestInterupt && !previouslyUnset)
		RequestInterupt(4);
}

//////////////////////////////////////////////////////////////////

void Emulator::KeyReleased(int key)
{
  m_JoypadState = BitSet(m_JoypadState, key);
}

//////////////////////////////////////////////////////////////////

BYTE Emulator::GetJoypadState() const
{
  // this function CANNOT call ReadMemory(0xFF00) it must access it directly from m_Rom[0xFF00]
	// because ReadMemory traps this address
	BYTE res = m_Rom[0xFF00];
	res ^= 0xFF;

  // Action buttons
	if (!TestBit(res, 4))
	{
		BYTE topJoypad = m_JoypadState >> 4;
		topJoypad |= 0xF0; // turn the top 4 bits on
		res &= topJoypad;  // show what buttons are pressed
	}
  //directional buttons
	else if (!TestBit(res,5))
	{
		BYTE bottomJoypad = m_JoypadState & 0xF;
		bottomJoypad |= 0xF0;
		res &= bottomJoypad;
	}
	return res;

}

//////////////////////////////////////////////////////////////////
void Emulator::DrawScanLine()
{
  BYTE lcdControl = ReadMemory(0xFF40);

  // We can only draw if the LCD is enabled
  if (TestBit(lcdControl, 7))
  {
    /* TODO: Call functions based on each component being enabled or nor */
    RenderBackground(lcdControl);
    RenderSprites(lcdControl);
  }
}

//////////////////////////////////////////////////////////////////

static int vblankcount = 0;

void Emulator::IssueVerticalBlank( )
{
	vblankcount++;
	RequestInterupt(0);
	if (hack == 60)
	{
		//OutputDebugStr(STR::Format("Total VBlanks was: %d\n", vblankcount));
		vblankcount = 0;
	}

}

//////////////////////////////////////////////////////////////////

static int counter = 0;
static int count2 = 0;

void Emulator::DrawCurrentLine( )
{
	if (TestBit(ReadMemory(0xFF40), 7)== false)
		return;

	m_Rom[0xFF44]++;
	m_RetraceLY = RETRACE_START;

	BYTE scanLine = ReadMemory(0xFF44);

	if ( scanLine == VERTICAL_BLANK_SCAN_LINE)
		IssueVerticalBlank( );

	if (scanLine > VERTICAL_BLANK_SCAN_LINE_MAX)
		m_Rom[0xFF44] = 0;

	if (scanLine < VERTICAL_BLANK_SCAN_LINE)
	{
		DrawScanLine( );
	}

}

//////////////////////////////////////////////////////////////////

void Emulator::RenderBackground(BYTE lcdControl)
{
// lets draw the background (however it does need to be enabled)
	if (TestBit(lcdControl, 0))
	{
		WORD tileData = 0;
		WORD backgroundMemory =0;
		bool unsig = true;

    // where to draw the visual area and the window
		BYTE scrollY = ReadMemory(0xFF42);
		BYTE scrollX = ReadMemory(0xFF43);
		BYTE windowY = ReadMemory(0xFF4A);
		BYTE windowX = ReadMemory(0xFF4B) - 7;

		bool usingWindow = false;

    // is the window enabled?
		if (TestBit(lcdControl,5))
		{
      // is the current scanline we're drawing
      // within the windows Y pos?,
			if (windowY <= ReadMemory(0xFF44))
				usingWindow = true;
		}
		else
		{
			usingWindow = false;
		}

		// which tile data are we using?
		if (TestBit(lcdControl,4))
		{
			tileData = 0x8000;
		}
		else
		{
      // IMPORTANT: This memory region uses signed
      // bytes as tile identifiers
			tileData = 0x8800;
			unsig= false;
		}

		// which background mem?
		if (usingWindow == false)
		{
			if (TestBit(lcdControl,3))
				backgroundMemory = 0x9C00;
			else
				backgroundMemory = 0x9800;
		}
		else
		{
			if (TestBit(lcdControl,6))
				backgroundMemory = 0x9C00;
			else
				backgroundMemory = 0x9800;
		}

		BYTE yPos = 0;

    // yPos is used to calculate which of 32 vertical tiles the
    // current scanline is drawing
		if (!usingWindow)
			yPos = scrollY + ReadMemory(0xFF44);
		else
			yPos = ReadMemory(0xFF44) - windowY;

    // which of the 8 vertical pixels of the current
    // tile is the scanline on?
		WORD tileRow = (((BYTE)(yPos/8))*32); // 32 is the max tile amount

    // time to start drawing the 160 horizontal pixels
    // for this scanline
		for (int pixel = 0; pixel < 160; pixel++)
		{
			BYTE xPos = pixel+scrollX;

      // translate the current x pos to window space if necessary
			if (usingWindow)
			{
				if (pixel >= windowX)
				{
					xPos = pixel - windowX;
				}
			}

      // which of the 32 horizontal tiles does this xPos fall within?
			WORD tileCol = (xPos/8);
			SIGNED_WORD tileNum;

      // get the tile identity number. Remember it can be signed
      // or unsigned
			if(unsig)
				tileNum = (BYTE)ReadMemory(backgroundMemory+tileRow + tileCol);
			else
				tileNum = (SIGNED_BYTE)ReadMemory(backgroundMemory+tileRow + tileCol);

      // deduce where this tile identifier is in memory
			WORD tileLocation = tileData;

			if (unsig)
				tileLocation += (tileNum * 16);
			else
				tileLocation += ((tileNum+128) *16);

      // find the correct vertical line we're on of the
      // tile to get the tile data
      // from in memory
			BYTE line = yPos % 8;
			line *= 2;
			BYTE data1 = ReadMemory(tileLocation + line);  // each vertical line takes up two bytes of memory
			BYTE data2 = ReadMemory(tileLocation + line + 1);

      // pixel 0 in the tile is it 7 of data 1 and data2.
      // Pixel 1 is bit 6 etc..
			int colourBit = xPos % 8;
			colourBit -= 7;
			colourBit *= -1;

      // combine data 2 and data 1 to get the colour id for this pixel
      // in the tile
			int colourNum = BitGetVal(data2,colourBit);
			colourNum <<= 1;
			colourNum |= BitGetVal(data1,colourBit);

      // now we have the colour id get the actual
      // colour from palette 0xFF47
			COLOUR col = GetColour(colourNum, 0xFF47);
			int red = 0;
			int green = 0;
			int blue = 0;

			switch(col)
			{
			case WHITE:	red = 255; green = 255; blue = 255; break;
			case LIGHT_GRAY:red = 0xCC; green = 0xCC; blue = 0xCC; break;
			case DARK_GRAY:	red = 0x77; green = 0x77; blue = 0x77; break;
			}

			int finaly = ReadMemory(0xFF44);

      // safety check to make sure what im about
      // to set is int the 160x144 bounds
			if ((finaly < 0) || (finaly > 143) || (pixel < 0) || (pixel > 159))
			{
				assert(false);
				continue;
			}

			m_ScreenData[finaly][pixel][0] = red;
			m_ScreenData[finaly][pixel][1] = green;
			m_ScreenData[finaly][pixel][2] = blue;
		}
	}
}

//////////////////////////////////////////////////////////////////

void Emulator::RenderSprites(BYTE lcdControl)
{
	// lets draw the sprites (however it does need to be enabled)
	if (TestBit(lcdControl, 1))
	{
		bool use8x16 = false;
		if (TestBit(lcdControl,2))
			use8x16 = true;

		for (int sprite = 0; sprite < 40; sprite++)
		{
      // sprite occupies 4 bytes in the sprite attributes table
 			BYTE index = sprite*4;
 			BYTE yPos = ReadMemory(0xFE00+index) - 16;  // 16 is offset
 			BYTE xPos = ReadMemory(0xFE00+index+1)-8;   // 8 is offset
 			BYTE tileLocation = ReadMemory(0xFE00+index+2);
 			BYTE attributes = ReadMemory(0xFE00+index+3);

			bool yFlip = TestBit(attributes,6);
			bool xFlip = TestBit(attributes,5);

			int scanline = ReadMemory(0xFF44);

			int ysize = 8;

			if (use8x16)
				ysize = 16;

      // does this sprite intercept with the scanline?
 			if ((scanline >= yPos) && (scanline < (yPos+ysize)))
 			{
 				int line = scanline - yPos;

        // read the sprite in backwards in the y axis
 				if (yFlip)
 				{
 					line -= ysize;
 					line *= -1;
 				}

 				line *= 2; // same as for tiles
 				BYTE data1 = ReadMemory( (0x8000 + (tileLocation * 16)) + line ); 
 				BYTE data2 = ReadMemory( (0x8000 + (tileLocation * 16)) + line+1 );

        // its easier to read in from right to left as pixel 0 is
        // bit 7 in the colour data, pixel 1 is bit 6 etc...
 				for (int tilePixel = 7; tilePixel >= 0; tilePixel--)
 				{
					int colourbit = tilePixel;
          // read the sprite in backwards for the x axis
 					if (xFlip)
 					{
 						colourbit -= 7;
 						colourbit *= -1;
 					}

          // the rest is the same as for tiles
 					int colourNum = BitGetVal(data2,colourbit);
 					colourNum <<= 1;
 					colourNum |= BitGetVal(data1,colourbit);

					COLOUR col = GetColour(colourNum, TestBit(attributes,4)?0xFF49:0xFF48);

 					// white is transparent for sprites.
 					if (col == WHITE)
 						continue;

 					int red = 0;
 					int green = 0;
 					int blue = 0;

					switch(col)
					{
					case WHITE:	red = 255; green = 255; blue = 255; break;
					case LIGHT_GRAY:red = 0xCC; green = 0xCC; blue = 0xCC; break;
					case DARK_GRAY:	red = 0x77; green = 0x77; blue = 0x77; break;
					}

 					int xPix = 0 - tilePixel;
 					xPix += 7;

					int pixel = xPos+xPix;

					if ((scanline < 0) || (scanline > 143) || (pixel < 0) || (pixel > 159))
					{
						continue;
					}

					// check if pixel is hidden behind background
					if (TestBit(attributes, 7) == 1)
					{
						if ( (m_ScreenData[scanline][pixel][0] != 255) || (m_ScreenData[scanline][pixel][1] != 255) || (m_ScreenData[scanline][pixel][2] != 255) )
							continue;
					}

 					m_ScreenData[scanline][pixel][0] = red;
 					m_ScreenData[scanline][pixel][1] = green;
 					m_ScreenData[scanline][pixel][2] = blue;
 				}
 			}
		}
	}
}

//////////////////////////////////////////////////////////////////

Emulator::COLOUR Emulator::GetColour(BYTE colourNum, WORD address) const{
  COLOUR res = WHITE;
  BYTE palette = ReadMemory(address);
  int hi = 0;
  int lo = 0;

  // which bits of the colour palette does the colour id map to?
  switch (colourNum)
  {
    case 0: hi = 1; lo = 0; break;
    case 1: hi = 3; lo = 2; break;
    case 2: hi = 5; lo = 4; break;
    case 3: hi = 7; lo = 6; break;
    default: assert(false); break;
  }

	int colour = 0;
	colour = BitGetVal(palette, hi) << 1;
	colour |= BitGetVal(palette, lo);

	switch (colour)
	{
    case 0: res = WHITE; break;
    case 1: res = LIGHT_GRAY; break;
    case 2: res = DARK_GRAY; break;
    case 3: res = BLACK; break;
    default: assert(false); break;
	}

	return res;
}

//////////////////////////////////////////////////////////////////

void Emulator::CreateRamBanks(int numBanks)
{
	// DOES THE FIRST RAM BANK NEED TO BE SET TO THE CONTENTS of m_Rom[0xA000] - m_Rom[0xC000]?
	for (int i = 0; i < 17; i++)
	{
		BYTE* ram = new BYTE[0x2000]; // Each RAM bank is 0x2000 bytes
		memset(ram, 0, sizeof(ram));
		m_RamBank.push_back(ram);
	}

	for (int i = 0; i < 0x2000; i++)
		m_RamBank[0][i] = m_Rom[0xA000+i];
}

//////////////////////////////////////////////////////////////////
void Emulator::RequestInterupt(int bit)
{
  BYTE requestFlag = ReadMemory(0xFF0F);
  requestFlag = BitSet(requestFlag, bit);
  WriteByte(0xFF0F, requestFlag);
}

//////////////////////////////////////////////////////////////////

WORD Emulator::ReadWord( ) const
{
	WORD res = ReadMemory(m_ProgramCounter+1) ;
	res = res << 8 ;
	res |= ReadMemory(m_ProgramCounter) ;
	return res ;
}