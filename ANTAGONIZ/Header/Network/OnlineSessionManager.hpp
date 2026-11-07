#ifndef __ONLINE_SESSION_MANAGER__
#define __ONLINE_SESSION_MANAGER__

#include "NetworkManager.hpp"
#include "ResourceIntegritySession.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

enum class OnlineSessionKind
{
	NONE,
	DIRECT_IP,
	STEAM_LOBBY
};

enum class OnlineSessionState
{
	IDLE,
	CREATING,
	SEARCHING,
	JOINING,
	IN_LOBBY,
	CONNECTING,
	MIGRATING_HOST,
	ERROR_STATUS
};

struct SteamLobbyEntry
{
	std::uint64_t id = 0;
	std::string name;
	int players = 0;
	int capacity = 0;
	bool passwordProtected = false;
	// Empty for older lobbies or an unrecognized language tag.
	std::string language;
};

struct OnlinePlayerInfo
{
	std::uint64_t steamId = 0;
	int networkId = -1;
	std::string name;
	int pingMs = -1;
	bool local = false;
	bool host = false;
};

struct OnlineHostDeparture
{
	std::uint64_t steamId = 0;
};

class OnlineSessionManager
{
public:
	~OnlineSessionManager() { release(); }

	bool initialize();
	void update();
	void release();

	bool isSteamAvailable() const { return m_steamAvailable; }
	const std::string& getSteamPlayerName() const { return m_steamPlayerName; }
	std::string getDefaultSteamLobbyName() const;
	OnlineSessionKind getKind() const { return m_kind; }
	OnlineSessionState getState() const { return m_state; }
	bool isHost() const { return m_host; }
	bool isMigratingHost() const { return m_migratingHost; }
	bool hasSession() const { return m_kind != OnlineSessionKind::NONE; }
	bool isNetworkReady() const;
	int getPlayerCount() const;
	int getMinimumPlayers() const { return m_minimumPlayers; }
	void setMinimumPlayers(int count);
	std::uint64_t getLobbyId() const { return m_lobbyId.ConvertToUint64(); }
	const std::vector<SteamLobbyEntry>& getLobbyList() const { return m_lobbies; }
	std::vector<OnlinePlayerInfo> getPlayers() const;
	std::vector<std::string> getMemberNames() const;
	const std::string& getLocalPlayerName() const { return m_localPlayerName; }
	const std::string& getDirectPlayerName() const { return m_directPlayerName; }
	const std::string& getLastError() const { return m_lastError; }
	const NetworkManager* getNetworkManager() const { return m_network.get(); }
	NetworkManager* getNetworkManager() { return m_network.get(); }

	bool hostDirect(int port);
	bool joinDirect(const std::string& address, int port);
	bool createSteamLobby(const std::string& lobbyName, bool privateLobby, const std::string& password, int capacity, const std::string& language = "");
	bool requestSteamLobbyList();
	bool joinSteamLobby(std::uint64_t lobbyId, const std::string& password);
	// Direct-IP preference only; Steam sessions always use their account persona.
	void setLocalPlayerName(const std::string& name);
	void leaveSession();
	bool kickClient(int networkId, const std::string& reason);

	// Optional: after creating/joining a session, opt in before loading project
	// assets on every participant.
	// Without a project-specific collector, no asset gate is enabled.
	// Once begun, failure/cancellation keeps the gate closed until leaveSession().
	void beginResourceIntegrityCheck();
	void completeResourceIntegrityCheck(const std::string& digest, const std::string& error);
	void cancelResourceIntegrityCheck();
	bool isResourceIntegrityAccepted() const;

	bool canStartGame(bool allowDebugSolo) const;
	bool startGame(bool allowDebugSolo);
	void startDebugSolo();
	bool consumeLaunchRequest();
	void requestLobbyInvite(std::uint64_t lobbyId);
	std::uint64_t consumeLobbyInvite();
	std::uint64_t consumeLobbyPasswordRequest();
	bool consumeReturnToMainMenuRequest();
	bool consumeDepartedSteamHost(OnlineHostDeparture& departure);

private:
	void useSteamPlayerName();
	void configureNetworkCallbacks();
	void updateResourceIntegrity();
	bool finishResourceIntegrityFailure();
	bool finishPendingKick();
	void broadcastDirectPlayerCount();
	void sendDirectPlayerNames(int targetClientId = -1);
	void startSteamHostNetwork();
	void startSteamClientNetwork(CSteamID host);
	void observeSteamLobbyOwner();
	void beginSteamHostMigration(CSteamID previousHost);
	void updateSteamHostMigration();
	void finishSteamHostMigration(CSteamID newHost);
	void failSteamHostMigration(const std::string& message);
	void rememberDepartedSteamHost(CSteamID departedHost);
	CSteamID selectSteamMigrationOwner() const;
	void transferSteamLobbyOwnershipBeforeLeave();
	void setError(const std::string& message);
	void resetSessionState();
	std::string passwordHash(const std::string& password, CSteamID lobby) const;
	bool validatePendingLobbyPassword(CSteamID lobby);
	void beginJoinLobby(CSteamID lobby);
	void startQueuedLobbyOperation();
	void startLobbyNameCheck();
	bool completeLobbyOperation(CSteamID joinedLobby, bool success);
	bool validateLobbyCompatibility(CSteamID lobby);
	bool authorizeSteamPeer(std::uint64_t steamId, const std::string& password) const;
	void requestInitialScene();

