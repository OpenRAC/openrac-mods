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

// Going Commando's: a byte (0x001A7BB4 on USA v1.01, the menus' action type 9 sets it), the same
// indexes as Ratchet & Clank's (the port's rac2_version.h maps it for PAL and USA v2.00).
inline std::uint32_t rac2_language_address(std::string_view disc) {
    return disc == "ntsc" ? 0x001A7BB4u : disc == "pal" ? 0x001A7C3Cu : disc == "ntsc2" ? 0x001A7BBCu : 0;
}

struct Words {
    const char* text[5];
};

inline constexpr Words kWords[] = {
    {{" units", " unités", " Einheiten", " unidades", " unità"}},
    {{"Team tools",
      "Outils équipe",
      "Team-Werkzeuge",
      "Equipo",
      "Strumenti squadra"}},
    {{"Co-op rescue",
      "Sauvetage coop",
      "Koop-Wiederbeleben",
      "Reanimación coop",
      "Rianimazione coop"}},
    {{"Off", "Non", "Aus", "No", "No"}},
    {{"On", "Oui", "An", "Sí", "Sì"}},
    {{"Time to rescue",
      "Délai de secours",
      "Zeitfenster für Hilfe",
      "Plazo de rescate",
      "Tempo per il soccorso"}},
    {{"Host rally point",
      "Point de ralliement",
      "Host-Treffpunkt",
      "Punto del anfitrión",
      "Punto di ritrovo host"}},
    {{"Mark host position",
      "Marquer la position hôte",
      "Host-Position markieren",
      "Marcar posición del anfitrión",
      "Segna posizione host"}},
    {{"Clear rally point",
      "Effacer le point",
      "Treffpunkt löschen",
      "Borrar punto",
      "Cancella ritrovo"}},
    {{"Give up", "Abandonner", "Aufgeben", "Rendirse", "Arrenditi"}},
    {{"No rally point",
      "Aucun point",
      "Kein Treffpunkt",
      "Sin punto de reunión",
      "Nessun ritrovo"}},
    {{"Needs rescue", "À secourir", "Braucht Hilfe", "Necesita ayuda", "Serve aiuto"}},
    {{"Loading", "Chargement", "Lädt", "Cargando", "Caricamento"}},
    {{"Hold L3 + R3", "Tenir L3 + R3", "L3 + R3 halten", "Pulsa L3 + R3", "Tieni L3 + R3"}},
    {{"Awaiting rescue ",
      "En attente d'aide : ",
      "Warte auf Hilfe: ",
      "Esperando rescate: ",
      "In attesa di aiuto: "}},
    {{"Rally point: ", "Ralliement : ", "Treffpunkt: ", "Punto de reunión: ", "Ritrovo: "}},
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
      "Via adresse",
      "Mit Adresse beitreten",
      "Unirse por dirección",
      "Unisciti con indirizzo"}},
    {{"No server: port ",
      "Sans serveur : port ",
      "Ohne Server: Port ",
      "Sin servidor: puerto ",
      "Senza server: porta "}},
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
    {{"No server", "Sans serveur", "Ohne Server", "Sin servidor", "Senza server"}},
    {{"Start", "Démarrer", "Starten", "Empezar", "Avvia"}},
    {{"Join game", "Rejoindre", "Beitreten", "Unirse", "Entra"}},
    {{"Not ready", "Pas prêt", "Nicht bereit", "No listo", "Non pronto"}},
    {{"Ready", "Prêt", "Bereit", "Listo", "Pronto"}},
    {{"Leave", "Quitter", "Verlassen", "Salir", "Esci"}},
    {{"Party ", "Groupe ", "Gruppe ", "Grupo ", "Gruppo "}},
    {{"Party at ", "Groupe sur ", "Gruppe auf ", "Grupo en ", "Gruppo su "}},
    {{"Playing: ", "Partie : ", "Spiel: ", "Partida: ", "Partita: "}},
    {{"Receiving...", "Réception...", "Empfange...", "Recibiendo...", "Ricezione..."}},
    {{" (host)", " (hôte)", " (Host)", " (anfitrión)", " (host)"}},
    {{" (ready)", " (prêt)", " (bereit)", " (listo)", " (pronto)"}},
    {{" (not ready)", " (pas prêt)", " (nicht bereit)", " (no listo)", " (non pronto)"}},
    {{" - you", " - vous", " - du", " - tú", " - tu"}},
    {{" players", " joueurs", " Spieler", " jugadores", " giocatori"}},
    {{", waiting", ", attente", ", wartet", ", espera", ", attesa"}},
    {{"port ", "port ", "Port ", "puerto ", "porta "}},
    {{"Leave party?", "Quitter ?", "Verlassen?", "¿Salir?", "Uscire?"}},
    {{"Save ", "Sauvegarde ", "Spielstand ", "Partida ", "Salvataggio "}},
    {{"empty", "vide", "leer", "vacía", "vuoto"}},
    {{"Host which save?",
      "Quelle sauvegarde ?",
      "Welcher Spielstand?",
      "¿Qué partida?",
      "Quale salvataggio?"}},
    {{"Play which save?",
      "Quelle sauvegarde ?",
      "Welcher Spielstand?",
      "¿Qué partida?",
      "Quale salvataggio?"}},
    {{"New game", "Nouvelle partie", "Neues Spiel", "Nueva partida", "Nuova partita"}},
    {{"Port 7770", "Port 7770", "Port 7770", "Puerto 7770", "Porta 7770"}},
    {{"Plays a copy", "Joue une copie", "Spielt Kopie", "Juega copia", "Gioca copia"}},
    {{"Can't host", "Impossible", "Nicht möglich", "No se puede", "Impossibile"}},
    {{"Refresh", "Actualiser", "Aktualisieren", "Actualizar", "Aggiorna"}},
    {{"Asking...", "Requête...", "Frage an...", "Consultando...", "Richiesta..."}},
    {{"No games", "Aucune partie", "Keine Spiele", "Sin partidas", "Nessuna partita"}},
        {{"Hosting", "Hébergé", "Hosting", "Alojando", "Host"}},
    {{" (TCP)", " (TCP)", " (TCP)", " (TCP)", " (TCP)"}},
    {{"Not connected", "Non connecté", "Nicht verbunden", "Sin conexión", "Non connesso"}},
    {{"Joining...", "Connexion...", "Beitritt...", "Uniéndose...", "Accesso..."}},
    {{"Connecting...", "Connexion...", "Verbinde...", "Conectando...", "Connessione..."}},
    {{"Host offline", "Hôte absent", "Host offline", "Sin anfitrión", "Host offline"}},
    {{"Server offline", "Serveur absent", "Server offline", "Sin servidor", "Server offline"}},
    {{"Server lost", "Serveur perdu", "Server weg", "Servidor perdido", "Server perso"}},
    {{"Waiting...", "Attente...", "Warte...", "Esperando...", "Attesa..."}},
    {{"Joined", "Connecté", "Verbunden", "Conectado", "Connesso"}},
    {{"Connected", "Connecté", "Verbunden", "Conectado", "Connesso"}},
    {{"Party over", "Groupe dissous", "Gruppe beendet", "Grupo disuelto", "Gruppo sciolto"}},
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
    {{"Direct party", "Groupe direct", "Direktgruppe", "Grupo directo", "Gruppo diretto"}},
    {{"Direct", "Direct", "Direkt", "Directa", "Diretta"}},
    {{"Host's level",
      "Niveau de l'hôte",
      "Level des Hosts",
      "Nivel del anfitrión",
      "Livello dell'host"}},
    {{"None", "Aucun", "Keins", "Ninguno", "Nessuno"}},
    {{" (host, you)", " (hôte, vous)", " (Host, du)", " (anfitrión, tú)", " (host, tu)"}},
    {{" (you)", " (vous)", " (du)", " (tú)", " (tu)"}},
    {{"Stop hosting?", "Arrêter ?", "Beenden?", "¿Parar?", "Fermare?"}},
    {{"End the party?", "Dissoudre ?", "Auflösen?", "¿Disolver?", "Sciogliere?"}},
    {{"No such code", "Code inconnu", "Code unbekannt", "Código erróneo", "Codice errato"}},
    {{"Party full", "Groupe complet", "Gruppe voll", "Grupo lleno", "Gruppo pieno"}},
    {{"For Ratchet & Clank", "Pour Ratchet & Clank", "Für Ratchet & Clank", "Para Ratchet & Clank", "Per Ratchet & Clank"}},
    {{"For Ratchet & Clank 2",
      "Pour Ratchet & Clank 2",
      "Für Ratchet & Clank 2",
      "Para Ratchet & Clank 2",
      "Per Ratchet & Clank 2"}},
    {{"Already in party", "Déjà en groupe", "Schon in Gruppe", "Ya en un grupo", "Già in gruppo"}},
    {{"No party", "Aucun groupe", "Keine Gruppe", "Sin grupo", "Nessun gruppo"}},
    {{"Refused", "Refusé", "Abgelehnt", "Rechazado", "Rifiutato"}},
    {{"Host only", "Hôte requis", "Nur Host", "Solo anfitrión", "Solo host"}},
    {{"Host left", "Hôte parti", "Host weg", "Host se fue", "Host uscito"}},
    {{"Starting", "Démarrage", "Startet", "Iniciando", "Avvio"}},
    {{"Can't start", "Échec", "Start misslang", "No inicia", "Avvio fallito"}},
    {{"Can't start here", "Échec ici", "Hier nicht möglich", "No inicia aquí", "Non parte qui"}},
    {{"Only the host can fly",
      "Seul l'hôte pilote",
      "Nur der Host fliegt",
      "Solo el host vuela",
      "Solo l'host vola"}},
    {{"Only the host can skip",
      "Seul l'hôte peut passer",
      "Nur der Host kann überspringen",
      "Solo el anfitrión puede saltar",
      "Solo l'host può saltare"}},
    {{"Waiting for players",
      "Attente des joueurs",
      "Warte auf Spieler",
      "Esperando jugadores",
      "Attesa giocatori"}},
    {{"Host has ", "Hôte : ", "Host: ", "Host: ", "Host: "}},
    {{"another", "autre", "andere", "otra", "diversa"}},
    {{"A save", "Une sauvegarde", "Ein Spielstand", "Una partida", "Un salvataggio"}},
    {{" gold bolt", " boulon en or", " Gold-Bolt", " guitón de oro", " bolt d'oro"}},
    {{" gold bolts", " boulons en or", " Gold-Bolts", " guitones de oro", " bolt d'oro"}},
    {{"Bad address", "Adresse fausse", "Falsche Adresse", "Dirección mala", "Indirizzo errato"}},
    {{"Port {0} busy",
      "Port {0} occupé",
      "Port {0} belegt",
      "Pto. {0} usado",
      "Porta {0} in uso"}},
    {{"Port {0} failed",
      "Port {0} échoué",
      "Port {0} Fehler",
      "Puerto {0} falló",
      "Porta {0} KO"}},
    {{" (differs)", " (différent)", " (anders)", " (distinto)", " (diverso)"}},
    {{" (checking)", " (vérif.)", " (prüft)", " (revisando)", " (verifica)"}},
    {{"(differs): mods", "(différent) : mods", "(anders): Mods", "(distinto): mods", "(diverso): mod"}},
    {{"Back", "Retour", "Zurück", "Atrás", "Indietro"}},
    {{"Why can't I join?", "Pourquoi ?", "Warum nicht?", "¿Por qué no?", "Perché no?"}},
    {{"Host too old", "Hôte trop ancien", "Host zu alt", "Host muy viejo", "Host obsoleto"}},
    {{"Checking mods...", "Vérification...", "Prüfe Mods...", "Revisando mods...", "Verifica mod..."}},
    {{"Can't join", "Accès refusé", "Kein Beitritt", "No puedes unirte", "Accesso negato"}},
    {{"Can't play", "Jeu impossible", "Kein Spiel", "No puedes jugar", "Gioco negato"}},
    {{"Remove ", "Retirer ", "Entfernen: ", "Quitar a ", "Rimuovi "}},
    {{"Remove a player", "Retirer un joueur", "Spieler entfernen", "Quitar jugador", "Rimuovi giocatore"}},
    {{"Continue as host", "Reprendre en hôte", "Als Host weiter", "Seguir de host", "Continua da host"}},
    {{"Past parties", "Anciens groupes", "Frühere Gruppen", "Grupos previos", "Gruppi passati"}},
    {{"Own copies", "Chacun sa copie", "Eigene Kopien", "Copias propias", "Copie proprie"}},
    {{"Press Start when ready", "Start si prêt", "Start, wenn bereit", "Start al estar listo", "Start se pronto"}},
    {{"Stand by a fallen player", "Près d'un joueur à terre", "Neben Gefallenem stehen", "Junto a un caído", "Vicino a un caduto"}},
    {{"Revive rule: on", "Secours : actif", "Wiederbeleben: an", "Reanimar: sí", "Rianimare: sì"}},
    {{"\x10 Select  \x12 Back",
      "\x10 Choix  \x12 Retour",
      "\x10 Wahl  \x12 Zurück",
      "\x10 Elegir  \x12 Atrás",
      "\x10 OK  \x12 Indietro"}},
    {{"\x10 Select  \x12 Leave",
      "\x10 Choix  \x12 Quitter",
      "\x10 Wahl  \x12 Zurück",
      "\x10 Elegir  \x12 Salir",
      "\x10 Scegli  \x12 Esci"}},
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

