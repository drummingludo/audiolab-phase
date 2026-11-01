# 13 · Plugin « Audio Lab Phase » (interface et moteur)

Plugin VST3 / AU / AAX (non signé) d'inversion de polarité et de décalage temporel, demandé par Ludovic le 01/11/2026 : décalage sur un curseur, unité « Échantillons » ou « ms » au choix, avance et retard (±10 ms), gauche et droite séparés avec un bouton Link. Construit comme Audio Lab Compresseur, Basses et Reverb. **Interface à la charte UI V2 des plugins** (fiche 02, 01/11/2026).

| | Audio Lab Phase |
|---|---|
| Dépôt GitHub | drummingludo/audiolab-phase |
| Code plugin (AU) | `aufx AlPh Trsv` |
| Identifiant | fr.transversalstudio.audiolab.phase |
| Catégories | VST3 Fx / Tools, AAX Delay |
| Taille de base | 580 × 396 (redimensionnable de 60 à 200 %, mémorisée) |
| Formats d'entrée / sortie | mono → mono, stéréo → stéréo |
| Version | 1.0.0 |

## 1. Réglages (paramètres automatisables)

| Réglage | Plage | Défaut | Remarque |
|---|---|---|---|
| Décalage G, Décalage D | −10 à +10 ms | 0 | négatif = le son arrive plus tôt (avance), positif = plus tard (retard) |
| Polarité G, Polarité D | Normale / Inversée | Normale | paramètres « choix » (et non « bool ») : rappel de session exact, exigé par pluginval |
| Link G/D | Off / On | On | D suit G (décalage et polarité) ; le plugin recopie aussi G dans les paramètres de D, donc rien ne saute quand on coupe Link |
| Unité | Échantillons / ms | Échantillons | change le pas du curseur, l'affichage et la précision (voir § 2) |
| Bypass | | | fondu de 20 ms vers l'entrée retardée de la latence (reste alignée) |

Les décalages sont enregistrés en ms. Programmes proposés au logiciel hôte : Par défaut, Polarité inversée, Test mono : D inversée, Retard 1 ms (34 cm), Avance 1 ms (34 cm), Élargissement Haas (D +8 ms). Les presets ne changent pas l'unité.

## 2. Moteur (`Source/PhaseCore.h`, sans JUCE)

- **Latence annoncée fixe** L = ⌈10 ms × fs⌉ + 32 échantillons (473 à 44,1 kHz, 512 à 48 kHz, 992 à 96 kHz), indépendante des réglages (obligatoire en AAX, et aucun « saut » de compensation dans les autres formats). Retard interne = L + d : avec la compensation de l'hôte, un décalage d < 0 avance réellement le son.
- **Unité Échantillons** : d arrondi à l'échantillon entier ; la sortie est une copie exacte de l'entrée (au bit près), décalée, et retournée si la polarité est inversée.
- **Unité ms** : grille de 1 µs ; quand d tombe entre deux échantillons, lecture par un filtre sinc fenêtré (Kaiser β = 8, 64 points, somme normalisée à 1). Mesuré : gain plat à ±0,0006 dB de 20 Hz à 20 kHz (44,1 / 48 / 96 kHz), erreur de retard ≤ 0,003 µs. Si d tombe juste sur un échantillon (1,000 ms à 48 kHz), lecture exacte sans filtre.
- **Sans clic** : changement de décalage par fondu enchaîné linéaire de 20 ms entre l'ancienne et la nouvelle position (pas d'effet Doppler) ; polarité qui passe de +1 à −1 en 20 ms ; bypass en 20 ms.
- Aucune allocation pendant le traitement ; calcul en double, mémoire en float.

## 3. Interface (grille de 24 px)