	void onLobbyCreated(LobbyCreated_t* result, bool ioFailure);
	void onLobbyListReceived(LobbyMatchList_t* result, bool ioFailure);
	void onLobbyEntered(LobbyEnter_t* result, bool ioFailure);
	void onLobbyDataUpdated(LobbyDataUpdate_t* event);
	void onLobbyMembersChanged(LobbyChatUpdate_t* event);
	void onLobbyJoinRequested(GameLobbyJoinRequested_t* event);

private:
	bool m_initialized = false;
	bool m_steamAvailable = false;
	std::string m_steamPlayerName;
	OnlineSessionKind m_kind = OnlineSessionKind::NONE;
	OnlineSessionState m_state = OnlineSessionState::IDLE;
	bool m_host = false;
	bool m_launchRequested = false;
	bool m_sceneLaunched = false;
	bool m_returnToMainMenuRequested = false;
	bool m_hostTransportLost = false;
	bool m_kickPending = false;
	std::string m_kickReason;
	bool m_migratingHost = false;
	bool m_departedHostPending = false;
	std::uint64_t m_invitedLobbyId = 0;
	std::uint64_t m_lobbyPasswordRequestId = 0;
	int m_minimumPlayers = 2;
	int m_directPlayerCount = 1;
	std::string m_localPlayerName;
	std::string m_directPlayerName;
	std::map<int, std::string> m_directPlayerNames;
	bool m_directNameSent = false;
	std::string m_lastError;
	std::unique_ptr<NetworkManager> m_network;
	ResourceIntegritySession m_resourceIntegritySession;
	bool m_resourceIntegrityRequired = false;

	CSteamID m_lobbyId;
	CSteamID m_steamNetworkHost;
	CSteamID m_migrationPreviousHost;
	CSteamID m_migrationTargetHost;
	CSteamID m_expectedDepartingHost;
	OnlineHostDeparture m_departedHost;
	std::chrono::steady_clock::time_point m_migrationStarted;
	std::chrono::steady_clock::time_point m_nextMigrationRetry;
	std::vector<SteamLobbyEntry> m_lobbies;
	std::string m_pendingLobbyName;
	std::string m_pendingLobbyLanguage;
	std::string m_pendingPassword;
	bool m_pendingPrivate = false;
	int m_pendingCapacity = 8;
	CSteamID m_pendingJoinLobby;

	// Keep cancelled operations observed until completion. A new operation waits
	// so a late LeaveLobby cannot remove a newer join of the same lobby.
	enum class PendingLobbyOperation { NONE, CHECK_CREATE_NAME, CREATE, JOIN };
	PendingLobbyOperation m_queuedLobbyOperation = PendingLobbyOperation::NONE;
	CSteamID m_queuedJoinLobby;
	bool m_lobbyOperationActive = false;
	std::uint64_t m_requestGeneration = 0;
	std::uint64_t m_activeRequestGeneration = 0;

	// Browser refreshes and name checks share Steam's single search slot.
	enum class LobbySearchPurpose { NONE, BROWSER, CHECK_CREATE_NAME };
	LobbySearchPurpose m_lobbySearchPurpose = LobbySearchPurpose::NONE;
	std::uint64_t m_lobbySearchGeneration = 0;

	CCallResult<OnlineSessionManager, LobbyCreated_t> m_lobbyCreatedResult;
	CCallResult<OnlineSessionManager, LobbyMatchList_t> m_lobbyListResult;
	CCallResult<OnlineSessionManager, LobbyEnter_t> m_lobbyEnterResult;
	CCallbackManual<OnlineSessionManager, LobbyDataUpdate_t> m_lobbyDataCallback;
	CCallbackManual<OnlineSessionManager, LobbyChatUpdate_t> m_lobbyMembersCallback;
	CCallbackManual<OnlineSessionManager, GameLobbyJoinRequested_t> m_lobbyInviteCallback;
};

#endif //!__ONLINE_SESSION_MANAGER__
