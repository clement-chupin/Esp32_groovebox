#pragma once
// ============================================================
// crunchos_module.h
// Intégration du moteur CrunchOS (Tracker + Voices) dans clavier_v2.
// Le Tracker joue les drums en parallèle du synth Mozzi.
// ============================================================

#include "Tracker.h"
#include "InputManager.h"
#include "ScreenManager.h"

// Objets globaux instanciés dans clavier_v2.ino
extern Tracker       crunchTracker;
extern InputManager  crunchInputMgr;
extern ScreenManager crunchScreenMgr;
extern bool          crunchTransportRunning;

// Données partagées pour le rendu OLED CrunchOS
extern char     crunchLedCmd;
extern int      crunchVolumeBars[4];
extern String   crunchNoteChars[12];

// Appeler depuis updateAudio() : renvoie le sample CrunchOS (int16_t)
inline int16_t crunchAudioSample() {
    crunchTracker.UpdateTracker();
    int32_t s = crunchTracker.sample;
    if (s > 32767)  s = 32767;
    if (s < -32768) s = -32768;
    return (int16_t)s;
}

// Appeler depuis modes_module.cpp : route un justPressed vers CrunchOS InputManager
// Parcourt le tableau justPressed[] et envoie le premier key pressé vers InputManager.
void crunchHandleInput();

// Appeler depuis display_module.cpp pour les modes drum
void crunchRenderDisplay();
void crunchRenderLeds();
