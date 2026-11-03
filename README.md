# Audio Lab Phase

L'outil de polarité et de décalage temporel d'**Audio Lab** (« Chimiste du son », Transversal Studio), en plugin **VST3 / AU** pour Logic Pro, Ableton Live, FL Studio, Cubase, Reaper, Studio One, Bitwig…, et **AAX** pour Pro Tools (version de test non signée, voir plus bas).

Deux micros sur une même source (caisse claire dessus et dessous, kick in et out, ampli de guitare et DI, overheads et proches) ne captent pas le son au même instant ni avec la même polarité : additionnés, ils se creusent mutuellement (filtre en peigne, grave qui disparaît). Le plugin retourne la polarité et décale chaque canal pour les remettre en phase.

- **Polarité** (Ø) par canal, souvent appelée « inversion de phase ».
- **Décalage de −10 à +10 ms** par canal (≈ 3,4 m de distance entre micros) : à droite le son arrive plus tard, à gauche **plus tôt**. Pour pouvoir avancer un son et tourner sa phase, le plugin annonce une latence fixe (≈ 25 ms depuis la 2.0.0 : 10 ms + 32 échantillons + 14,25 ms) que le logiciel compense automatiquement. Mode **Zero** (réglages avancés) : latence nulle, retards seulement, à l'échantillon près, sans rotation.
- **Rotation de phase 0 à 360°** par canal (2.0.0) : toutes les fréquences tournent du même angle (transformée de Hilbert à phase linéaire), exacte à ±0,6° de 40 Hz à 18 kHz.
- **Bouton Align** (2.0.0) : envoyez l'autre micro dans l'entrée side-chain, lancez la lecture, cliquez ; Lab Phase mesure le décalage, la polarité et la rotation (même mesure que Lab Corr) et les règle sur la piste.
- **Unité au choix** : **Échantillons** (pas d'un échantillon, le son est recopié tel quel, au bit près) ou **ms** (pas de 0,01 ms, 1 µs avec Maj ; entre deux échantillons, lecture par interpolation sinc fenêtrée de 64 points, plate à ±0,001 dB jusqu'à 20 kHz).
- **Gauche et droite séparés, avec Link** : un seul réglage pour les deux canaux quand Link est actif.
- **Pour apprendre** : écran **Temps** (où sortent G et D par rapport au son d'origine, polarité vers le haut ou vers le bas, écart en ms et en cm) et écran **Somme mono** (filtre en peigne quand G et D sont additionnés, avec la fréquence du premier creux). Saisie au clavier en ms, en échantillons, en µs ou en distance (« 34 cm »).
- **Sans clic** : changement de décalage par fondu enchaîné de 20 ms, polarité qui bascule en 20 ms, bypass aligné.
- **Interface Audio Lab** (charte UI V2) : thème sombre, police Poppins, fenêtre redimensionnable, info-bulles en anglais simple. Presets d'usage proposés au logiciel hôte.

Plan détaillé de l'interface et du moteur : `docs/13_GUI_Plugin_Phase.md`.

## Télécharger

La page **Releases** contient deux archives :
- `AudioLabPhase-Mac.zip` : VST3 + AU, Apple Silicon et Intel, avec un installateur en un double-clic.
- `AudioLabPhase-Windows.zip` : VST3 64 bits.

Mode d'emploi : fichier `LISEZ-MOI.txt` dans chaque archive.

### Pro Tools (AAX)

Chaque archive contient aussi `Audio Lab Phase.aaxplugin` (Mac universel, Windows 64 bits), fabriqué avec le SDK AAX fourni par JUCE (GPL v3). Il n'est **pas encore signé** par PACE : il ne se charge que dans **Pro Tools Developer**. Latence fixe annoncée pour chaque mode (Standard ≈ 25 ms, Zero 0) ; changer de mode change la latence (à faire à l'arrêt ; non testé dans Pro Tools).

## Fabrication

Chaque envoi sur `main` déclenche la fabrication sur des machines Mac et Windows de GitHub, puis la validation automatique (`auval` pour l'AU, `pluginval` niveau 10 pour VST3 et AU ; pour l'AAX, contrôle de structure, d'architectures et du point d'entrée). Un tag `v…` publie la version.

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Tests (Linux ou Mac) :

```
g++ -O2 -std=c++17 -ffp-contract=off -I Source tests/core_test.cpp -o tests/core_test && tests/core_test
cmake -B build -DALP_TESTS=ON
cmake --build build --target ALPProcessorTest ALPRenderUI
```

## Licence

Code sous **GNU AGPLv3** (voir `LICENSE`), comme la version libre du framework [JUCE](https://juce.com) qu'il utilise. Police Poppins sous licence SIL OFL 1.1 (`Resources/Poppins-OFL-LICENSE.txt`).

© Transversal Studio, Ludovic Diaz
