/* Replacement for SOUNDC.C (Sound Blaster VOC driver) and SBMUSIC.C (CMF/OPL2 music).
 *
 * The originals load Creative's driver blobs and program the card through port I/O, which cannot
 * run here. This file implements the same interface from sound.h natively. For now everything is
 * silent: SoundCard() reports "no card", so the engine never starts music, and VOC playback is a
 * no-op that succeeds. Planned: VOC -> raylib Sound, CMF -> OPL2 emulator.
 */
#include "gen.h"
#include "gmgen.h"
#include "jstick.h"
#include "sound.h"

int DetectCard(SoundCards *s, unsigned int *Port, unsigned int *Interrupt, char *drvrname)
  {
  *s = None;
  return 0;
  }

int SoundCard(ConfigStruct *cs) { return 0; }   // 0 = no FM card, music stays off
int InitMusic(int *Instru, char *cmffile) { return 1; }  // non-zero = failed to load
void PlayIt(void) {}
void sbfreemem(void) {}
void ResetFM(void) {}
void StopIt(void) {}

char InitSbVocDriver(ConfigStruct *cs) { return 0; }
char PlaySbVocFile(char far *filename) { return 0; }
void ShutSbVocDriver(void) {}
void StopSound(void) {}
void FreeSoundSample(void) {}
