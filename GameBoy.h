#pragma once
#ifndef _GAMEBOY_H
#define _GAMEBOY_H

#include "Emulator.h"
#include "SDL.h"

class GameBoy
{
  public:
    ~GameBoy();

    static GameBoy* CreateInstance();
    static GameBoy* GetSingleton();

    void RenderGame();
    void Initialize();
    void SetKeyPressed(int key);
    void SetKeyReleased(int key);
    void StartEmulation();
    void HandleInput(SDL_Event& event);

  private:
    GameBoy();

    bool CreateSDLWindow();
    void InitGL();

    static GameBoy* m_Instance;
    Emulator* m_Emulator;
};

#endif