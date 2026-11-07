#pragma once

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <string>

// Messages for transport/session infrastructure only. The game can select the
// language without importing any gameplay localization or chat command system.
namespace NetworkText
{
    inline std::atomic<bool> french{false};
    inline void setFrench(bool enabled) { french.store(enabled, std::memory_order_relaxed); }
    inline bool isFrench() { return french.load(std::memory_order_relaxed); }
    inline void initializeFromSteamLanguage(const char* language)
    {
        std::string value = language == nullptr ? "" : language;
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
        { return static_cast<char>(std::tolower(c)); });
        setFrench(value == "french" || value == "fr" || value.compare(0, 3, "fr-") == 0);
    }

    enum class Id
    {
        CommonPlayerTitle,
        IntegrityMismatch,
        IntegrityProtocolMismatch,
        IntegrityTimeout,
        IntegrityUnavailable,
        MainDefaultLobbyName,
        NetworkDirectClientSocketFailed,
        NetworkDirectServerClosed,
        NetworkDirectServerPortFailed,
        NetworkDirectTimeout,
        NetworkInvalidLobbyOwner,
        NetworkListenSocketFailed,
        NetworkMessageTooLarge,
        NetworkP2PConnectionFailed,
        NetworkPollGroupFailed,
        NetworkSteamHostDisconnected,
        NetworkSteamSocketsUnavailable,
        NetworkTransportStartFailed,
        OnlineAccessRefused,
        OnlineAddressRequired,
        OnlineAdmissionRefused,
        OnlineCreateRejected,
        OnlineDefaultHost,
        OnlineDefaultLobby,
        OnlineDefaultPlayerPrefix,
        OnlineDirectDisconnected,
        OnlineIncompatibleLobby,
        OnlineIncorrectPassword,
        OnlineInvalidLobbyId,
        OnlineInvalidPort,
        OnlineJoinRejected,
        OnlineKicked,
        OnlineKickedReason,
        OnlineLobbyCreateFailed,
        OnlineLobbyGone,
        OnlineLobbyNameCheckFailed,
        OnlineLobbyNameTaken,
        OnlineLobbyRetrieveFailed,
        OnlineLobbySearchFailed,
        OnlineMigrationTimeout,
        OnlineMigrationUnavailable,
        OnlineNoOwner,
        OnlinePrivatePasswordRequired,
        OnlineSearchRejected,
        OnlineSteamPlayer,
        OnlineSteamUnavailable,
        OnlineUnknownError,
    };

    inline const char* get(Id id)
    {
        switch (id)
        {
        case Id::CommonPlayerTitle: return french ? "Joueur" : "Player";
        case Id::IntegrityMismatch: return french ? "Vos ressources diffèrent de celles de l’hôte. Vérifiez les fichiers du jeu et utilisez la même version." : "Your game resources differ from the host. Verify your game files and use the same game version.";
        case Id::IntegrityProtocolMismatch: return french ? "Cette version du jeu utilise un protocole de vérification des ressources incompatible. Mettez le jeu à jour et réessayez." : "This game version uses an incompatible resource verification protocol. Update the game and try again.";
        case Id::IntegrityTimeout: return french ? "Le délai de vérification des ressources est dépassé. Essayez de rejoindre à nouveau." : "Resource verification timed out. Please try joining again.";
        case Id::IntegrityUnavailable: return french ? "Les ressources du projet n’ont pas pu être vérifiées. Vérifiez les fichiers et réessayez." : "The project resources could not be verified. Check your files and try again.";
        case Id::MainDefaultLobbyName: return french ? "Salon ENDURANCE" : "ENDURANCE Lobby";
        case Id::NetworkDirectClientSocketFailed: return french ? "Impossible de créer le socket client IP direct" : "Failed to create the direct-IP client socket";
        case Id::NetworkDirectServerClosed: return french ? "Le serveur IP direct a fermé la connexion" : "The direct-IP server closed the connection";
        case Id::NetworkDirectServerPortFailed: return french ? "Impossible d’ouvrir le port du serveur IP direct" : "Failed to open the direct-IP server port";
        case Id::NetworkDirectTimeout: return french ? "La connexion IP directe a expiré" : "Direct-IP connection timed out";
        case Id::NetworkInvalidLobbyOwner: return french ? "L’ID du propriétaire du salon Steam n’est pas valide" : "The Steam lobby owner ID is invalid";
        case Id::NetworkListenSocketFailed: return french ? "Impossible de créer le socket d’écoute P2P Steam" : "Failed to create the Steam P2P listen socket";
        case Id::NetworkMessageTooLarge: return french ? "Un message réseau a dépassé la limite de sécurité" : "A network message exceeded the safety limit";
        case Id::NetworkP2PConnectionFailed: return french ? "Impossible de créer la connexion P2P Steam" : "Failed to create the Steam P2P connection";
        case Id::NetworkPollGroupFailed: return french ? "Impossible de créer le groupe d’écoute P2P Steam" : "Failed to create the Steam P2P poll group";
        case Id::NetworkSteamHostDisconnected: return french ? "L’hôte Steam s’est déconnecté" : "Steam host disconnected";
        case Id::NetworkSteamSocketsUnavailable: return french ? "SteamNetworkingSockets n’est pas disponible" : "SteamNetworkingSockets is unavailable";
        case Id::NetworkTransportStartFailed: return french ? "Impossible de démarrer le transport réseau" : "Unable to start the network transport";
        case Id::OnlineAccessRefused: return french ? "Steam a refusé l’accès au salon" : "Steam refused access to the lobby";
        case Id::OnlineAddressRequired: return french ? "Une adresse de serveur est requise" : "A server address is required";
        case Id::OnlineAdmissionRefused: return french ? "L’hôte a refusé la connexion. Vérifiez le mot de passe du lobby et les places disponibles." : "The host refused the connection. Check the lobby password and available places.";
        case Id::OnlineCreateRejected: return french ? "Steam a refusé la demande de création du salon" : "Steam rejected the lobby creation request";
        case Id::OnlineDefaultHost: return french ? "Hôte" : "Host";
        case Id::OnlineDefaultLobby: return french ? "Salon Steam" : "Steam lobby";
        case Id::OnlineDefaultPlayerPrefix: return french ? "Joueur " : "Player ";
        case Id::OnlineDirectDisconnected: return french ? "L’hôte en IP directe s’est déconnecté" : "The direct-IP host disconnected";
        case Id::OnlineIncompatibleLobby: return french ? "Ce lobby utilise une version incompatible du jeu. Mettez le jeu à jour et réessayez." : "This lobby uses an incompatible game version. Update the game and try again.";
        case Id::OnlineIncorrectPassword: return french ? "Mot de passe du salon incorrect" : "Incorrect lobby password";
        case Id::OnlineInvalidLobbyId: return french ? "L’ID du salon Steam n’est pas valide" : "The Steam lobby ID is invalid";
        case Id::OnlineInvalidPort: return french ? "Le port IP direct doit être compris entre 1 et 65535" : "The direct-IP port must be between 1 and 65535";
        case Id::OnlineJoinRejected: return french ? "Steam a refusé la demande de connexion au salon" : "Steam rejected the lobby join request";
        case Id::OnlineKicked: return french ? "Vous avez été expulsé de la session." : "You were removed from the session.";
        case Id::OnlineKickedReason: return french ? "Vous avez été expulsé de la session. Raison : {0}" : "You were removed from the session. Reason: {0}";
        case Id::OnlineLobbyCreateFailed: return french ? "Steam n’a pas pu créer le salon" : "Steam failed to create the lobby";
        case Id::OnlineLobbyGone: return french ? "Le salon Steam n’existe plus" : "The Steam lobby no longer exists";
        case Id::OnlineLobbyNameCheckFailed: return french ? "Steam n’a pas pu vérifier le nom du salon. Réessayez." : "Steam could not check the lobby name. Please try again.";
        case Id::OnlineLobbyNameTaken: return french ? "Un salon porte déjà ce nom. Choisissez un autre nom." : "A lobby with this name already exists. Please choose another name.";
        case Id::OnlineLobbyRetrieveFailed: return french ? "Steam n’a pas pu récupérer ce salon" : "Steam could not retrieve this lobby";
        case Id::OnlineLobbySearchFailed: return french ? "La recherche de salons Steam a échoué" : "Steam lobby search failed";
        case Id::OnlineMigrationTimeout: return french ? "La migration de l’hôte Steam a expiré" : "Steam host migration timed out";
        case Id::OnlineMigrationUnavailable: return french ? "Le salon Steam est devenu indisponible pendant la migration de l’hôte" : "The Steam lobby became unavailable during host migration";
        case Id::OnlineNoOwner: return french ? "Le salon Steam n’a plus de propriétaire" : "The Steam lobby no longer has an owner";
        case Id::OnlinePrivatePasswordRequired: return french ? "Un salon privé nécessite un mot de passe" : "A private lobby requires a password";
        case Id::OnlineSearchRejected: return french ? "Steam a refusé la recherche de salons" : "Steam rejected the lobby search request";
        case Id::OnlineSteamPlayer: return french ? "Joueur Steam" : "Steam player";
        case Id::OnlineSteamUnavailable: return french ? "Steam n’est pas disponible" : "Steam is not available";
        case Id::OnlineUnknownError: return french ? "Erreur de session en ligne inconnue" : "Unknown online-session error";
        }
        return "";
    }

    // Bound remote display text without truncating a UTF-8 sequence. Control
    // characters are removed and whitespace is collapsed before display.
    inline std::string clean(const std::string& text, std::size_t limit)
    {
        std::string result;
        result.reserve((std::min)(text.size(), limit));
        bool previousWasSpace = true;
        for (std::size_t i = 0; i < text.size();)
        {
            const unsigned char first = static_cast<unsigned char>(text[i]);
            if (first == ' ' || (first >= '\t' && first <= '\r'))
            {
                if (!previousWasSpace && result.size() < limit) result.push_back(' ');
                previousWasSpace = true;
                ++i;
                continue;
            }
            if (first < 0x20u || first == 0x7fu) { ++i; continue; }
            std::size_t bytes = 1;
            if ((first & 0xe0u) == 0xc0u) bytes = 2;
            else if ((first & 0xf0u) == 0xe0u) bytes = 3;
            else if ((first & 0xf8u) == 0xf0u) bytes = 4;
            else if ((first & 0x80u) != 0u) { ++i; continue; }
            if (i + bytes > text.size() || result.size() + bytes > limit) break;
            bool valid = true;
            for (std::size_t b = 1; b < bytes; ++b)
                if ((static_cast<unsigned char>(text[i + b]) & 0xc0u) != 0x80u) valid = false;
            if (!valid) { ++i; continue; }
            result.append(text, i, bytes);
            i += bytes;
            previousWasSpace = false;
        }
        if (!result.empty() && result.back() == ' ') result.pop_back();
        return result;
    }

    // Substitute a single argument verbatim, never as a printf format string.
    inline std::string format(const char* pattern, const std::string& argument)
    {
        std::string output;
        const std::string text = pattern == nullptr ? "" : pattern;
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            if (text.compare(i, 3, "{0}") == 0) { output += argument; i += 2; }
            else output += text[i];
        }
        return output;
    }
}
