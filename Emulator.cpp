#include "Config.h"
#include "Emulator.h"

/* Local constants */
#define RETRACE_START 456

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
        RequestInterrupt(2);
      }
    }
  }

  if (m_DividerVariable >= 256)
  {
    m_DividerVariable = 0;
    m_Rom[0xFF04];
  }

}

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
			// char buffer[256];
			// sprintf(buffer, "Chaning Rom Bank to %d", m_CurrentRomBank);
			// LogMessage::GetSingleton()->DoLogMessage(buffer, false);
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
				// char buffer[256];
				// sprintf(buffer, "Chaning Rom Bank to %d", m_CurrentRomBank);
				// LogMessage::GetSingleton()->DoLogMessage(buffer, false);

			}
			else
			{
				m_CurrentRamBank = data & 0x3;

        // TODO: define log messages  
				// char buffer[256];
				// sprintf(buffer, "=====Chaning Ram Bank to %d=====", m_CurrentRamBank);
				// LogMessage::GetSingleton()->DoLogMessage(buffer, false);

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

  // No control needed over this area so write to memory
  else
  {
    m_Rom[address] = data;
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

  // return memory
  return m_Rom[address];
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