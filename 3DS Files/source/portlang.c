#include "portlang.h"

#include <stdio.h>
#include <string.h>

#include "langtab.h"

// One row a string: English, French, German, Italian, Spanish, Swedish, in
// LangEnum's order. Latin-1, as retail's tables are, so each accent is its
// byte. "$NAME", "$N" and "$MAX" are filled in by whoever shows the line,
// and '@' breaks it, as in retail's.
static const char* const kStrings[PORTLANG_END - PORTLANG_FIRST][6] = {
    // PORTLANG_LOCAL_WIRELESS
    { "Local Wireless", "Sans fil local", "Lokal kabellos",
      "Wireless locale", "Inal\xe1mbrico local", "Lokalt tr\xe5" "dl\xf6st" },
    // PORTLANG_LOCAL_WIRELESS_BLURB
    { "Up to eight players, each on their own 3DS.",
      "Jusqu'\xe0 huit joueurs, chacun sur sa 3DS.",
      "Bis zu acht Spieler, jeder an seinem 3DS.",
      "Fino a otto giocatori, ognuno con il suo 3DS.",
      "Hasta ocho jugadores, cada uno con su 3DS.",
      "Upp till \xe5tta spelare, var och en med sin 3DS." },
    // PORTLANG_HOST
    { "Host", "Cr\xe9" "er", "Erstellen", "Crea", "Crear", "Skapa" },
    // PORTLANG_JOIN
    { "Join", "Rejoindre", "Beitreten", "Unisciti", "Unirse", "G\xe5 med" },
    // PORTLANG_HOST_OR_JOIN
    { "Host a race, or join one?",
      "Cr\xe9" "er une course ou en rejoindre une ?",
      "Ein Rennen erstellen oder einem beitreten?",
      "Creare una gara o unirsi a una?",
      "\xbf" "Crear una carrera o unirse a una?",
      "Skapa ett lopp eller g\xe5 med i ett?" },
    // PORTLANG_SEARCHING
    { "Looking for races...", "Recherche de courses...",
      "Suche nach Rennen...", "Ricerca di gare...", "Buscando carreras...",
      "Letar efter lopp..." },
    // PORTLANG_FOUND
    { "Race: $NAME@Players: $N/$MAX", "Course : $NAME@Joueurs : $N/$MAX",
      "Rennen: $NAME@Spieler: $N/$MAX", "Gara: $NAME@Giocatori: $N/$MAX",
      "Carrera: $NAME@Jugadores: $N/$MAX", "Lopp: $NAME@Spelare: $N/$MAX" },
    // PORTLANG_NEXT
    { "Next", "Suivant", "Weiter", "Avanti", "Siguiente", "N\xe4sta" },
    // PORTLANG_WAITING_FOR_PLAYERS
    { "Waiting for players:", "En attente de joueurs :",
      "Warte auf Spieler:", "In attesa di giocatori:",
      "Esperando jugadores:", "V\xe4ntar p\xe5 spelare:" },
    // PORTLANG_WAITING_FOR_HOST
    { "Waiting for $NAME to start the race.",
      "En attente du lancement de la course par $NAME.",
      "Warte, bis $NAME das Rennen startet.",
      "In attesa che $NAME avvii la gara.",
      "Esperando a que $NAME inicie la carrera.",
      "V\xe4ntar p\xe5 att $NAME startar loppet." },
    // PORTLANG_START
    { "Start", "Lancer", "Starten", "Avvia", "Empezar", "Starta" },
    // PORTLANG_LINK_LOST
    { "The link to $NAME was lost.",
      "La liaison avec $NAME a \xe9t\xe9 perdue.",
      "Die Verbindung zu $NAME ist abgebrochen.",
      "Il collegamento con $NAME si \xe8 interrotto.",
      "Se perdi\xf3 la conexi\xf3n con $NAME.",
      "Anslutningen till $NAME br\xf6ts." },
    // PORTLANG_WIRELESS_OFF: with the console's wireless on, which
    // PORTLANG_WIRELESS_DISABLED says when it is not
    { "Local wireless could not start. Please try again.",
      "Impossible de lancer le sans-fil local. Veuillez r\xe9" "essayer.",
      "Lokal kabellos konnte nicht starten. Bitte versuche es erneut.",
      "Impossibile avviare il wireless locale. Riprova.",
      "No se pudo iniciar el modo inal\xe1mbrico local. Int\xe9ntalo de "
      "nuevo.",
      "Lokalt tr\xe5" "dl\xf6st kunde inte starta. F\xf6rs\xf6k igen." },
    // PORTLANG_JOIN_FAILED
    { "Could not join the race.", "Impossible de rejoindre la course.",
      "Beitritt zum Rennen fehlgeschlagen.",
      "Impossibile unirsi alla gara.", "No se pudo unir a la carrera.",
      "Kunde inte g\xe5 med i loppet." },
    // PORTLANG_WAITING_FOR_CARS
    { "Waiting for a car from:", "En attente de la voiture de :",
      "Warte auf das Auto von:", "In attesa dell'auto di:",
      "Esperando el coche de:", "V\xe4ntar p\xe5 bil fr\xe5n:" },
    // PORTLANG_CPU_RACERS
    { "CPU Racers", "Pilotes IA", "KI-Fahrer", "Piloti CPU", "Pilotos CPU",
      "Datorf\xf6rare" },
    // PORTLANG_CPU_RACERS_N
    { "CPU racers: $N", "Pilotes IA : $N", "KI-Fahrer: $N",
      "Piloti CPU: $N", "Pilotos CPU: $N", "Datorf\xf6rare: $N" },
    // PORTLANG_WIRELESS_DISABLED
    { "Wireless communication is turned off. Turn it on from the HOME "
      "Menu to play over Local Wireless.",
      "La communication sans fil est d\xe9sactiv\xe9" "e. Activez-la "
      "depuis le menu HOME pour jouer en sans fil local.",
      "Die Drahtlos-Kommunikation ist deaktiviert. Aktiviere sie im "
      "HOME-Men\xfc, um lokal kabellos zu spielen.",
      "La comunicazione wireless \xe8 disattivata. Attivala dal menu HOME "
      "per giocare in wireless locale.",
      "La conexi\xf3n inal\xe1mbrica est\xe1 desactivada. Act\xedvala "
      "desde el men\xfa HOME para jugar en modo inal\xe1mbrico local.",
      "Tr\xe5" "dl\xf6s kommunikation \xe4r avst\xe4ngd. Sl\xe5 p\xe5 den "
      "fr\xe5n HOME-menyn f\xf6r att spela lokalt tr\xe5" "dl\xf6st." },
    // PORTLANG_HOST_MODE: its buttons are the main menu's two modes,
    // retail's 10023 and 10022
    { "Which mode will you host?",
      "Quel mode voulez-vous cr\xe9" "er ?",
      "Welchen Modus willst du erstellen?",
      "Quale modalit\xe0 vuoi creare?",
      "\xbf" "Qu\xe9 modo quieres crear?",
      "Vilket l\xe4ge vill du skapa?" },
    // PORTLANG_RECONNECTING: $N is the time left, m:ss
    { "Reconnecting... $N", "Reconnexion... $N",
      "Verbindung wird wiederhergestellt... $N", "Riconnessione... $N",
      "Reconectando... $N", "\xc5teransluter... $N" },
    // PORTLANG_CPU_DRIVES
    { "The CPU drives $NAME's car.", "L'IA conduit la voiture de $NAME.",
      "Die KI f\xe4hrt das Auto von $NAME.", "La CPU guida l'auto di $NAME.",
      "La CPU conduce el coche de $NAME.", "Datorn k\xf6r $NAMEs bil." },
    // PORTLANG_CATCHING_UP: $N is how far, in percent
    { "Catching up with the race... $N%",
      "Rattrapage de la course... $N %",
      "Das Rennen wird aufgeholt... $N %",
      "Recupero della gara... $N%",
      "Alcanzando la carrera... $N %",
      "Kommer ikapp loppet... $N %" },
    // PORTLANG_COMING_BACK: the same, $NAME's
    { "$NAME is coming back into the race... $N%",
      "$NAME revient dans la course... $N %",
      "$NAME kehrt ins Rennen zur\xfc" "ck... $N %",
      "$NAME sta rientrando in gara... $N%",
      "$NAME vuelve a la carrera... $N %",
      "$NAME kommer tillbaka till loppet... $N %" },
    // PORTLANG_SURROUND: the 3DS's own Surround output, the name its System
    // Settings give it, where retail offered Dolby Pro Logic II (110011)
    { "Surround", "Surround", "Surround", "Surround", "Envolvente",
      "Surround" },
    // PORTLANG_RACER and PORTLANG_COP: Options' Controller, whose keys are
    // the racer's or the cop's car's (joyconfig.h)
    { "Racer", "Pilote", "Fahrer", "Pilota", "Piloto", "F\xf6rare" },
    { "Cop", "Policier", "Cop", "Poliziotto", "Poli", "Polis" },
    // PORTLANG_ROADBLOCK and PORTLANG_HELICOPTER: the cop's calls, in the
    // words retail's Be A Cop Blurb has for them (970001)
    { "Roadblock", "Barrage routier", "Stra\xdf" "ensperre",
      "Blocco stradale", "Corte de carretera", "V\xe4gsp\xe4rr" },
    { "Helicopter", "H\xe9licopt\xe8re", "Helikopter", "Elicottero",
      "Helic\xf3ptero", "Helikopter" },
    // PORTLANG_CONTROLS_BLURB
#ifdef NFSHP2_R36S
#define CONTROLS_SELECT "\x86"
#else
#define CONTROLS_SELECT "A"
#endif
    { "Pick an action and press " CONTROLS_SELECT ", then press its new key. The action "
      "that had that key takes the old one. On the top line, Left and "
      "Right switch between the racer's keys and the cop's.",
      "Choisissez une action et appuyez sur " CONTROLS_SELECT ", puis sur son nouveau "
      "bouton. L'action qui avait ce bouton prend l'ancien. Sur la ligne "
      "du haut, Gauche et Droite passent des boutons du pilote \xe0 ceux "
      "du policier.",
      "W\xe4hle eine Aktion und dr\xfc" "cke " CONTROLS_SELECT ", dann ihre neue Taste. Die "
      "Aktion, die diese Taste hatte, bekommt die alte. In der obersten "
      "Zeile wechseln Links und Rechts zwischen den Tasten des Fahrers "
      "und denen des Cops.",
      "Scegli un'azione e premi " CONTROLS_SELECT ", poi il suo nuovo tasto. L'azione che "
      "aveva quel tasto prende quello vecchio. Sulla riga in alto, "
      "Sinistra e Destra passano dai tasti del pilota a quelli del "
      "poliziotto.",
      "Elige una acci\xf3n y pulsa " CONTROLS_SELECT ", y luego su nuevo bot\xf3n. La "
      "acci\xf3n que ten\xed" "a ese bot\xf3n se queda con el anterior. En "
      "la l\xednea de arriba, Izquierda y Derecha cambian entre los "
      "botones del piloto y los del poli.",
      "V\xe4lj en handling och tryck p\xe5 " CONTROLS_SELECT ", sedan p\xe5 dess nya knapp. "
      "Handlingen som hade knappen f\xe5r den gamla. P\xe5 \xf6versta "
      "raden v\xe4xlar V\xe4nster och H\xf6ger mellan f\xf6rarens knappar "
      "och polisens." },
#undef CONTROLS_SELECT
    // PORTLANG_CONTROLS_PRESS: $NAME is the action's label
    { "Press the new key for $NAME. START cancels.",
      "Appuyez sur le nouveau bouton pour $NAME. START annule.",
      "Dr\xfc" "cke die neue Taste f\xfcr $NAME. START bricht ab.",
      "Premi il nuovo tasto per $NAME. START annulla.",
      "Pulsa el nuevo bot\xf3n para $NAME. START cancela.",
      "Tryck p\xe5 den nya knappen f\xf6r $NAME. START avbryter." },
    // PORTLANG_SAVE_CHANGES: the save prompt of a screen left with changes
    { "Save your changes to the SD Card?",
      "Sauvegarder vos modifications sur la carte SD ?",
      "Deine \xc4nderungen auf der SD Card speichern?",
      "Salvare le modifiche sulla scheda SD?",
      "\xbfGuardar los cambios en la tarjeta SD?",
      "Spara dina \xe4ndringar p\xe5 SD-kortet?" },
    // PORTLANG_SAVE_CHANGES_RACE: the debug menu's Start Race
    { "Save your changes to the SD Card before the race?",
      "Sauvegarder vos modifications sur la carte SD avant la course ?",
      "Deine \xc4nderungen vor dem Rennen auf der SD Card speichern?",
      "Salvare le modifiche sulla scheda SD prima della gara?",
      "\xbfGuardar los cambios en la tarjeta SD antes de la carrera?",
      "Spara dina \xe4ndringar p\xe5 SD-kortet f\xf6re loppet?" },
    // PORTLANG_GRAPHICS_FULL .. PORTLANG_GRAPHICS_BLURB: Display Options'
    // row for the R36S's speed presets (r36s_quality), whole, as the row's
    // own text holds 16 characters and a LanguageID's has no such limit
    { "Graphics: Full", "Graphismes : Compl\xe8te", "Grafik: Voll",
      "Grafica: Completa", "Gr\xe1" "ficos: Completa", "Grafik: Full" },
    { "Graphics: Fast", "Graphismes : Rapide", "Grafik: Schnell",
      "Grafica: Veloce", "Gr\xe1" "ficos: R\xe1pida", "Grafik: Snabb" },
    { "Graphics: Fastest", "Graphismes : Tr\xe8s rapide",
      "Grafik: Am schnellsten", "Grafica: Velocissima",
      "Gr\xe1" "ficos: Muy r\xe1pida", "Grafik: Snabbast" },
    { "Full draws the game as the PS2 does. Fast redraws the reflections "
      "and the mirror less often. Fastest also draws the 3D smaller.",
      "Compl\xe8te dessine le jeu comme la PS2. Rapide redessine moins "
      "souvent les reflets et le r\xe9troviseur. Tr\xe8s rapide dessine "
      "aussi la 3D en plus petit.",
      "Voll zeichnet das Spiel wie die PS2. Schnell zeichnet Spiegelungen "
      "und R\xfc" "ckspiegel seltener neu. Am schnellsten zeichnet die 3D "
      "au\xdf" "erdem kleiner.",
      "Completa disegna il gioco come la PS2. Veloce ridisegna meno spesso "
      "i riflessi e lo specchietto. Velocissima disegna anche il 3D pi\xf9 "
      "piccolo.",
      "Completa dibuja el juego como la PS2. R\xe1pida redibuja con menos "
      "frecuencia los reflejos y el retrovisor. Muy r\xe1pida dibuja "
      "adem\xe1s el 3D m\xe1s peque\xf1o.",
      "Full ritar spelet som PS2. Snabb ritar om reflexerna och backspegeln "
      "mer s\xe4llan. Snabbast ritar dessutom 3D-grafiken mindre." },
    // PORTLANG_SMOKE_FULL .. PORTLANG_SMOKE_BLURB: the Smoke row (R36S)
    { "Smoke: Full", "Fum\xe9" "e : Compl\xe8te", "Rauch: Voll", "Fumo: Completo",
      "Humo: Completo", "R\xf6" "k: Full" },
    { "Smoke: Less", "Fum\xe9" "e : Moins", "Rauch: Weniger", "Fumo: Meno",
      "Humo: Menos", "R\xf6" "k: Mindre" },
    { "Smoke: Least", "Fum\xe9" "e : Le moins", "Rauch: Am wenigsten",
      "Fumo: Il meno", "Humo: Lo m\xed" "nimo", "R\xf6" "k: Minst" },
    { "Full draws every smoke and dust puff, as the PS2 does. Less and Least "
      "draw fewer of them, and thin a dense cloud: they cost the GPU less.",
      "Compl\xe8te dessine chaque bouff\xe9" "e de fum\xe9" "e et de poussi\xe8" "re, "
      "comme la PS2. Moins et Le moins en dessinent moins et all\xe8" "gent un "
      "nuage dense : elles co\xfbtent moins au GPU.",
      "Voll zeichnet jede Rauch- und Staubwolke, wie die PS2. Weniger und Am "
      "wenigsten zeichnen weniger davon und lichten dichte Wolken aus: sie "
      "kosten die GPU weniger.",
      "Completo disegna ogni sbuffo di fumo e polvere, come la PS2. Meno e Il "
      "meno ne disegnano meno e diradano le nubi dense: costano meno alla GPU.",
      "Completo dibuja cada bocanada de humo y polvo, como la PS2. Menos y Lo "
      "m\xed" "nimo dibujan menos y aclaran las nubes densas: cuestan menos a la "
      "GPU.",
      "Full ritar varje r\xf6" "k- och dammoln, som PS2. Mindre och Minst ritar "
      "f\xe4" "rre och glesar ut t\xe4" "ta moln: de kostar GPU:n mindre." },
    // PORTLANG_FACE_STANDARD .. PORTLANG_FACE_BLURB: Options' Controller,
    // Face buttons (R36S), as the whole row's text
    { "Face buttons: Standard", "Boutons de face : Standard",
      "Aktionstasten: Standard", "Tasti azione: Standard",
      "Botones: Est\xe1" "ndar", "Knappar: Standard" },
    { "Face buttons: Swap A/B & X/Y", "Boutons de face : Inverser A/B et X/Y",
      "Aktionstasten: A/B und X/Y tauschen", "Tasti azione: Scambia A/B e X/Y",
      "Botones: Cambiar A/B y X/Y", "Knappar: Byt A/B och X/Y" },
    { "Pick how the four face buttons are read. Standard is your device's "
      "layout; Swap A/B & X/Y exchanges the bottom and right buttons, and the "
      "left and top ones, for pads whose buttons come out crossed.",
      "Choisissez comment lire les quatre boutons de face. Standard suit la "
      "disposition de l'appareil ; Inverser A/B et X/Y \xe9" "change les "
      "boutons du bas et de droite, puis ceux de gauche et du haut, pour les "
      "manettes dont les boutons sont crois\xe9" "s.",
      "Legt fest, wie die vier Aktionstasten gelesen werden. Standard ist die "
      "Belegung deines Ger\xe4" "ts; A/B und X/Y tauschen vertauscht die "
      "untere mit der rechten und die linke mit der oberen Taste, f\xfc" "r "
      "Pads mit vertauschten Tasten.",
      "Scegli come leggere i quattro tasti azione. Standard \xe8 la "
      "disposizione del dispositivo; Scambia A/B e X/Y scambia il tasto in "
      "basso con quello a destra, e quello a sinistra con quello in alto, per "
      "i controller con i tasti incrociati.",
      "Elige c\xf3" "mo se leen los cuatro botones. Est\xe1" "ndar es la "
      "disposici\xf3" "n de tu dispositivo; Cambiar A/B y X/Y intercambia el "
      "bot\xf3" "n de abajo con el de la derecha, y el de la izquierda con el "
      "de arriba, para mandos con los botones cruzados.",
      "V\xe4" "lj hur de fyra knapparna l\xe4" "ses. Standard \xe4" "r enhetens "
      "layout; Byt A/B och X/Y byter nedre och h\xf6" "gra knappen, samt "
      "v\xe4" "nstra och \xf6" "vre, f\xf6" "r handkontroller med korsade "
      "knappar." },
};

