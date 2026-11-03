# 13 · Plugin « Audio Lab Phase » (interface et moteur)

Plugin VST3 / AU / AAX (non signé) d'inversion de polarité et de décalage temporel, demandé par Ludovic le 01/11/2026 : décalage sur un curseur, unité « Échantillons » ou « ms » au choix, avance et retard (±10 ms), gauche et droite séparés avec un bouton Link. Construit comme Audio Lab Compresseur, Basses et Reverb. **Interface à la charte UI V2 des plugins** (fiche 02, 01/11/2026).

| | Audio Lab Phase |
|---|---|
| Dépôt GitHub | drummingludo/audiolab-phase |
| Code plugin (AU) | `aufx AlPh Trsv` |
| Identifiant | fr.transversalstudio.audiolab.phase |
| Catégories | VST3 Fx / Tools, AAX Delay |
| Taille de base | 580 × 586 replié, 580 × 660 panneau avancé ouvert (580 × 396 en 1.x ; redimensionnable de 60 à 200 %, mémorisée) |
| Formats d'entrée / sortie | mono → mono, stéréo → stéréo ; entrée side-chain mono ou stéréo facultative (2.0.0, bouton Align) |
| Version | 2.0.0 |

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

## 5. Version 2.0.0 (audit qualité du 03/11/2026)

Ce qui change, en bref : chaque canal peut **tourner sa phase** de 0 à 360°, le bouton **Align** règle tout seul décalage, polarité et rotation d'après le side-chain, et un mode **Zero** supprime la latence (pour l'enregistrement). Tout ce qui existait reste identique au bit près (sauf la latence, plus longue en mode Standard, et compensée par le logiciel).

### 5.1 Rotation de phase (0 à 360°, par canal, avec Link)

- **Ce que c'est.** Toutes les fréquences sont avancées du même angle θ, sans retarder le son et sans changer le spectre : y = z·cos θ − H{z}·sin θ, H étant la transformée de Hilbert. 180° = polarité inversée ; 90° transforme un sinus en cosinus. Utile quand deux micros ne s'alignent pas avec un simple décalage (kick in / out, DI / ampli : le haut-parleur, le filtre de l'ampli ou la membrane tournent la phase).
- **Moteur.** Transformée de Hilbert à phase linéaire : RIF antisymétrique de 2M + 1 points (un sur deux est nul), fenêtre de Kaiser β = 3,5, M = ⌈14,25 ms × fs⌉ impair (629 points à 44,1 kHz, 685 à 48 kHz, 1369 à 96 kHz, 2737 à 192 kHz) : même réponse en Hz à toutes les fréquences d'échantillonnage. Calcul en float par deux produits scalaires contigus (signal rangé par parité), seulement quand la rotation n'est pas 0° ou 180° (là : copie exacte, ou opposée).
- **Mesures** (sinus, 44,1 / 48 / 96 / 192 kHz, θ = 30 à 333°) : **écart d'angle ≤ 0,57° de 40 Hz à 18 kHz** (cible de l'audit : ±2°), module ±0,17 dB. Sous 40 Hz la rotation s'atténue progressivement (gain de la transformée de Hilbert 0,98 à 40 Hz, 0,66 à 20 Hz ; à 90°, −1,2 dB à 30 Hz, −3,6 dB à 20 Hz ; à 45°, 4° d'écart à 30 Hz) ; le continu n'est jamais tourné.
- **Changement en lecture** : l'angle glisse en 20 ms par le plus court chemin (de 350° à 10° : par 0°), module constant, aucun clic (plus grand pas mesuré 0,0474 pour 0,0461 sur le sinus seul).
- **Coût** (stéréo, rotation + décalage fractionnaire) : 1,3 % d'un cœur à 48 kHz, 3,7 % à 96 kHz, 12,6 % à 192 kHz (sans rotation : comme en 1.x).
- **Pourquoi la latence augmente.** Pour tourner la phase d'un son de 40 Hz, il faut « voir » environ un demi-cycle avant et après (12,5 ms) : c'est une loi physique, pas un choix de programmation. Le budget de la 1.x (10 ms + 32 éch.) servait déjà entièrement à l'avance des décalages négatifs ; la rotation ajoute M (14,25 ms) pour tenir ±0,6° et ±0,2 dB dès 40 Hz (±2° aurait demandé 9,6 ms, ±1° 12,3 ms : on a pris la marge pour le module).

