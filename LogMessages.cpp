#include "LogMessages.h"
#include <assert.h>
#include <iostream>
#include <ctime>
#include <chrono>
#include <string>

#ifdef WIN32
  #include <windows.h>
#endif

LogMessage* LogMessage::m_Instance = 0 ; // Do I need this?

LogMessage::LogMessage()
{
  m_LogFile = NULL;

  m_LogFile = (_iobuf*)fopen("emulator.log", "w");
}

LogMessage::~LogMessage()
{
  if (m_LogFile != NULL)
    fclose((FILE*)m_LogFile);
}

LogMessage* LogMessage::CreateInstance()
{
  if (m_Instance == 0)
    m_Instance = new LogMessage();
  
  return m_Instance;
}

LogMessage* LogMessage::GetSingleton()
{
  assert(m_Instance != 0);
  return m_Instance;
}

void LogMessage::DoLogMessage(const char* message, bool logToConsole)
{
  // Get current time
  auto currentTime = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

  // Convert current time to string
  std::string timeString = std::ctime(&currentTime);
  size_t nullPos = timeString.find('\n');

  if (nullPos != std::string::npos) {
      timeString.erase(nullPos, 1);
  }

  // Print buffer
  char msgBuffer[255];
  std::snprintf(msgBuffer, sizeof(msgBuffer), "[%s]: %s", timeString.c_str(), message);

  // Log to file
  if (m_LogFile != NULL)
    fputs(msgBuffer, (FILE*)m_LogFile);

  if (logToConsole == true)
    std::cout << msgBuffer;
}
