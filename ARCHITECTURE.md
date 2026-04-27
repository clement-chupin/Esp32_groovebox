# Clavier V2 - Architecture Modulaire

## 📋 Vue d'ensemble

Le code a été réorganisé en modules fonctionnels pour améliorer la maintenabilité et la clarté. Chaque module gère un aspect spécifique du système.

## 🗂️ Structure des modules

### **config_module.h**
**Rôle :** Configuration centralisée et constantes globales
- Définitions des pins (I2C, I2S, ADC, LEDs)
- Constantes audio (sample rate, polyphonie, drums)
- Structures de données (EnvPreset, DrumBank, ArpPreset)
- Configurations des gammes musicales
- Paramètres de timing et UI

**À utiliser pour :** Modifier les configurations matérielles et les presets

---

### **synth_module.h/.cpp**
**Rôle :** Moteur de synthèse polyphonique
- Gestion des 8 voix de synthèse
- Oscillateurs (sinus, saw, square, triangle, etc.)
- Enveloppes ADSR
- Arpégiateur avec presets configurables
- Conversion notes → fréquences
- Support de différentes gammes (chromatique, pentatonique, modes)

**Fonctions principales :**
- `noteOn()` / `noteOff()` : Déclenchement des notes
- `updateSynthControl()` : Mise à jour périodique (enveloppes, arp, slides)
- `keyToFreqColumnOrder()` : Mapping boutons → notes
- `applyEnvPreset()` : Application des presets d'enveloppe

---

### **drum_module.h/.cpp**
**Rôle :** Synthèse et séquenceur de drums
- 4 pistes de drums synthétisées (kick, snare, hihat, clap)
- Générateur de bruit LFSR pour les cymbales
- Séquenceur 4×8 steps avec patterns
- 6 banques de sons (Classic, Electro, Noisy, Deep808, Tape, Metal)
- Sweep de fréquence pour les kicks/snares

**Fonctions principales :**
- `triggerDrum()` : Déclencher un drum
- `triggerDrumVoice()` : Variante avec contrôle de hauteur
- `runDrumSequencer()` : Avance le séquenceur
- `updateDrumsControl()` : Sweep fréquences + enveloppes

---

### **effects_module.h/.cpp**
**Rôle :** Chaîne d'effets audio
- 14 effets disponibles :
  - Filtres : LPF, HPF
  - Delays : DelayBPM, Delay2
  - Résonances : ResEcho S/R/X avec feedback
  - Modulations : Acid, AcidDrv, Flanger
  - Distorsions : Fuzz, BitCrush
  - Autres : Noise
- Buffer de delay 16384 samples
- Gestion multi-effet (layering possible)

**Fonctions principales :**
- `isEffectActive()` : Vérifier si un effet est actif
- `toggleEffectSlot()` : Activer/désactiver un effet
- `compress16()` : Compression dynamique

---

### **controls_module.h/.cpp**
**Rôle :** Lecture et traitement des potentiomètres
- 5 potentiomètres ADC avec filtrage adaptatif
- Mapping vers paramètres audio (volume, BPM, enveloppe, etc.)
- Quantification intelligente de l'octave avec hystérésis
- Mode debug optionnel (#define ENABLE_POT_DEBUG)

**Potentiomètres :**
1. Volume maître (0-160000)
2. Param FX / Bank drum (contextuel selon mode)
3. Preset enveloppe (8 presets)
4. BPM (80-120)
5. Octave (-4 à +2)

---

### **modes_module.h/.cpp**
**Rôle :** Gestion des modes d'interaction
- 5 modes principaux :
  1. **INSTRUMENT** : Clavier polyphonique + arpégiateur
  2. **DRUMBOX** : Séquenceur de patterns drums
  3. **DRUM_INSTRUMENT** : Split mode (pattern + jeu live)
  4. **FX** : Contrôle des effets + notes
  5. **SCALE_HELP** : Aide aux gammes musicales

**Fonctionnalités :**
- Long-press (500ms) pour reset shape/arp/fx
- Tap tempo sur le bouton CLEAR en mode DRUMBOX
- Combo boutons pour accès rapide aux fonctions
- Handlers spécifiques pour chaque mode

---

### **input_module.h** _(existant, à réorganiser)_
**Rôle :** Scan des boutons et gestion des événements
- Lecture des MCP23X17 (3× expanders I2C)
- Matrice 4×8 + 4 boutons extras
- Détection press/release avec debouncing
- Task FreeRTOS dédiée au scan

---

### **display_module.h** _(existant, à réorganiser)_
**Rôle :** Affichage OLED et LEDs
- Écran SH1107 128×128
- Bande NeoPixel (36 boutons × 3 LEDs)
- Effets visuels dynamiques par mode
- Light show synchronisé au BPM

---

### **audio_module.h** _(existant, à réorganiser)_
**Rôle :** Génération audio finale
- Fonction `updateAudio()` appelée à 16384 Hz
- Mixage synth + drums
- Application de la chaîne d'effets
- Sortie I2S vers DAC externe

---

## 🔄 Flux de données

```
Hardware Inputs
    ↓
[controls_module] ← Lecture ADC
[input_module] ← Scan boutons
    ↓
[modes_module] ← Traitement événements
    ↓
[synth_module] ← noteOn/noteOff
[drum_module] ← triggerDrum
    ↓
[audio_module] ← updateAudio()
    ↓
[effects_module] ← Traitement FX
    ↓
I2S Output → DAC
```

## 🎛️ Avantages de cette architecture

1. **Séparation des responsabilités** : Chaque module a un rôle clair
2. **Réutilisabilité** : Les modules peuvent être testés indépendamment
3. **Maintenabilité** : Plus facile de localiser et corriger les bugs
4. **Scalabilité** : Ajout de nouveaux modes ou effets simplifié
5. **Documentation** : Code auto-documenté par la structure

## 🚀 Prochaines étapes

- [ ] Finaliser la réorganisation de `input_module.h`
- [ ] Simplifier `audio_module.h` (garder uniquement updateAudio)
- [ ] Nettoyer `display_module.h` (séparation OLED/LEDs)
- [ ] Mettre à jour `clavier_v2.ino` avec les nouveaux includes
- [ ] Tester la compilation et le fonctionnement

## 📝 Notes de migration

**Includes nécessaires dans clavier_v2.ino :**
```cpp
#include "config_module.h"
#include "synth_module.h"
#include "drum_module.h"
#include "effects_module.h"
#include "controls_module.h"
#include "modes_module.h"
#include "input_module.h"
#include "display_module.h"
#include "audio_module.h"
```

**Ordre d'initialisation dans setup() :**
1. `initSynth()`
2. `initDrums()`
3. `initEffects()`
4. Puis les autres initialisations hardware

**Ordre des updates dans updateControl() :**
1. `updateSynthControl()`
2. `updateDrumsControl()`
3. `runDrumSequencer()`