// Retail's memory card messages and the Load/Save screen's help lines,
// said for the 3DS: retail's text in each language with the card's words
// put as the SD Card's and the MEMORY CARD slot dropped
// (docs/formats/language-table.md, "The port's wording"). The slot went
// first in the two that need space, so they take the KB as "%2$d"
// (memcard.c's formatPrompt). The help lines keep their buttons: 0x82 0x83
// the D-pad, 0x86 0x80 Cross and START.
static const struct {
    int32_t     id;
    const char* text[6];
} kRetail[] = {
    { 150004, {
        "SD Card",
        "Carte SD",
        "SD Card",
        "Scheda SD",
        "Tarjeta SD",
        "SD-kort"
    } },
    { 151001, {
        "Do not remove the SD Card or turn the power off. @ @ Loading game "
        "settings.",
        "Ne retirez pas la carte SD et n'\xe9teignez pas la console. @ @ "
        "Chargement des param\xe8tres de la partie...",
        "Die SD Card nicht entfernen. Konsole nicht ausschalten. @ "
        "@Spieleinstellungen werden geladen.",
        "Non rimuovere la scheda SD. Non spegnere la console. @ @ "
        "Caricamento impostazioni di gioco.",
        "No retires la tarjeta SD. No apagues la consola. @ @ Cargando "
        "par\xe1metros del juego.",
        "Ta inte ur ditt SD-kort. Sl\xe5 inte av str\xf6mmen. @ @ Laddar "
        "spelinst\xe4llningar."
    } },
    { 151003, {
        "No saved game settings found on the SD Card",
        "Aucun param\xe8tre sauvegard\xe9 n'a \xe9t\xe9 d\xe9tect\xe9 sur "
        "la carte SD.",
        "Auf der SD Card wurden keine gespeicherten Einstellungen gefunden.",
        "Nessuna partita salvata sulla scheda SD",
        "No se han encontrado par\xe1metros del juego guardados en la "
        "tarjeta SD.",
        "Inga sparade spelinst\xe4llningar hittades p\xe5 ditt SD-kort"
    } },
    { 151004, {
        "Saving game settings to the SD Card.@Do not remove the SD Card or "
        "turn the power off.",
        "Sauvegarde des param\xe8tres de la partie sur la carte SD en "
        "cours. Ne retirez pas la carte SD et n'\xe9teignez pas la console.",
        "Spieldaten werden auf der SD Card gespeichert. Die SD Card nicht "
        "entfernen. Konsole nicht ausschalten.",
        "Salvataggio del file di gioco sulla scheda SD@Non rimuovere la "
        "scheda SD. Non spegnere la console.",
        "Guardando par\xe1metros del juego en la tarjeta SD.@No retires la "
        "tarjeta SD. No apagues la consola.",
        "Sparar spelinst\xe4llningar till SD-kortet.@Ta inte ur ditt "
        "SD-kort. Sl\xe5 inte av str\xf6mmen."
    } },
    { 151006, {
        "No SD Card detected. @ @Would you like to retry?",
        "Aucune carte SD n'a \xe9t\xe9 d\xe9tect\xe9" "e.@Voulez-vous "
        "recommencer ?",
        "Es wurde keine SD Card gefunden.@ @M\xf6" "chtest du es erneut "
        "versuchen?",
        "Nessuna scheda SD rilevata. @ @Vuoi riprovare?",
        "No se ha detectado ninguna tarjeta SD. @\xbfQuieres volver a "
        "intentarlo?",
        "Inget SD-kort hittat. @ @Vill du f\xf6rs\xf6ka igen?"
    } },
    { 151007, {
        "The SD Card is unformatted. Would you like to format it?",
        "La carte SD n'est pas format\xe9" "e. Souhaitez-vous la formater ?",
        "Die SD Card ist nicht formatiert. Soll jetzt formatiert werden?",
        "La scheda SD non \xe8 formattata. Vuoi formattarla?",
        "La tarjeta SD no est\xe1 formateada. \xbfQuieres formatearla?",
        "Ditt SD-kort \xe4r inte formaterat. Vill du formatera det?"
    } },
    { 151008, {
        "Do not remove the SD Card. @ @ Formatting...",
        "Ne retirez pas la carte SD. @ @ Formatage en cours...",
        "Die SD Card nicht entfernen. @ @ Formatierung wird "
        "durchgef\xfchrt ...",
        "Non rimuovere la scheda SD. @ @ Formattazione in corso...",
        "No retires la tarjeta SD. @ @ Formateando...",
        "Ta inte ur ditt SD-kort. @ @ Formaterar..."
    } },
    { 151009, {
        "The SD Card was formatted successfully!",
        "Formatage de la carte SD r\xe9ussi !",
        "Die SD Card wurde erfolgreich formatiert!",
        "La scheda SD \xe8 stata formattata correttamente!",
        "\xa1Se ha formateado la tarjeta SD!",
        "Ditt SD-kort formaterat!"
    } },
    { 151010, {
        "Format failed for the SD Card. @ @ Do you want to try again?",
        "Echec du formatage de la carte SD. @ @ Souhaitez-vous "
        "r\xe9" "essayer ?",
        "Die Formatierung der SD Card ist fehlgeschlagen. @ @ "
        "M\xf6" "chtest du es erneut versuchen?",
        "Formattazione fallita della scheda SD. @ @ Vuoi riprovare?",
        "Error de formato para la tarjeta SD. @ @ \xbfQuieres volver a "
        "intentarlo?",
        "Kunde inte formatera ditt SD-kort. @ @ Vill du f\xf6rs\xf6ka igen?"
    } },
    { 151011, {
        "There is not enough space to save the game on the SD Card. Need "
        "For Speed: Hot Pursuit 2 requires %2$d KB free. Do you want to "
        "try again?",
        "La carte SD poss\xe8" "de un espace insuffisant. La sauvegarde "
        "des donn\xe9" "es de Need For Speed : Poursuite Infernale 2 "
        "n\xe9" "cessite %2$dKB d'espace libre. Souhaitez-vous "
        "r\xe9" "essayer ?",
        "Es ist nicht gen\xfcgend freier Speicher vorhanden, um das Spiel "
        "auf der SD Card zu speichern. F\xfcr Need For Speed: Hot Pursuit "
        "2 werden %2$d KB freier Speicherplatz ben\xf6tigt. M\xf6" "chtest "
        "du es noch mal versuchen?",
        "Spazio insufficiente sulla scheda SD. Need For Speed: Hot Pursuit "
        "2 richiede %2$d KB liberi. Vuoi riprovare?",
        "No hay espacio suficiente para guardar la partida en la tarjeta "
        "SD. Need For Speed: Hot Pursuit 2 requiere %2$d KB libres. "
        "\xbfQuieres volver a intentarlo?",
        "Det finns inte tillr\xe4" "ckligt med minne f\xf6r att spara "
        "spelet p\xe5 ditt SD-kort. Need For Speed: Hot Pursuit 2 "
        "kr\xe4ver %2$d KB ledigt utrymme. Vill du f\xf6rs\xf6ka igen?"
    } },
    { 151012, {
        "Save failed for the SD Card.@Do you want to try again?",
        "Echec de la sauvegarde sur la carte SD.@Souhaitez-vous "
        "r\xe9" "essayer ?",
        "Die Daten konnten nicht auf der SD Card gespeichert "
        "werden.@M\xf6" "chtest du es noch mal versuchen?",
        "Salvataggio fallito sulla scheda SD.@Vuoi riprovare?",
        "Error de guardado en la tarjeta SD.@\xbfQuieres volver a "
        "intentarlo?",
        "Kunde inte spara p\xe5 SD-kortet.@Vill du f\xf6rs\xf6ka igen?"
    } },
    { 151013, {
        "Checking the SD Card",
        "V\xe9rification de la carte SD...",
        "Die Suche nach einer SD Card wurde gestartet.",
        "Controllo della scheda SD",
        "Comprobando la tarjeta SD.",
        "Letar efter SD-kort"
    } },
    { 151014, {
        "A new SD Card was found.",
        "Une nouvelle carte SD a \xe9t\xe9 d\xe9tect\xe9" "e.",
        "Es wurde eine neue SD Card gefunden.",
        "\xc8 stata rilevata una nuova scheda SD.",
        "Se ha encontrado una tarjeta SD nueva.",
        "Ett nytt SD-kort hittades."
    } },
    { 151015, {
        "No SD Card detected.@ Would you like to retry loading?",
        "Aucune carte SD n'a \xe9t\xe9 d\xe9tect\xe9" "e.",
        "Es wurde keine SD Card gefunden.@M\xf6" "chtest du erneut "
        "versuchen, zu laden?",
        "Nessuna scheda SD rilevata. Vuoi riprovare il caricamento?",
        "No se ha detectado ninguna tarjeta SD. \xbfQuieres volver a "
        "intentar cargar?",
        "Inget SD-kort hittat.@ Vill du f\xf6rs\xf6ka ladda igen?"
    } },
    { 151016, {
        "Invalid SD Card @ @ The inserted device is not an SD Card.",
        "Carte SD non valide @ @ L'accessoire ins\xe9r\xe9 n'est pas une "
        "carte SD.",
        "Ung\xfcltige SD Card @ @ Der eingesteckte Gegenstand ist keine SD "
        "Card.",
        "Scheda SD non valida @ @ La periferica inserita non \xe8 una "
        "scheda SD.",
        "Tarjeta SD no v\xe1lida @ @ El dispositivo insertado no es una "
        "tarjeta SD.",
        "Ogiltigt SD-kort @ @ Enheten \xe4r inget SD-kort."
    } },
    { 151017, {
        "No SD Card detected.",
        "Aucune carte SD n'a \xe9t\xe9 d\xe9tect\xe9" "e.",
        "Es wurde keine SD Card gefunden.",
        "Nessuna scheda SD individuata.",
        "No se ha detectado ninguna tarjeta SD.",
        "Inget SD-kort hittat."
    } },
    { 151018, {
        "Load failed for the SD Card.@Would you like to try again?",
        "Echec du chargement \xe0 partir de la carte SD. @Souhaitez-vous "
        "r\xe9" "essayer ?",
        "Laden von der SD Card fehlgeschlagen.@M\xf6" "chtest du es noch "
        "mal versuchen?",
        "Caricamento fallito della scheda SD.@Vuoi riprovare?",
        "Error al cargar la tarjeta SD. @\xbfQuieres volver a intentarlo?",
        "Kunde inte ladda ditt SD-kort.@Vill du f\xf6rs\xf6ka igen?"
    } },
    { 151020, {
        "Overwrite existing game settings@on the SD Card?",
        "Ecraser les param\xe8tres sauvegard\xe9s@sur la carte SD ?",
        "M\xf6" "chtest du die aktuellen Spieleinstellungen@auf der SD "
        "Card \xfc" "berschreiben?",
        "Sovrascrivere le impostazioni di gioco@esistenti sulla scheda SD?",
        "\xbfReemplazar los par\xe1metros del juego existentes en la "
        "tarjeta SD?",
        "Skriv \xf6ver nuvarande spelinst\xe4llningar p\xe5 SD-kortet?"
    } },
    { 151022, {
        "Current settings will be overwritten by the game settings on the "
        "SD Card. Continue?",
        "Les param\xe8tres actuels seront remplac\xe9s par ceux "
        "pr\xe9sents sur la carte SD. Continuer ?",
        "Die aktuellen Einstellungen werden durch die Spieleinstellungen "
        "auf der SD Card \xfc" "berschrieben. Fortfahren?",
        "Le impostazioni attuali saranno sostituite dalle impostazioni di "
        "gioco presenti sulla scheda SD. Continuare?",
        "Los par\xe1metros actuales ser\xe1n reemplazados por los "
        "par\xe1metros del juego en la tarjeta SD. \xbf" "Deseas continuar?",
        "Nuvarande inst\xe4llningar kommer att skrivas \xf6ver av "
        "spelinst\xe4llningarna p\xe5 ditt SD-kort. Forts\xe4tt?"
    } },
    { 151023, {
        "Create Saved Game on the SD Card?",
        "Cr\xe9" "er une sauvegarde sur la carte SD ?",
        "Gespeichertes Spiel auf der SD Card erstellen.",
        "Creazione del file di salvataggio sulla scheda SD.",
        "\xbf" "Crear partida guardada en la tarjeta SD?",
        "Skapa sparad spelomg\xe5ng p\xe5 ditt SD-kort?"
    } },
    { 151024, {
        "Load failed for the SD Card.",
        "Echec du chargement \xe0 partir de la carte SD.",
        "Laden von der SD Card fehlgeschlagen.",
        "Caricamento fallito dalla scheda SD.",
        "Error de cargado en la tarjeta SD.",
        "Kunde inte ladda ditt SD-kort."
    } },
    { 151025, {
        "No SD Card detected.",
        "Aucune carte SD n'a \xe9t\xe9 d\xe9tect\xe9" "e.",
        "Keine SD Card gefunden.",
        "Nessuna scheda SD individuata.",
        "No se ha detectado ninguna tarjeta SD.",
        "Inget SD-kort hittat."
    } },
    { 151027, {
        "No SD Card detected. Need For Speed: Hot Pursuit 2 requires %2$d "
        "KB free. Do you want to try again?",
        "Aucune carte SD n'a \xe9t\xe9 d\xe9tect\xe9" "e. Need For Speed : "
        "Poursuite Infernale 2 n\xe9" "cessite %2$dKB. Voulez-vous "
        "r\xe9" "essayer ?",
        "Keine SD Card gefunden. F\xfcr Need For Speed: Hot Pursuit 2 "
        "werden %2$d KB freier Speicherplatz ben\xf6tigt. M\xf6" "chtest "
        "du es noch mal versuchen?",
        "Nessuna scheda SD rilevata. Need For Speed: Hot Pursuit 2 "
        "richiede almeno %2$d KB di spazio disponibile. Vuoi riprovare?",
        "No se ha detectado ninguna tarjeta SD. Need For Speed: Hot "
        "Pursuit 2 requiere %2$d KB libres. \xbfQuieres intentarlo de "
        "nuevo?",
        "Inget SD-kort hittat. Need For Speed: Hot Pursuit 2 kr\xe4ver "
        "%2$d KB ledigt utrymme. Vill du f\xf6rs\xf6ka igen?"
    } },
    { 151101, {
        "The SD Card is unformatted.",
        "La carte SD n'est pas format\xe9" "e.",
        "Die SD Card ist nicht formatiert.",
        "La scheda SD non \xe8 formattata.",
        "La tarjeta SD no est\xe1 formateada.",
        "Ditt SD-kort \xe4r inte formaterat"
    } },
    { 152001, {
        "Do not remove the SD Card. @ @ Unformatting...",
        "Ne retirez pas la carte SD. @ @ Annulation du formatage en "
        "cours...",
        "Die SD Card nicht entfernen. @ @ Formatierung wird "
        "r\xfc" "ckg\xe4ngig gemacht ...",
        "Non rimuovere la scheda SD. @ @ Deformattazione in corso...",
        "No retires la tarjeta SD. @ @ Quitando formato...",
        "Ta inte ur ditt SD-kort. @ @ Avformaterar..."
    } },
    { 800070, {
        "\x82 \x83 Select SD Card function",
        "\x82 \x83 Parcourir les options de carte SD",
        "\x82 \x83 SD Card-Funktion ausw\xe4hlen",
        "\x82 \x83 seleziona funzione scheda SD",
        "\x82 \x83 Seleccionar funci\xf3n tarjeta SD",
        "\x82 \x83 V\xe4lj SD-kort-funktion"
    } },
    { 800071, {
        "\x86 \x80 Activate SD Card function",
        "\x86 \x80 S\xe9lectionner l'option de carte SD",
        "\x86 \x80 SD Card-Funktion aktivieren",
        "\x86 \x80 attiva funzione scheda SD",
        "\x86 \x80 Activar funci\xf3n tarjeta SD",
        "\x86 \x80 Aktivera SD-kort-funktion"
    } },
};