// The most characters a page's title, list line and footer line may carry (the game's panels are
// narrower than the port's byte limit: tests/words_test.cpp measures every string against them).
// Dynamic text (names, levels, addresses) is cut to these with an ASCII ellipsis.
inline constexpr std::size_t kTitleMax = 20;
inline constexpr std::size_t kItemMax = 33;
inline constexpr std::size_t kFooterLineMax = 20;

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
    for (const char* prefix : {"Host has "}) {
        const std::size_t n = std::strlen(prefix);
        if (notice.compare(0, n, prefix) == 0) {
            const std::string rest = notice.substr(n);
            return std::string(translated(prefix, language))
                   + (rest == "another" ? translated("another", language) : rest);
        }
    }
    // The local host's bind errors, with the numeric port intact.
    for (const char* tail : {" busy", " failed"}) {
        const std::string suffix = tail;
        if (notice.compare(0, 5, "Port ") == 0 && notice.size() > 5 + suffix.size()
            && notice.compare(notice.size() - suffix.size(), suffix.size(), suffix) == 0) {
            const std::string port = notice.substr(5, notice.size() - 5 - suffix.size());
            if (port.find_first_not_of("0123456789") == std::string::npos) {
                std::string text = translated(suffix == " busy" ? "Port {0} busy" : "Port {0} failed", language);
                text.replace(text.find("{0}"), 3, port);
                return text;
            }
        }
    }
    return notice;
}

}  // namespace mp
