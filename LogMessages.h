#pragma once
#ifndef _LOGMESSAGE_H
#define _LOGMESSAGE_H

struct _iobuf;

class LogMessage 
{
  public:
    ~LogMessage();

    static LogMessage* CreateInstance();
    static LogMessage* GetSingleton();
    void DoLogMessage(const char* message, bool logToConsole);  // Can I change it to a string?

    private:
      LogMessage(); // Made private to allow abstracted singleton

      static LogMessage* m_Instance;
      _iobuf* m_LogFile;

};

#endif