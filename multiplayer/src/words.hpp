// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
// Original mod UI text in the five PAL languages. UTF-8 is converted by the port's UI API.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace mp {

enum class Language {
    kEnglish,
    kFrench,
    kGerman,
    kSpanish,
    kItalian
};

// The game's index, not the console's language ID. The unused index 1 and unknowns use English.
inline Language language_from_game(int index) {
    return index >= 2 && index <= 5 ? static_cast<Language>(index - 1) : Language::kEnglish;
}

// PAL D_0015EE88; NTSC game_language (the port's rac1_version.h maps the two).
// Unknown discs must never read an address belonging to a supported disc.
inline std::uint32_t language_address(std::string_view disc) {
    return disc == "pal" ? 0x0015EE88u : disc == "ntsc" ? 0x0015ED88u : 0;
}

struct Words {
    const char* text[5];
};

inline constexpr Words kWords[] = {
    {{"Multiplayer", "Multijoueur", "Mehrspieler", "Multijugador", "Multigiocatore"}},
    {{"Browse games", "Trouver une partie", "Spiele suchen", "Buscar partidas", "Cerca partite"}},
    {{"Create match", "Créer une partie", "Spiel erstellen", "Crear partida", "Crea partita"}},
    {{"Join by code",
      "Rejoindre par code",
      "Mit Code beitreten",
      "Unirse por código",
      "Unisciti con codice"}},
    {{"Direct connect",
      "Connexion directe",
      "Direktverbindung",
      "Conexión directa",
      "Connessione diretta"}},
    {{"Playing as ", "Joueur : ", "Spieler: ", "Jugador: ", "Giocatore: "}},
    {{"Party code ", "Code : ", "Gruppencode ", "Código: ", "Codice gruppo: "}},
    {{"Party code", "Code du groupe", "Gruppencode", "Código del grupo", "Codice gruppo"}},
    {{"Character ", "Caractère ", "Zeichen ", "Carácter ", "Carattere "}},
    {{"Join", "Rejoindre", "Beitreten", "Unirse", "Unisciti"}},
    {{"Host directly",
      "Héberger en direct",
      "Direkt hosten",
      "Alojar directamente",
      "Ospita direttamente"}},
    {{"Join by address",
      "Rejoindre une adresse",
      "Mit Adresse beitreten",
      "Unirse por dirección",
      "Unisciti con indirizzo"}},
    {{"No server: port ",
      "Sans serveur : port ",
      "Ohne Server: Port ",
      "Sin servidor: puerto ",
      "Senza server: porta "}},
    {{", TCP and UDP", ", TCP et UDP", ", TCP und UDP", ", TCP y UDP", ", TCP e UDP"}},
    {{"Join ", "Rejoindre ", "Beitreten: ", "Unirse a ", "Unisciti a "}},
    {{"Address part ",
      "Partie d'adresse ",
      "Adressteil ",
      "Parte de dirección ",
      "Parte indirizzo "}},
    {{"Port", "Port", "Port", "Puerto", "Porta"}},
    {{"Port, by 100",
      "Port, par 100",
      "Port, in 100ern",
      "Puerto, de 100 en 100",
      "Porta, passi di 100"}},
    {{"Plays without the server",
      "Jeu sans serveur",
      "Spielen ohne Server",
      "Juega sin servidor",
      "Gioca senza server"}},
    {{"Start", "Démarrer", "Starten", "Empezar", "Avvia"}},
    {{"Join the game",
      "Rejoindre le jeu",
      "Ins Spiel einsteigen",
      "Entrar al juego",
      "Entra in gioco"}},
    {{"Not ready", "Pas prêt", "Nicht bereit", "No listo", "Non pronto"}},
    {{"Ready", "Prêt", "Bereit", "Listo", "Pronto"}},
    {{"Leave", "Quitter", "Verlassen", "Salir", "Esci"}},
    {{"Party ", "Groupe ", "Gruppe ", "Grupo ", "Gruppo "}},
    {{"Party at ", "Groupe sur ", "Gruppe auf ", "Grupo en ", "Gruppo su "}},
    {{"Playing: ", "Partie : ", "Spiel: ", "Partida: ", "Partita: "}},
    {{"Receiving the host's game...",
      "Réception de la partie...",
      "Empfange Spiel des Hosts...",
      "Recibiendo partida del anfitrión...",
      "Ricezione partita dell'host..."}},
    {{" (host)", " (hôte)", " (Host)", " (anfitrión)", " (host)"}},
    {{" (ready)", " (prêt)", " (bereit)", " (listo)", " (pronto)"}},
    {{" (not ready)", " (pas prêt)", " (nicht bereit)", " (no listo)", " (non pronto)"}},
    {{" - you", " - vous", " - du", " - tú", " - tu"}},
    {{" of ", " sur ", " von ", " de ", " di "}},
    {{" players", " joueurs", " Spieler", " jugadores", " giocatori"}},
    {{", not all ready",
      ", joueurs pas prêts",
      ", nicht alle bereit",
      ", faltan jugadores listos",
      ", non tutti pronti"}},
    {{"port ", "port ", "Port ", "puerto ", "porta "}},
    {{"Not an address: ",
      "Adresse invalide : ",
      "Ungültige Adresse: ",
      "Dirección no válida: ",
      "Indirizzo non valido: "}},
    {{"Stop hosting? The party ends for everyone.",
      "Arrêter ? Le groupe sera dissous.",
      "Hosting beenden? Die Gruppe wird aufgelöst.",
      "¿Dejar de alojar? El grupo se disolverá.",
      "Terminare l'host? Il gruppo si scioglierà."}},
    {{"Leave the party? It ends for everyone.",
      "Quitter ? Le groupe sera dissous.",
      "Verlassen? Die Gruppe wird aufgelöst.",
      "¿Salir? El grupo se disolverá.",
      "Uscire? Il gruppo si scioglierà."}},
    {{"Leave the party?",
      "Quitter le groupe ?",
      "Gruppe verlassen?",
      "¿Salir del grupo?",
      "Uscire dal gruppo?"}},
    {{"Save ", "Sauvegarde ", "Spielstand ", "Partida ", "Salvataggio "}},
    {{"empty", "vide", "leer", "vacía", "vuoto"}},
    {{"Host with which save?",
      "Quelle sauvegarde héberger ?",
      "Welchen Spielstand hosten?",
      "¿Qué partida alojar?",
      "Quale salvataggio ospitare?"}},
    {{"Play which save?",
      "Quelle sauvegarde jouer ?",
      "Welchen Spielstand spielen?",
      "¿Qué partida jugar?",
      "Quale salvataggio giocare?"}},
    {{"New game", "Nouvelle partie", "Neues Spiel", "Nueva partida", "Nuova partita"}},
    {{"No server: port 7770",
      "Sans serveur : port 7770",
      "Ohne Server: Port 7770",
      "Sin servidor: puerto 7770",
      "Senza server: porta 7770"}},
    {{"The party plays it; your card is kept",
      "Joué en groupe ; carte préservée",
      "Gruppenspiel; deine Karte bleibt erhalten",
      "El grupo la juega; tu tarjeta se conserva",
      "Gioco di gruppo; la tua scheda resta intatta"}},
    {{"Cannot host here",
      "Impossible d'héberger ici",
      "Hosting hier nicht möglich",
      "No se puede alojar aquí",
      "Impossibile ospitare qui"}},
    {{"Refresh", "Actualiser", "Aktualisieren", "Actualizar", "Aggiorna"}},
    {{"Asking the server...",
      "Requête au serveur...",
      "Frage Server ab...",
      "Consultando al servidor...",
      "Richiesta al server..."}},
    {{"No games are hosted: Create match hosts one",
      "Aucune partie : choisissez Créer une partie",
      "Keine Spiele: Wähle Spiel erstellen",
      "Sin partidas: elige Crear partida",
      "Nessuna partita: scegli Crea partita"}},
    {{"Hosting on ", "Hébergé sur ", "Gehostet auf ", "Alojando en ", "Ospitato su "}},
    {{"Hosting on port ",
      "Hébergé sur le port ",
      "Hosting auf Port ",
      "Alojando en el puerto ",
      "Ospitato sulla porta "}},
    {{", TCP only", ", TCP seul", ", nur TCP", ", solo TCP", ", solo TCP"}},
    {{"Not connected", "Non connecté", "Nicht verbunden", "Sin conexión", "Non connesso"}},
    {{"Joining ", "Connexion à ", "Verbinde mit ", "Conectando a ", "Connessione a "}},
    {{"Connecting...", "Connexion...", "Verbinde...", "Conectando...", "Connessione..."}},
    {{"Host unreachable",
      "Hôte inaccessible",
      "Host nicht erreichbar",
      "Anfitrión inaccesible",
      "Host irraggiungibile"}},
    {{"Server unreachable",
      "Serveur inaccessible",
      "Server nicht erreichbar",
      "Servidor inaccesible",
      "Server irraggiungibile"}},
    {{"The host went away",
      "Hôte déconnecté",
      "Host hat sich getrennt",
      "Anfitrión desconectado",
      "Host disconnesso"}},
    {{"The server went away",
      "Serveur déconnecté",
      "Server hat sich getrennt",
      "Servidor desconectado",
      "Server disconnesso"}},
    {{"Connected, saying hello...",
      "Connexion, identification...",
      "Verbunden, Anmeldung...",
      "Conectado, identificando...",
      "Connesso, identificazione..."}},
    {{"Waiting for the server...",
      "Attente du serveur...",
      "Warte auf Server...",
      "Esperando al servidor...",
      "Attesa del server..."}},
    {{"Joined ", "Connecté à ", "Verbunden mit ", "Conectado a ", "Connesso a "}},
    {{"Connected to the server",
      "Connecté au serveur",
      "Mit Server verbunden",
      "Conectado al servidor",
      "Connesso al server"}},
    {{"The party is over",
      "Le groupe est dissous",
      "Die Gruppe wurde aufgelöst",
      "El grupo se ha disuelto",
      "Il gruppo si è sciolto"}},
    {{"Leave party",
      "Quitter le groupe",
      "Gruppe verlassen",
      "Salir del grupo",
      "Esci dal gruppo"}},
    {{"Not in a party",
      "Aucun groupe rejoint",
      "In keiner Gruppe",
      "No estás en un grupo",
      "Non sei in un gruppo"}},
    {{"Direct", "Direct", "Direkt", "Directa", "Diretta"}},
    {{"Host's level",
      "Niveau de l'hôte",
      "Level des Hosts",
      "Nivel del anfitrión",
      "Livello dell'host"}},
    {{"None", "Aucun", "Keins", "Ninguno", "Nessuno"}},
    {{" (host, you)", " (hôte, vous)", " (Host, du)", " (anfitrión, tú)", " (host, tu)"}},
    {{" (you)", " (vous)", " (du)", " (tú)", " (tu)"}},
    {{"Stop hosting?",
      "Arrêter d'héberger ?",
      "Hosting beenden?",
      "¿Dejar de alojar?",
      "Terminare l'host?"}},
    {{"End the party?",
      "Dissoudre le groupe ?",
      "Gruppe auflösen?",
      "¿Disolver el grupo?",
      "Sciogliere il gruppo?"}},
    {{"No party has that code",
      "Aucun groupe avec ce code",
      "Keine Gruppe mit diesem Code",
      "Ningún grupo tiene ese código",
      "Nessun gruppo ha quel codice"}},
    {{"That party is full",
      "Ce groupe est complet",
      "Diese Gruppe ist voll",
      "Ese grupo está lleno",
      "Quel gruppo è pieno"}},
    {{"You are in a party already",
      "Vous êtes déjà dans un groupe",
      "Du bist bereits in einer Gruppe",
      "Ya estás en un grupo",
      "Sei già in un gruppo"}},
    {{"You are not in a party",
      "Vous n'êtes pas dans un groupe",
      "Du bist in keiner Gruppe",
      "No estás en un grupo",
      "Non sei in un gruppo"}},
    {{"The server refused",
      "Refus du serveur",
      "Server hat abgelehnt",
      "El servidor lo ha rechazado",
      "Il server ha rifiutato"}},
    {{"The host left: the party is over",
      "Hôte parti : groupe dissous",
      "Host gegangen: Gruppe aufgelöst",
      "El anfitrión salió: grupo disuelto",
      "Host uscito: gruppo sciolto"}},
    {{"Game starting",
      "Lancement de la partie",
      "Spiel startet",
      "Iniciando partida",
      "Avvio partita"}},
    {{"The game would not start",
      "Impossible de lancer la partie",
      "Spiel konnte nicht starten",
      "No se pudo iniciar la partida",
      "Impossibile avviare la partita"}},
    {{"The host's game would not start here",
      "Impossible de lancer la partie de l'hôte",
      "Spiel des Hosts konnte nicht starten",
      "No se pudo iniciar la partida del anfitrión",
      "Impossibile avviare la partita dell'host"}},
    {{"Only the host flies the ship to another planet",
      "Seul l'hôte peut changer de planète",
      "Nur der Host kann den Planeten wechseln",
      "Solo el anfitrión puede cambiar de planeta",
      "Solo l'host può cambiare pianeta"}},
    {{"Only the host can skip",
      "Seul l'hôte peut passer",
      "Nur der Host kann überspringen",
      "Solo el anfitrión puede saltar",
      "Solo l'host può saltare"}},
    {{"Waiting for the other players to arrive",
      "Attente des autres joueurs",
      "Warte auf die anderen Spieler",
      "Esperando a los demás jugadores",
      "Attesa degli altri giocatori"}},
    {{"Incompatible version: the host has ",
      "Version incompatible : hôte sur ",
      "Inkompatible Version: Host hat ",
      "Versión incompatible: el anfitrión usa ",
      "Versione incompatibile: l'host usa "}},
    {{"another", "une autre version", "eine andere Version", "otra versión", "un'altra versione"}},
    {{"A save", "Une sauvegarde", "Ein Spielstand", "Una partida", "Un salvataggio"}},
    {{" gold bolt", " boulon en or", " Gold-Bolt", " guitón de oro", " bolt d'oro"}},
    {{" gold bolts", " boulons en or", " Gold-Bolts", " guitones de oro", " bolt d'oro"}},
    {{"Could not listen on port ",
      "Impossible d'écouter le port ",
      "Kann nicht lauschen auf Port ",
      "No se pudo escuchar en el puerto ",
      "Impossibile ascoltare sulla porta "}},
    {{"Port {0} is in use",
      "Le port {0} est occupé",
      "Port {0} ist belegt",
      "El puerto {0} está en uso",
      "La porta {0} è occupata"}},
    {{"\x10 Select   \x12 Back",
      "\x10 Choisir   \x12 Retour",
      "\x10 Wählen   \x12 Zurück",
      "\x10 Elegir   \x12 Atrás",
      "\x10 Scegli   \x12 Indietro"}},
    {{"\x10 Select   \x12 Leave",
      "\x10 Choisir   \x12 Quitter",
      "\x10 Wählen   \x12 Verlassen",
      "\x10 Elegir   \x12 Salir",
      "\x10 Scegli   \x12 Esci"}},
    {{"Left, Right change   \x10 Join",
      "Gauche, droite : changer   \x10 Rejoindre",
      "Links, rechts ändern   \x10 Beitreten",
      "Izq., der.: cambiar   \x10 Unirse",
      "Sin., des.: cambia   \x10 Unisciti"}},
    {{"Left, Right change   \x10 Join   \x12 Back",
      "Gauche, droite   \x10 Rejoindre   \x12 Retour",
      "Links, rechts   \x10 Beitreten   \x12 Zurück",
      "Izq., der.   \x10 Unirse   \x12 Atrás",
      "Sin., des.   \x10 Unisciti   \x12 Indietro"}},
};

