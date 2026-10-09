//-----------------------------------------------------------------------
// timer.hpp      GAME-MAKER  V2.0                By Andy Stone         |
// Created: June 1,1992                                                 |
//                                                                      |
// This file steals the timer, and allows the user to set time based    |
// function calls.                                                      |
//-----------------------------------------------------------------------

#ifndef TIMERCLASS
#define TIMERCLASS
#include "gen.h"
#include "genclass.hpp"
extern char CallEm(NodeClass *CursorNode);

#define TIMERSTKLEN     2000
extern unsigned int far MyDS; 
extern gm_ulong far Clock;
extern gm_ulong far OldTimer;

class TimerClass:public LinkedListClass
  {
  public:
  char In;
  gm_ulong OldClock;
  friend void interrupt NewTimer(...);
  void AddFn(void Function(void *ptrtodata),void *data); //,int freq=1); //add in freq functality later
  void RemFn(void Function(void *ptrtodata),void *data,int freq=1);
  void TurnOff(void);
  void TurnOn(void);
  };
#endif