- **Écran** 532 × 176 (x 24, y 24), fond « Surface », deux vues (sélecteur « Temps | Somme mono » sous l'écran, à gauche) :
  - **Temps** : axe de −10 à +10 ms (graduations tous les 1 ms et chiffres tous les 5 ms ; en Échantillons, graduations rondes, par exemple ±200, ±400 et la limite ±480 éch. à 48 kHz, sans chiffre trop près du bord) ; son d'origine en pointillés « Repères secondaires » au centre ; G et D en impulsions orange #EAA93D (vers le haut = polarité normale, vers le bas = inversée, « Ø » à côté du nom) ; en-tête « Avance » à gauche, « Retard » à droite, écart D − G en ms et en cm au centre. En bypass : une seule impulsion grise « Original ».
  - **Somme mono** : réponse de (G + D) / 2, 20 Hz → 20 kHz (log), 0 → −30 dB, courbe teal avec remplissage 25 % → 2 %, dessinée exactement colonne par colonne (creux et sommets calculés : un peigne plus serré que l'écran apparaît comme une bande) ; texte : « Premier creux : 1,00 kHz, puis tous les 2,00 kHz », « Graves annulés… » (polarités opposées), « Annulation totale » ou « Aucun creux ».
- **Unité** : sélecteur « Échantillons | ms » sous l'écran, à droite.
- **Lignes G (y 276) et D (y 324)**, noms des commandes au-dessus (POLARITÉ, LINK, DÉCALAGE) :
  - bouton **Ø** 36 × 36 (« Capuchon » ; inversé = #005F73) ;
  - **Link** : pilule verticale entre les deux lignes, icône de chaîne inclinée à 45° : maillons emboîtés sur fond #005F73 quand les canaux sont liés, maillons séparés (contour « Rails ») sinon ;
  - **curseur bipolaire** 306 px : départ au centre (0), partie colorée du centre au bouton en dégradé orange #BB3E03 → #EAA93D (temps = orange), graduations à 0, ±5 et ±10 ms, repères « −10 ms / 0 / +10 ms » sous la ligne D ;
  - **case de valeur** 116 px : valeur dans l'unité choisie (14 px, chiffres en cellules fixes), puis l'autre unité et la distance parcourue par le son (343 m/s), en gris doux ; un clic ouvre la saisie au clavier (« 48 », « 1,5 ms », « 72 éch. », « 250 µs », « 34 cm »).
- Manipulation du curseur : clic sur la piste = le bouton y saute, glisser (Maj = 10 fois plus fin), molette et flèches pas à pas (1 éch. ; en ms 0,01 ms, Maj 1 µs), double-clic ou Origine = 0.
- Piste mono : ligne D et Link grisés, sélecteur de vue masqué (vue Temps seulement).
- Rien ne s'anime : l'interface se redessine seulement quand un réglage change.

## 4. Vérifications faites

- `tests/core_test.cpp` (moteur seul) : copie exacte décalée à 44,1 / 48 / 96 / 192 kHz, d de −10 à +10 ms, polarités, blocs de 1 à 4096 ; avance réelle après compensation (impulsion à 1000 → 952 pour −1 ms) ; unité ms 20 Hz → 20 kHz ; fondus sans saut ; bypass exact ; indépendance de la taille des blocs ; mono.
- `tests/processor_test.cpp` (plugin) : sauvegarde / rappel, presets, latence fixe, G −2 ms et D +3 ms inversée au bit près, Link (moteur et recopie des paramètres, aucun saut en coupant Link), bypass aligné, mono, plugin = moteur (blocs irréguliers, réglages changés en lecture), saisie au clavier.
- `tests/render_ui.cpp` : captures des vues, unités, Link, bypass, saisie, mono, 60 % et 200 %.
- pluginval niveau 10 (Linux VST3, avec tests d'interface) ; sur GitHub : auval + pluginval niveau 10 (Mac VST3 + AU), pluginval (Windows VST3), contrôle de structure AAX.
- Non testé ici : écoute réelle dans un logiciel, compensation de latence dans chaque logiciel, Windows sur un vrai PC, AAX dans Pro Tools Developer.