inline const char* translated(const char* english, Language language) {
    const auto index = static_cast<unsigned>(language);
    for (const Words& words : kWords) {
        if (std::strcmp(words.text[0], english) == 0) {
            return words.text[index < 5 ? index : 0];
        }
    }
    return english;
}

// The current UI API truncates bytes, not UTF-8 characters. Reserve an ASCII ellipsis and
// stop before a continuation byte, so a long dynamic label never loses half an accented letter.
inline std::string fit_ui_text(const std::string& value, std::size_t limit) {
    if (value.size() <= limit) {
        return value;
    }
    std::size_t end = limit >= 3 ? limit - 3 : limit;
    while (end > 0 && (static_cast<unsigned char>(value[end]) & 0xC0u) == 0x80u) {
        --end;
    }
    return value.substr(0, end) + (limit >= 3 ? "..." : "");
}

// Notices remain English in the network/session layer. Translate only known mod-owned messages
// at the display boundary, never player names, chat, server text or remote save descriptions.
inline std::string localized_notice(const std::string& notice, Language language) {
    const char* exact = translated(notice.c_str(), language);
    if (exact != notice.c_str()) {
        return exact;
    }
    for (const char* prefix :
         {"Incompatible version: the host has ", "Could not listen on port ", "Not an address: "}) {
        const std::size_t n = std::strlen(prefix);
        if (notice.compare(0, n, prefix) == 0) {
            const std::string rest = notice.substr(n);
            return std::string(translated(prefix, language))
                   + (rest == "another" ? translated("another", language) : rest);
        }
    }
    // The local host's bind error, with the numeric port intact.
    const std::string suffix = " is in use";
    if (notice.compare(0, 5, "Port ") == 0 && notice.size() > 5 + suffix.size()
        && notice.compare(notice.size() - suffix.size(), suffix.size(), suffix) == 0) {
        const std::string port = notice.substr(5, notice.size() - 5 - suffix.size());
        if (port.find_first_not_of("0123456789") == std::string::npos) {
            std::string text = translated("Port {0} is in use", language);
            text.replace(text.find("{0}"), 3, port);
            return text;
        }
    }
    return notice;
}

}  // namespace mp