### 5.2 Bouton Align (alignement automatique sur le side-chain)

- Entrée **side-chain** facultative (mono ou stéréo) : on y envoie la piste de référence (l'autre micro).
- Clic sur **Align** : dans un fil de fond (jamais dans le fil audio), Lab Phase compare la piste (somme mono, ou G et D séparément si Link est coupé) au side-chain sur les ≈ 1,4 dernières secondes (65536 éch. à 48 kHz), avec la brique `DelayEstimator.h` (GCC-PHAT, la même mesure que Lab Corr), puis règle **décalage, polarité et rotation** pour caler la piste sur le side-chain. La rotation n'est réglée que si elle dépasse 3° ; sinon 0°.
- Si le décalage tombe entre deux échantillons et que l'unité est Échantillons, l'unité passe en ms (précision sous l'échantillon) ; le message le dit.
- Message (2 lignes) : « Aligned on the side-chain » / « +0.417 ms · polarity inverted · rotation 12° · confidence 94 % ». Refus expliqués : « No side-chain signal », « No clear match (confidence 2 %) », « Offset beyond 10 ms », « The track is late » (mode Zero, une avance serait nécessaire), « Not enough signal » (attente de 4 s au plus après le clic si la lecture vient de démarrer).
- Les réglages passent par les paramètres (annulables dans le logiciel hôte).
- **Copie adaptée de `DelayEstimator.h`** (la brique commune n'est pas modifiée) : affinage conjoint du décalage et de la rotation par régression pondérée de la phase de l'interspectre. Précision sur les tests de la brique : ≤ 0,002 éch. et 0,1° (au lieu de 0,011 éch. et 0,7°).
- **Grille interne des décalages en ms : 10 ns** (1 µs en 1.x) pour que la valeur mesurée soit appliquée telle quelle ; la souris, les flèches et la saisie gardent leurs pas (0,01 ms, 1 µs). Une valeur réglée en 1.x tombe toujours exactement au même endroit.
- **Mesures de bout en bout** (la piste traitée doit annuler le side-chain, gain mis à niveau) : side-chain = piste +20,37 éch. inversée → nul −70,7 dB ; piste en retard de 7,5 éch. et tournée de 60° → −64,4 dB ; piste +3,2 ms, −35°, inversée → −68,0 dB. Dans le plugin (fil de fond) : side-chain en avance de 10,5 éch. et tourné de 40° → réglé à −0,2188 ms, 40,0°, unité ms, nul −67 dB.

### 5.3 Mode de latence (« Latency », panneau avancé)

| | Standard (défaut) | Zero |
|---|---|---|
| Latence annoncée | ⌈10 ms⌉ + 32 + M : 1102 éch. (25,0 ms) à 44,1 kHz, 1197 (24,9 ms) à 48 kHz, 2361 (24,6 ms) à 96 kHz, 4689 (24,4 ms) à 192 kHz | 0 |
| Décalages | −10 à +10 ms, pas d'un échantillon ou sous l'échantillon (unité ms) | 0 à +10 ms seulement, à l'échantillon entier (une lecture entre deux échantillons sans latence est impossible) ; négatif → 0 |
| Rotation | oui | non (grisée) |

- Changement de mode : fondu de sortie de 10 ms, bascule, fondu d'entrée de 10 ms (aucun clic, plus grand pas mesuré égal à celui du sinus seul) ; en revenant en Standard, l'historique de la rotation est recalculé depuis l'entrée : la sortie est aussitôt identique à celle d'un moteur resté en Standard (écart 0). La latence annoncée change (à faire de préférence à l'arrêt : le logiciel recalcule sa compensation).
- En mode Zero, la case de valeur affiche le décalage réellement appliqué, la graduation « −10 ms » est grisée, la rotation affiche « off in Zero mode ».

### 5.4 Nouveaux réglages

| Identifiant | Nom | Plage | Défaut |
|---|---|---|---|
| `rotL` | Rotation L | 0 à 360° (pas de 1°, 0,1° avec Maj ou au clavier) | 0° |
| `rotR` | Rotation R | 0 à 360° | 0° |
| `latMode` | Latency | Standard · Zero | Standard |

- Link relie aussi les rotations (D recopie G). Les presets remettent la rotation à 0° (aucun ne la change) ; ils ne touchent ni l'unité ni le mode de latence.
- Une session des versions 1.x se rouvre avec rotation 0° et latence Standard (valeurs par défaut) : le son est identique, seulement retardé de 14,25 ms de plus, ce que le logiciel compense.

### 5.5 Interface (580 de large ; grille de 24 px)

- La face validée (écran, sélecteurs, lignes de décalage) ne bouge pas. Dessous :
  - **ROTATION** (titre en y 396, 24 px sous les repères du décalage) : lignes G (y 414) et D (y 462), mêmes colonnes que le décalage (curseur 306 px en x 124, case de valeur 116 px en x 440), Link propre au bloc (même réglage que celui du décalage). Curseur de 0° (à gauche) à 360°, partie colorée en dégradé teal #0A9396 → #79AFB0 (forme du signal), graduations 0 / 90 / 180 / 270 / 360°, repères sous la ligne D. Case de valeur : « 90° » (14 px) puis « = −270° » (11 px, gris doux) ; un clic ouvre la saisie (« 90 », « −90 », « 12.5 »).
  - **Rangée du bas** (y 534) : « Advanced settings » (x 16), latence annoncée (« Latency: 24.9 ms (1197 smp) », 11 px gris doux), bouton **Align** (88 px, bord droit en x 556).
  - **Panneau avancé** (y 578, 532 × 58, « Surface », coins 12, marge 16) : « Latency » [Standard | Zero] et une phrase d'explication.
- Écran **Temps** : une voie tournée montre son impulsion réelle, cos θ · δ − sin θ · h(t) : pointe raccourcie (couchée à 90°) et « traînes » en 1/t de part et d'autre ; l'angle s'affiche à côté du nom (« R Ø 90° »). « · zero latency » dans l'en-tête en mode Zero.
- Écran **Somme mono** : les creux se déplacent avec la différence de rotation ; sans décalage, niveau plat annoncé (« No dips, level −3.0 dB (L and R 90° apart) »).
- Fenêtre : 580 × 586 replié (+190 px : bloc Rotation et rangée du bas), 580 × 660 ouvert.

### 5.6 Vérifications 2.0.0

- `tests/quality_test.cpp` (nouveau, sans JUCE, lancé sur GitHub avant la fabrication Mac) : latence par mode et par fréquence d'échantillonnage ; rotation 40 Hz → 18 kHz à 4 fréquences d'échantillonnage (≤ 2°, ±0,2 dB) ; 0° / 180° / 360° au bit près ; rotation + décalage fractionnaire + polarité (0,003°) ; glissement sans clic ; mode Zero (copie exacte, pas d'avance) ; bascule de mode sans clic et sans écart ; Align de bout en bout (nul ≤ −40 dB) ; coût.
- `tests/core_test.cpp` : inchangé sauf la grille de 10 ns dans le calcul attendu du test « unité ms » ; tous les tests au bit près passent avec la nouvelle latence.
- `tests/processor_test.cpp` : les 9 tests 1.x passent ; en plus, sessions 1.x, mode Zero (latence 0, copies exactes), rotation et Link, Align direct (sortie = side-chain au bit près), Align en fil de fond (nul −67 dB), refus (pas de side-chain, signaux sans rapport, piste en retard en mode Zero).
- `tests/render_ui.cpp` : captures existantes, plus rotation (Temps, Somme mono), mode Zero avec panneau, résultat d'Align, 60 % et 200 %.
- pluginval niveau 10 (Linux, VST3 Debug, avec tests d'interface) : réussi.
- Non testé ici : écoute réelle, routage side-chain et changement de latence dans les logiciels (Logic, Live, Pro Tools…), Windows, AAX.
