#include "Config.h"
#include "GameBoy.h"
#include "LogMessages.h"

// extern "C" { FILE __iob_func[3] = { *stdin,*stdout,*stderr }; }

int WinMain(int argc, char *argv[])
{
	LogMessage* log = LogMessage::CreateInstance();
	GameBoy* gb = GameBoy::CreateInstance();

	gb->StartEmulation();

	delete gb;
	delete log;

	return 0;
}