const char* portlang_get(int32_t languageEnum, int32_t id, size_t* outLen)
{
    static char        number[PORTLANG_END - PORTLANG_FIRST][12];
    const char* const* row;
    const char*        s;

    if (id < PORTLANG_FIRST || id >= PORTLANG_END)
        return NULL;
    row = kStrings[id - PORTLANG_FIRST];
    if (languageEnum == LANG_ID_NUM) {
        s = number[id - PORTLANG_FIRST];
        if (!s[0])
            snprintf(number[id - PORTLANG_FIRST], sizeof number[0], "%ld",
                     (long)id);
    } else if (languageEnum == LANG_LONGEST) {
        int i;

        s = row[0];
        for (i = 1; i < 6; i++)
            if (strlen(row[i]) > strlen(s))
                s = row[i];
    } else {
        s = row[languageEnum >= LANG_EN && languageEnum <= LANG_SE
                    ? languageEnum : LANG_EN];
    }
    if (outLen)
        *outLen = strlen(s);
    return s;
}

const char* portlang_retail(int32_t languageEnum, int32_t id, size_t* outLen)
{
    size_t i;

    if (languageEnum < LANG_EN || languageEnum > LANG_SE)
        return NULL;
    for (i = 0; i < sizeof kRetail / sizeof kRetail[0]; i++)
        if (kRetail[i].id == id) {
            const char* s = kRetail[i].text[languageEnum];

            if (outLen)
                *outLen = strlen(s);
            return s;
        }
    return NULL;
}
