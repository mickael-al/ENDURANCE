#include "OnlineSessionManager.hpp"

#include "Debug.hpp"
#include "NetworkText.hpp"
#include "LobbyLanguage.hpp"
using namespace Ge;

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace
{
	constexpr const char* LobbyGameKey = "endurance_game";
	constexpr const char* LobbyGameValue = "endurance";
	constexpr const char* LobbyVersionKey = "endurance_version";
	constexpr const char* LobbyVersionValue = "1";
	constexpr const char* LobbyNameKey = "name";
	constexpr const char* LobbyLanguageKey = "endurance_language";
	constexpr const char* LobbyPrivateKey = "private";
	constexpr const char* LobbyPasswordRequiredKey = "password_required";
	constexpr const char* LobbyPasswordHashKey = "password_hash";
	constexpr const char* LobbyStateKey = "state";
	constexpr const char* LobbyStateWaiting = "waiting";
	constexpr const char* LobbyStateStarting = "starting";
	constexpr std::size_t MaximumLobbyNameBytes = 63;
	constexpr std::size_t MaximumDirectPlayerNameBytes = 32;
	constexpr const char* SteamLobbyNameSuffix = "'s Lobby";

	std::string trimLobbyName(const std::string& name)
	{
		const auto space = [](unsigned char value) { return value == ' ' || (value >= '\t' && value <= '\r'); };
		const auto first = std::find_if_not(name.begin(), name.end(), space);
		const auto last = std::find_if_not(name.rbegin(), name.rend(), space).base();
		return first < last ? std::string(first, last) : std::string();
	}

	std::string limitLobbyName(const std::string& name, std::size_t maximum)
	{
		if (name.size() <= maximum) return name;
		std::size_t end = maximum;
		// Never leave a partial UTF-8 code point in a transmitted name.
		while (end > 0 && (static_cast<unsigned char>(name[end]) & 0xc0) == 0x80) --end;
		return name.substr(0, end);
	}

	std::uint64_t integrityMilliseconds()
	{
		return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count());
	}

	const char* integrityFailureText(ResourceIntegritySession::Failure failure)
	{
		switch (failure)
		{
		case ResourceIntegritySession::Failure::Mismatch: return NetworkText::get(NetworkText::Id::IntegrityMismatch);
		case ResourceIntegritySession::Failure::Timeout: return NetworkText::get(NetworkText::Id::IntegrityTimeout);
		case ResourceIntegritySession::Failure::ProtocolMismatch: return NetworkText::get(NetworkText::Id::IntegrityProtocolMismatch);
		default: return NetworkText::get(NetworkText::Id::IntegrityUnavailable);
		}
	}

	constexpr std::chrono::seconds HostMigrationTimeout(10);
	constexpr std::chrono::milliseconds HostMigrationRetryDelay(500);
}

bool OnlineSessionManager::initialize()
{
	if (m_initialized)
	{
		return true;
	}
	m_initialized = true;
	m_state = OnlineSessionState::IDLE;

	m_steamAvailable = SteamAPI_Init();
	NetworkText::initializeFromSteamLanguage(
		m_steamAvailable && SteamApps() != nullptr
			? SteamApps()->GetCurrentGameLanguage()
			: nullptr);
	if (m_localPlayerName.empty())
	{
		m_localPlayerName = NetworkText::get(NetworkText::Id::CommonPlayerTitle);
	}
	if (!m_steamAvailable)
	{
		if (m_directPlayerName.empty()) setLocalPlayerName(m_localPlayerName);
		Debug::Warn("Steamworks is unavailable. Direct-IP multiplayer remains enabled.");
		return true;
	}

	m_lobbyDataCallback.Register(this, &OnlineSessionManager::onLobbyDataUpdated);
	m_lobbyMembersCallback.Register(this, &OnlineSessionManager::onLobbyMembersChanged);
	m_lobbyInviteCallback.Register(this, &OnlineSessionManager::onLobbyJoinRequested);
	if (SteamNetworkingUtils() != nullptr)
	{
		SteamNetworkingUtils()->InitRelayNetworkAccess();
	}
	useSteamPlayerName();
	if (m_directPlayerName.empty()) setLocalPlayerName(m_localPlayerName);
	Debug::Info("Steamworks initialized for %s", m_steamPlayerName.c_str());
	return true;
}

void OnlineSessionManager::update()
{
	if (!m_initialized)
	{
		return;
	}

	if (m_steamAvailable)
	{
		SteamAPI_RunCallbacks();
		startQueuedLobbyOperation();
	}

	if (finishPendingKick() || finishResourceIntegrityFailure()) return;
	if (m_kind == OnlineSessionKind::STEAM_LOBBY && m_lobbyId.IsValid())
	{
		observeSteamLobbyOwner();
	}
	if (m_migratingHost)
	{
		updateSteamHostMigration();
		return;
	}

	if (m_network == nullptr)
	{
		return;
	}

	NetworkManager* network = m_network.get();
	network->update();
	updateResourceIntegrity();
	if (finishPendingKick() || finishResourceIntegrityFailure()) return;
	if (network->wasAdmissionRejected())
	{
		failSteamHostMigration(NetworkText::get(NetworkText::Id::OnlineAdmissionRefused));
		return;
	}
	if (m_hostTransportLost)
	{
		m_hostTransportLost = false;
		if (m_kind == OnlineSessionKind::DIRECT_IP)
		{
			m_returnToMainMenuRequested = true;
			setError(NetworkText::get(NetworkText::Id::OnlineDirectDisconnected));
			return;
		}
		if (m_kind == OnlineSessionKind::STEAM_LOBBY)
		{
			beginSteamHostMigration(m_steamNetworkHost);
			updateSteamHostMigration();
			return;
		}
	}

	if (!m_host && m_state == OnlineSessionState::CONNECTING && network->isConnected())
	{
		m_state = OnlineSessionState::IN_LOBBY;
		requestInitialScene();
	}
	if (m_kind == OnlineSessionKind::DIRECT_IP && !m_host && !m_directNameSent &&
		network->isConnected() && network->getClientId() >= 0)
	{
		m_directNameSent = true;
		network->call(
			MessageType::SERVER_ONLY,
			"RegisterDirectPlayerName",
			network->getClientId(),
			m_localPlayerName);
	}
	if (network->getConnectionStatus() == NetworkConnectionStatus::ERROR_STATUS &&
		m_state != OnlineSessionState::ERROR_STATUS)
	{
		if (m_kind == OnlineSessionKind::STEAM_LOBBY && !m_host)
		{
			beginSteamHostMigration(m_steamNetworkHost);
			updateSteamHostMigration();
		}
		else
		{
			setError(network->getLastError());
		}
	}
}

void OnlineSessionManager::release()
{
	if (!m_initialized)
	{
		return;
	}
	leaveSession();
	m_lobbyCreatedResult.Cancel();
	m_lobbyEnterResult.Cancel();
	m_lobbyOperationActive = false;
	if (m_steamAvailable)
	{
		m_lobbyDataCallback.Unregister();
		m_lobbyMembersCallback.Unregister();
		m_lobbyInviteCallback.Unregister();
		SteamAPI_Shutdown();
	}
	m_steamAvailable = false;
	m_initialized = false;
}

void OnlineSessionManager::useSteamPlayerName()
{
	if (m_steamAvailable && SteamFriends() != nullptr)
	{
		const char* persona = SteamFriends()->GetPersonaName();
		m_steamPlayerName = persona == nullptr ? std::string() : persona;
	}
	m_localPlayerName = m_steamPlayerName.empty()
		? NetworkText::get(NetworkText::Id::CommonPlayerTitle) : m_steamPlayerName;
}

bool OnlineSessionManager::hostDirect(int port)
{
	leaveSession();
	if (port <= 0 || port > 65535)
	{
		setError(NetworkText::get(NetworkText::Id::OnlineInvalidPort));
		return false;
	}

	m_kind = OnlineSessionKind::DIRECT_IP;
	m_localPlayerName = m_directPlayerName;
	m_host = true;
	m_state = OnlineSessionState::CONNECTING;
	m_network = std::make_unique<NetworkManager>(Role::SERVER, "0.0.0.0", port);
	configureNetworkCallbacks();
	if (!m_network->start())
	{
		setError(m_network->getLastError());
		return false;
	}
	m_directPlayerCount = 1;
	m_directPlayerNames.clear();
	m_directPlayerNames[0] = m_localPlayerName;
	m_directNameSent = true;
	m_state = OnlineSessionState::IN_LOBBY;
	m_launchRequested = true;
	return true;
}

bool OnlineSessionManager::joinDirect(const std::string& address, int port)
{
	leaveSession();
	if (address.empty())
	{
		setError(NetworkText::get(NetworkText::Id::OnlineAddressRequired));
		return false;
	}
	if (port <= 0 || port > 65535)
	{
		setError(NetworkText::get(NetworkText::Id::OnlineInvalidPort));
		return false;
	}

	m_kind = OnlineSessionKind::DIRECT_IP;
	m_localPlayerName = m_directPlayerName;
	m_host = false;
	m_state = OnlineSessionState::CONNECTING;
	m_network = std::make_unique<NetworkManager>(Role::CLIENT, address, port);
	configureNetworkCallbacks();
	if (!m_network->start())
	{
		setError(m_network->getLastError());
		return false;
	}
	m_directPlayerCount = 2;
	m_directPlayerNames.clear();
	m_directPlayerNames[0] = NetworkText::get(NetworkText::Id::OnlineDefaultHost);
	m_directNameSent = false;
	m_state = OnlineSessionState::IN_LOBBY;
	m_launchRequested = true;
	return true;
}

std::string OnlineSessionManager::getDefaultSteamLobbyName() const
{
	const std::string player = trimLobbyName(m_steamPlayerName);
	if (player.empty()) return limitLobbyName(NetworkText::get(NetworkText::Id::MainDefaultLobbyName), MaximumLobbyNameBytes);
	const std::string suffix = SteamLobbyNameSuffix;
	return limitLobbyName(player, MaximumLobbyNameBytes - suffix.size()) + suffix;
}

bool OnlineSessionManager::createSteamLobby(
	const std::string& lobbyName,
	bool privateLobby,
	const std::string& password,
	int capacity,
	const std::string& language)
{
	if (!m_steamAvailable || SteamMatchmaking() == nullptr)
	{
		setError(NetworkText::get(NetworkText::Id::OnlineSteamUnavailable));
		return false;
	}
	if (privateLobby && password.empty())
	{
		setError(NetworkText::get(NetworkText::Id::OnlinePrivatePasswordRequired));
		return false;
	}

	leaveSession();
	m_kind = OnlineSessionKind::STEAM_LOBBY;
	useSteamPlayerName();
	m_host = true;
	m_state = OnlineSessionState::CREATING;
	m_pendingLobbyName = trimLobbyName(limitLobbyName(trimLobbyName(lobbyName), MaximumLobbyNameBytes));
	if (m_pendingLobbyName.empty()) m_pendingLobbyName = getDefaultSteamLobbyName();
	// Capture the choice before asynchronous name checking/creation begins.
	m_pendingLobbyLanguage = LobbyLanguage::normalize(language);
	if (m_pendingLobbyLanguage.empty())
		m_pendingLobbyLanguage = LobbyLanguage::defaultCode(NetworkText::isFrench());
	m_pendingPassword = privateLobby ? password : std::string();
	m_pendingPrivate = privateLobby;
	m_pendingCapacity = std::clamp(capacity, 2, 16);

	m_queuedLobbyOperation = PendingLobbyOperation::CHECK_CREATE_NAME;
	startQueuedLobbyOperation();
	return m_state != OnlineSessionState::ERROR_STATUS;
}

bool OnlineSessionManager::requestSteamLobbyList()
{
	// RequestLobbyList cancels the previous Steam search. A browser refresh must
	// not steal the name check or change the state of a creation already underway.
	if (m_state == OnlineSessionState::CREATING ||
		m_lobbySearchPurpose == LobbySearchPurpose::CHECK_CREATE_NAME) return false;
	if (!m_steamAvailable || SteamMatchmaking() == nullptr)
	{
		setError(NetworkText::get(NetworkText::Id::OnlineSteamUnavailable));
		return false;
	}

	m_lobbyListResult.Cancel();
	m_lobbySearchPurpose = LobbySearchPurpose::NONE;
	m_lobbies.clear();
	m_state = OnlineSessionState::SEARCHING;
	SteamMatchmaking()->AddRequestLobbyListStringFilter(LobbyGameKey, LobbyGameValue, k_ELobbyComparisonEqual);
	SteamMatchmaking()->AddRequestLobbyListStringFilter(LobbyVersionKey, LobbyVersionValue, k_ELobbyComparisonEqual);
	SteamMatchmaking()->AddRequestLobbyListStringFilter(LobbyPrivateKey, "0", k_ELobbyComparisonEqual);
	SteamMatchmaking()->AddRequestLobbyListResultCountFilter(50);
	const SteamAPICall_t call = SteamMatchmaking()->RequestLobbyList();
	if (call == k_uAPICallInvalid)
	{
		setError(NetworkText::get(NetworkText::Id::OnlineSearchRejected));
		return false;
	}
	m_lobbySearchPurpose = LobbySearchPurpose::BROWSER;
	m_lobbySearchGeneration = m_requestGeneration;
	m_lobbyListResult.Set(call, this, &OnlineSessionManager::onLobbyListReceived);
	return true;
}

bool OnlineSessionManager::joinSteamLobby(std::uint64_t lobbyId, const std::string& password)
{
	if (!m_steamAvailable || SteamMatchmaking() == nullptr)
	{
		setError(NetworkText::get(NetworkText::Id::OnlineSteamUnavailable));
		return false;
	}

	CSteamID lobby(static_cast<uint64>(lobbyId));
	if (!lobby.IsValid() || !lobby.IsLobby())
	{
		setError(NetworkText::get(NetworkText::Id::OnlineInvalidLobbyId));
		return false;
	}

	leaveSession();
	m_kind = OnlineSessionKind::STEAM_LOBBY;
	useSteamPlayerName();
	m_host = false;
	m_state = OnlineSessionState::JOINING;
	m_pendingJoinLobby = lobby;
	m_pendingPassword = password;
	if (!SteamMatchmaking()->RequestLobbyData(lobby))
	{
		setError(NetworkText::get(NetworkText::Id::OnlineLobbyRetrieveFailed));
		return false;
	}
	return true;
}

void OnlineSessionManager::onLobbyCreated(LobbyCreated_t* result, bool ioFailure)
{
	const bool success = !ioFailure && result != nullptr && result->m_eResult == k_EResultOK;
	if (!completeLobbyOperation(result == nullptr ? CSteamID() : CSteamID(result->m_ulSteamIDLobby), success)) return;
	if (ioFailure || result == nullptr || result->m_eResult != k_EResultOK)
	{
		setError(NetworkText::get(NetworkText::Id::OnlineLobbyCreateFailed));
		return;
	}

	m_lobbyId = CSteamID(result->m_ulSteamIDLobby);
	m_steamNetworkHost = SteamUser() == nullptr ? CSteamID() : SteamUser()->GetSteamID();
	ISteamMatchmaking* matchmaking = SteamMatchmaking();
	matchmaking->SetLobbyData(m_lobbyId, LobbyGameKey, LobbyGameValue);
	matchmaking->SetLobbyData(m_lobbyId, LobbyVersionKey, LobbyVersionValue);
	matchmaking->SetLobbyData(m_lobbyId, LobbyNameKey, m_pendingLobbyName.c_str());
	matchmaking->SetLobbyData(m_lobbyId, LobbyLanguageKey, m_pendingLobbyLanguage.c_str());
	matchmaking->SetLobbyData(m_lobbyId, LobbyPrivateKey, m_pendingPrivate ? "1" : "0");
	matchmaking->SetLobbyData(m_lobbyId, LobbyPasswordRequiredKey, m_pendingPrivate ? "1" : "0");
	matchmaking->SetLobbyData(
		m_lobbyId,
		LobbyPasswordHashKey,
		m_pendingPrivate ? passwordHash(m_pendingPassword, m_lobbyId).c_str() : "");
	matchmaking->SetLobbyData(m_lobbyId, LobbyStateKey, LobbyStateWaiting);
	matchmaking->SetLobbyJoinable(m_lobbyId, true);
	if (SteamUser() != nullptr)
	{
		matchmaking->SetLobbyGameServer(m_lobbyId, 0, 0, SteamUser()->GetSteamID());
	}

	startSteamHostNetwork();
	if (m_network != nullptr && m_network->isRunning())
	{
		m_state = OnlineSessionState::IN_LOBBY;
		requestInitialScene();
	}
}

void OnlineSessionManager::onLobbyListReceived(LobbyMatchList_t* result, bool ioFailure)
{
	const LobbySearchPurpose purpose = m_lobbySearchPurpose;
	if (purpose == LobbySearchPurpose::NONE || m_lobbySearchGeneration != m_requestGeneration) return;
	m_lobbySearchPurpose = LobbySearchPurpose::NONE;
	if (purpose == LobbySearchPurpose::CHECK_CREATE_NAME)
	{
		if (m_kind != OnlineSessionKind::STEAM_LOBBY || m_state != OnlineSessionState::CREATING) return;
		if (ioFailure || result == nullptr || SteamMatchmaking() == nullptr)
		{
			setError(NetworkText::get(NetworkText::Id::OnlineLobbyNameCheckFailed));
			return;
		}
		if (result->m_nLobbiesMatching != 0)
		{
			setError(NetworkText::get(NetworkText::Id::OnlineLobbyNameTaken));
			return;
		}
		m_queuedLobbyOperation = PendingLobbyOperation::CREATE;
		startQueuedLobbyOperation();
		return;
	}
	if (m_state != OnlineSessionState::SEARCHING) return;
	m_lobbies.clear();
	if (ioFailure || result == nullptr || SteamMatchmaking() == nullptr)
	{
		setError(NetworkText::get(NetworkText::Id::OnlineLobbySearchFailed));
		return;
	}

	for (std::uint32_t index = 0; index < result->m_nLobbiesMatching; ++index)
	{
		const CSteamID lobby = SteamMatchmaking()->GetLobbyByIndex(static_cast<int>(index));
		if (!lobby.IsValid() ||
			std::string(SteamMatchmaking()->GetLobbyData(lobby, LobbyPrivateKey)) == "1" ||
			std::string(SteamMatchmaking()->GetLobbyData(lobby, LobbyGameKey)) != LobbyGameValue ||
			std::string(SteamMatchmaking()->GetLobbyData(lobby, LobbyVersionKey)) != LobbyVersionValue)
		{
			continue;
		}

		SteamLobbyEntry entry;
		entry.id = lobby.ConvertToUint64();
		entry.name = SteamMatchmaking()->GetLobbyData(lobby, LobbyNameKey);
		entry.language = LobbyLanguage::normalize(SteamMatchmaking()->GetLobbyData(lobby, LobbyLanguageKey));
		if (entry.name.empty()) entry.name = NetworkText::get(NetworkText::Id::OnlineDefaultLobby);
		entry.players = SteamMatchmaking()->GetNumLobbyMembers(lobby);
		entry.capacity = SteamMatchmaking()->GetLobbyMemberLimit(lobby);
		entry.passwordProtected = std::string(SteamMatchmaking()->GetLobbyData(lobby, LobbyPasswordRequiredKey)) == "1";
		m_lobbies.push_back(std::move(entry));
	}
	// Keep every language visible and preserve Steam's order within each group.
	const char* preferredLanguage = LobbyLanguage::defaultCode(NetworkText::isFrench());
	std::stable_partition(m_lobbies.begin(), m_lobbies.end(),
		[preferredLanguage](const SteamLobbyEntry& entry)
		{
			return LobbyLanguage::isPreferred(entry.language, preferredLanguage);
		});
	m_state = OnlineSessionState::IDLE;
}

void OnlineSessionManager::onLobbyEntered(LobbyEnter_t* result, bool ioFailure)
{
	const bool success = !ioFailure && result != nullptr &&
		result->m_EChatRoomEnterResponse == k_EChatRoomEnterResponseSuccess;
	if (!completeLobbyOperation(result == nullptr ? CSteamID() : CSteamID(result->m_ulSteamIDLobby), success)) return;
	if (ioFailure || result == nullptr || result->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess)
	{
		setError(NetworkText::get(NetworkText::Id::OnlineAccessRefused));
		return;
	}

	m_lobbyId = CSteamID(result->m_ulSteamIDLobby);
	if (!validateLobbyCompatibility(m_lobbyId))
	{
		const std::string error = m_lastError;
		leaveSession();
		setError(error);
		return;
	}
	m_pendingCapacity = SteamMatchmaking()->GetLobbyMemberLimit(m_lobbyId);
	m_pendingPrivate = std::string(SteamMatchmaking()->GetLobbyData(m_lobbyId, LobbyPrivateKey)) == "1";
	const CSteamID owner = SteamMatchmaking()->GetLobbyOwner(m_lobbyId);
	m_steamNetworkHost = owner;
	m_host = SteamUser() != nullptr && owner == SteamUser()->GetSteamID();
	if (m_host)
	{
		startSteamHostNetwork();
	}
	else
	{
		startSteamClientNetwork(owner);
	}
	if (m_network == nullptr || !m_network->isRunning()) return;
	m_state = m_network->isConnected() ? OnlineSessionState::IN_LOBBY : OnlineSessionState::CONNECTING;
	if (m_state == OnlineSessionState::IN_LOBBY) requestInitialScene();
}

void OnlineSessionManager::onLobbyDataUpdated(LobbyDataUpdate_t* event)
{
	if (event == nullptr)
	{
		return;
	}
	const CSteamID lobby(event->m_ulSteamIDLobby);
	if (m_pendingJoinLobby.IsValid() && lobby == m_pendingJoinLobby)
	{
		m_pendingJoinLobby.Clear();
		if (event->m_bSuccess == 0 || !validateLobbyCompatibility(lobby) || !validatePendingLobbyPassword(lobby))
		{
			if (event->m_bSuccess == 0)
			{
				setError(NetworkText::get(NetworkText::Id::OnlineLobbyGone));
			}
			return;
		}
		beginJoinLobby(lobby);
		return;
	}

	if (m_lobbyId.IsValid() && lobby == m_lobbyId && SteamMatchmaking() != nullptr)
	{
		if (std::string(SteamMatchmaking()->GetLobbyData(m_lobbyId, LobbyStateKey)) == LobbyStateStarting)
		{
			requestInitialScene();
		}
	}
}

void OnlineSessionManager::onLobbyMembersChanged(LobbyChatUpdate_t* event)
{
	if (event == nullptr || !m_lobbyId.IsValid() || event->m_ulSteamIDLobby != m_lobbyId.ConvertToUint64())
	{
		return;
	}

	const std::uint32_t departureFlags =
		k_EChatMemberStateChangeLeft |
		k_EChatMemberStateChangeDisconnected |
		k_EChatMemberStateChangeKicked |
		k_EChatMemberStateChangeBanned;
	const CSteamID changedMember(event->m_ulSteamIDUserChanged);
	if ((event->m_rgfChatMemberStateChange & departureFlags) != 0)
	{
		if (SteamUser() != nullptr && changedMember == SteamUser()->GetSteamID())
		{
			// Membership has ended, so this process cannot reconnect or host here.
			// Do not transfer ownership from a lobby we no longer belong to.
			m_host = false;
			const bool kicked = (event->m_rgfChatMemberStateChange &
				(k_EChatMemberStateChangeKicked | k_EChatMemberStateChangeBanned)) != 0;
			failSteamHostMigration(NetworkText::get(kicked ? NetworkText::Id::OnlineKicked : NetworkText::Id::OnlineLobbyGone));
			return;
		}
		const bool departedCurrentHost = changedMember == m_steamNetworkHost;
		const bool departedMigratedHost = changedMember == m_expectedDepartingHost;
		if (departedCurrentHost || departedMigratedHost)
		{
			rememberDepartedSteamHost(changedMember);
		}
		if (departedCurrentHost)
		{
			beginSteamHostMigration(changedMember);
		}
		if (departedMigratedHost)
		{
			m_expectedDepartingHost.Clear();
		}
	}

	if (SteamMatchmaking() == nullptr || SteamMatchmaking()->GetNumLobbyMembers(m_lobbyId) <= 0)
	{
		failSteamHostMigration(NetworkText::get(NetworkText::Id::OnlineNoOwner));
		return;
	}
	observeSteamLobbyOwner();
	if (!m_migratingHost && !m_host)
	{
		m_departedHostPending = false;
	}
}

void OnlineSessionManager::onLobbyJoinRequested(GameLobbyJoinRequested_t* event)
{
	if (event != nullptr) requestLobbyInvite(event->m_steamIDLobby.ConvertToUint64());
}

void OnlineSessionManager::requestLobbyInvite(std::uint64_t lobbyId)
{
	const CSteamID lobby(static_cast<uint64>(lobbyId));
	if (!lobby.IsValid() || !lobby.IsLobby() || lobby == m_lobbyId) return;
	const bool returnToSceneMenu = m_sceneLaunched;
	leaveSession();
	m_invitedLobbyId = lobbyId;
	m_returnToMainMenuRequested = returnToSceneMenu;
}

void OnlineSessionManager::beginJoinLobby(CSteamID lobby)
{
	m_pendingJoinLobby.Clear();
	m_queuedJoinLobby = lobby;
	m_queuedLobbyOperation = PendingLobbyOperation::JOIN;
	startQueuedLobbyOperation();
}

void OnlineSessionManager::startQueuedLobbyOperation()
{
	if (m_lobbyOperationActive || m_lobbySearchPurpose == LobbySearchPurpose::CHECK_CREATE_NAME ||
		m_queuedLobbyOperation == PendingLobbyOperation::NONE ||
		!m_steamAvailable || SteamMatchmaking() == nullptr) return;
	const PendingLobbyOperation operation = m_queuedLobbyOperation;
	m_queuedLobbyOperation = PendingLobbyOperation::NONE;
	if (operation == PendingLobbyOperation::CHECK_CREATE_NAME)
	{
		startLobbyNameCheck();
		return;
	}
	m_activeRequestGeneration = m_requestGeneration;
	m_lobbyOperationActive = true;
	// "Private" games remain joinable by their shared ID, but are excluded from
	// our browser. The encrypted transport authenticates the password on the host.
	const SteamAPICall_t call = operation == PendingLobbyOperation::CREATE
		? SteamMatchmaking()->CreateLobby(k_ELobbyTypePublic, m_pendingCapacity)
		: SteamMatchmaking()->JoinLobby(m_queuedJoinLobby);
	m_queuedJoinLobby.Clear();
	if (call == k_uAPICallInvalid)
	{
		m_lobbyOperationActive = false;
		setError(NetworkText::get(operation == PendingLobbyOperation::CREATE
			? NetworkText::Id::OnlineCreateRejected : NetworkText::Id::OnlineJoinRejected));
		return;
	}
	if (operation == PendingLobbyOperation::CREATE)
		m_lobbyCreatedResult.Set(call, this, &OnlineSessionManager::onLobbyCreated);
	else
		m_lobbyEnterResult.Set(call, this, &OnlineSessionManager::onLobbyEntered);
}

void OnlineSessionManager::startLobbyNameCheck()
{
	// This is a fresh, exact-name search, independent of the browser's limited
	// regional results. Protected lobbies and other game versions count too.
	// Steam excludes full/non-joinable lobbies; this is not an atomic reservation.
	m_lobbyListResult.Cancel();
	m_lobbySearchPurpose = LobbySearchPurpose::NONE;
	auto* matchmaking = SteamMatchmaking();
	matchmaking->AddRequestLobbyListStringFilter(LobbyGameKey, LobbyGameValue, k_ELobbyComparisonEqual);
	matchmaking->AddRequestLobbyListStringFilter(LobbyNameKey, m_pendingLobbyName.c_str(), k_ELobbyComparisonEqual);
	matchmaking->AddRequestLobbyListDistanceFilter(k_ELobbyDistanceFilterWorldwide);
	matchmaking->AddRequestLobbyListResultCountFilter(1);
	const SteamAPICall_t call = matchmaking->RequestLobbyList();
	if (call == k_uAPICallInvalid)
	{
		setError(NetworkText::get(NetworkText::Id::OnlineLobbyNameCheckFailed));
		return;
	}
	m_lobbySearchPurpose = LobbySearchPurpose::CHECK_CREATE_NAME;
	m_lobbySearchGeneration = m_requestGeneration;
	m_lobbyListResult.Set(call, this, &OnlineSessionManager::onLobbyListReceived);
}

bool OnlineSessionManager::completeLobbyOperation(CSteamID joinedLobby, bool success)
{
	const bool current = m_lobbyOperationActive &&
		m_activeRequestGeneration == m_requestGeneration &&
		m_kind == OnlineSessionKind::STEAM_LOBBY;
	m_lobbyOperationActive = false;
	if (!current && success && joinedLobby.IsValid() && SteamMatchmaking() != nullptr)
		SteamMatchmaking()->LeaveLobby(joinedLobby);
	return current;
}

bool OnlineSessionManager::validateLobbyCompatibility(CSteamID lobby)
{
	if (SteamMatchmaking() != nullptr &&
		std::string(SteamMatchmaking()->GetLobbyData(lobby, LobbyGameKey)) == LobbyGameValue &&
		std::string(SteamMatchmaking()->GetLobbyData(lobby, LobbyVersionKey)) == LobbyVersionValue)
		return true;
	setError(NetworkText::get(NetworkText::Id::OnlineIncompatibleLobby));
	return false;
}

bool OnlineSessionManager::authorizeSteamPeer(std::uint64_t steamId, const std::string& password) const
{
	if (m_kind != OnlineSessionKind::STEAM_LOBBY || !m_host || !m_lobbyId.IsValid() ||
		SteamMatchmaking() == nullptr || password != m_pendingPassword) return false;
	const int count = SteamMatchmaking()->GetNumLobbyMembers(m_lobbyId);
	for (int index = 0; index < count; ++index)
	{
		if (SteamMatchmaking()->GetLobbyMemberByIndex(m_lobbyId, index).ConvertToUint64() == steamId)
			return true;
	}
	return false;
}

void OnlineSessionManager::requestInitialScene()
{
	if (!m_sceneLaunched && m_network != nullptr && m_network->isConnected())
		m_launchRequested = true;
}

bool OnlineSessionManager::validatePendingLobbyPassword(CSteamID lobby)
{
	if (SteamMatchmaking() == nullptr)
	{
		return false;
	}
	const bool passwordRequired = std::string(
		SteamMatchmaking()->GetLobbyData(lobby, LobbyPasswordRequiredKey)) == "1";
	if (!passwordRequired)
	{
		m_pendingPassword.clear();
		return true;
	}
	if (m_pendingPassword.empty())
	{
		leaveSession();
		m_lobbyPasswordRequestId = lobby.ConvertToUint64();
		return false;
	}
	const std::string expected = SteamMatchmaking()->GetLobbyData(lobby, LobbyPasswordHashKey);
	if (expected == passwordHash(m_pendingPassword, lobby))
	{
		return true;
	}
	setError(NetworkText::get(NetworkText::Id::OnlineIncorrectPassword));
	return false;
}

std::string OnlineSessionManager::passwordHash(const std::string& password, CSteamID lobby) const
{
	const std::uint64_t seed = HashUtil::DEFAULT_SEED64 ^ lobby.ConvertToUint64();
	const std::uint64_t value = HashUtil::hash64(password, seed);
	std::ostringstream stream;
	stream << std::hex << std::setw(16) << std::setfill('0') << value;
	return stream.str();
}

void OnlineSessionManager::startSteamHostNetwork()
{
	if (m_network != nullptr)
	{
		if (m_network->isRunning() && m_network->isServer())
		{
			return;
		}
		m_network->stop();
		m_network.reset();
	}
	m_network = std::make_unique<NetworkManager>(Role::SERVER, CSteamID());
	m_network->configureSteamAdmission(
		[this](std::uint64_t peer, const std::string& credential)
		{ return authorizeSteamPeer(peer, credential); }, std::string(), m_pendingCapacity);
	configureNetworkCallbacks();
	if (!m_network->start() && !m_migratingHost)
	{
		setError(m_network->getLastError());
	}
}

void OnlineSessionManager::startSteamClientNetwork(CSteamID host)
{
	if (m_network != nullptr)
	{
		m_network->stop();
		m_network.reset();
	}
	m_network = std::make_unique<NetworkManager>(Role::CLIENT, host);
	m_network->configureSteamAdmission({}, m_pendingPassword, m_pendingCapacity);
	configureNetworkCallbacks();
	if (!m_network->start() && !m_migratingHost)
	{
		setError(m_network->getLastError());
	}
}

void OnlineSessionManager::configureNetworkCallbacks()
{
	if (m_network == nullptr)
	{
		return;
	}
	// Preserve an explicitly enabled asset gate across transport migration.
	// An empty project has no collector and starts with this optional gate off.
	m_network->setResourceIntegrityRequired(m_resourceIntegrityRequired);
	m_resourceIntegritySession.bindTransport(m_network->getInstanceId(), m_host);
	m_network->registerServerToClientFunction<std::uint64_t, int>("ResourceIntegrityChallenge",
		std::function<void(std::uint64_t, int)>([this](std::uint64_t epoch, int version)
		{
			if (m_host) return;
			m_resourceIntegritySession.receiveChallenge(epoch, version);
			updateResourceIntegrity();
		}));
	m_network->registerClientToServerFunction<std::uint64_t, int, std::string>("ResourceIntegrityResponse",
		std::function<void(std::uint64_t, int, std::string)>([this](std::uint64_t epoch, int version, std::string digest)
		{
			if (!m_host || m_network == nullptr) return;
			// The transport owns sender identity. No client-supplied player ID is trusted.
			const int clientId = m_network->getExecutingClientId();
			if (clientId <= 0) return;
			m_resourceIntegritySession.receiveResponse(clientId, epoch, version, digest, integrityMilliseconds());
			updateResourceIntegrity();
		}));
	m_network->registerServerToClientFunction<std::uint64_t, int>("ResourceIntegrityAccepted",
		std::function<void(std::uint64_t, int)>([this](std::uint64_t epoch, int version)
		{
			if (!m_host) m_resourceIntegritySession.receiveAccepted(epoch, version);
		}));
	m_network->addClientConnectFunction("OnlineResourceIntegrity", [this](int clientId)
	{
		if (!m_host || m_network == nullptr) return;
		m_resourceIntegritySession.addPeer(clientId, integrityMilliseconds());
		m_network->addClientDisconnectFunction(clientId, "OnlineResourceIntegrity", [this, clientId](int)
		{
			m_resourceIntegritySession.removePeer(clientId);
		});
		updateResourceIntegrity();
	});
	m_network->registerServerToClientFunction<std::string>("SessionKicked",
		std::function<void(std::string)>([this](std::string reason)
		{
			if (m_host) return;
			// Defer leaving: deleting NetworkManager inside its RPC would invalidate it.
			m_kickReason = NetworkText::clean(reason, 120);
			m_kickPending = true;
		}));
	m_network->registerServerToClientFunction<>("StartGame", std::function<void()>([this]()
	{
		requestInitialScene();
	}));
	m_network->registerServerToClientFunction<int>("SessionPlayerCount", std::function<void(int)>([this](int playerCount)
	{
		m_directPlayerCount = std::max(1, playerCount);
	}));
	m_network->registerServerToClientFunction<int, std::string>(
		"DirectPlayerName",
		std::function<void(int, std::string)>([this](int clientId, std::string playerName)
		{
			m_directPlayerNames[clientId] = std::move(playerName);
		}));
	m_network->registerServerToClientFunction<int>("DirectPlayerLeft", std::function<void(int)>([this](int clientId)
	{
		m_directPlayerNames.erase(clientId);
	}));
	if (m_kind == OnlineSessionKind::DIRECT_IP && !m_host)
	{
		m_network->registerClientToServerFunction<int, std::string>(
			"RegisterDirectPlayerName",
			std::function<void(int, std::string)>([](int, std::string) {}));
	}
	if (m_kind == OnlineSessionKind::DIRECT_IP && m_host)
	{
		m_network->registerClientToServerFunction<int, std::string>(
			"RegisterDirectPlayerName",
			std::function<void(int, std::string)>([this](int clientId, std::string playerName)
			{
				const int authenticatedClientId =
					m_network == nullptr
						? -1
						: m_network->getExecutingClientId();
				if (authenticatedClientId <= 0 || clientId != authenticatedClientId)
				{
					return;
				}
				playerName = trimLobbyName(limitLobbyName(trimLobbyName(playerName), MaximumDirectPlayerNameBytes));
				if (playerName.empty())
				{
					playerName = NetworkText::get(NetworkText::Id::OnlineDefaultPlayerPrefix) + std::to_string(clientId);
				}
				m_directPlayerNames[clientId] = std::move(playerName);
				sendDirectPlayerNames();
			}));
		m_network->addClientConnectFunction("OnlineSessionPlayerCount", [this](int clientId)
		{
			m_network->addClientDisconnectFunction(clientId, "OnlineSessionPlayerCount", [this, clientId](int)
			{
				m_directPlayerNames.erase(clientId);
				m_network->call(MessageType::CLIENT_ONLY, "DirectPlayerLeft", clientId);
				broadcastDirectPlayerCount();
			});
			broadcastDirectPlayerCount();
			sendDirectPlayerNames(clientId);
		});
	}
	m_network->getOnServeurDisconnectClient().push_back([this]()
	{
		m_hostTransportLost = true;
	});
}

void OnlineSessionManager::beginResourceIntegrityCheck()
{
	m_resourceIntegrityRequired = true;
	if (m_network == nullptr) m_resourceIntegritySession.bindTransport(0, m_host);
	else m_network->setResourceIntegrityRequired(true);
	m_resourceIntegritySession.beginLocalCheck();
	updateResourceIntegrity();
}

void OnlineSessionManager::completeResourceIntegrityCheck(const std::string& digest, const std::string& error)
{
	m_resourceIntegritySession.completeLocalCheck(digest, error);
	if (m_resourceIntegritySession.localState() == ResourceIntegritySession::LocalState::Failed)
		m_launchRequested = false;
	updateResourceIntegrity();
}

void OnlineSessionManager::cancelResourceIntegrityCheck()
{
	if (!m_resourceIntegrityRequired) return;
	m_resourceIntegritySession.cancelLocalCheck();
	if (m_network != nullptr) m_network->setResourceIntegrityRequired(true);
}

bool OnlineSessionManager::isResourceIntegrityAccepted() const
{
	if (m_migratingHost) return false;
	if (!m_resourceIntegrityRequired) return true;
	// Editor/offline play has no transport or remote reference to acknowledge.
	if (m_network == nullptr && m_kind == OnlineSessionKind::NONE)
		return m_resourceIntegritySession.localState() == ResourceIntegritySession::LocalState::Ready;
	return m_resourceIntegritySession.accepted();
}

void OnlineSessionManager::updateResourceIntegrity()
{
	if (!m_resourceIntegrityRequired || m_network == nullptr) return;
	m_resourceIntegritySession.update(integrityMilliseconds());
	if (m_host)
	{
		for (auto& entry : m_resourceIntegritySession.peers())
		{
			auto& peer = entry.second;
			if (!peer.challengeSent)
			{
				peer.challengeSent = true;
				m_network->call(MessageType::CLIENT_ONLY_TARGET, entry.first, "ResourceIntegrityChallenge",
					peer.epoch, ResourceIntegritySession::ProtocolVersion);
			}
			if (peer.verdictDelivered) continue;
			if (peer.accepted)
			{
				peer.verdictDelivered = true;
				m_network->setClientResourceIntegrityValidated(entry.first, true);
				m_network->call(MessageType::CLIENT_ONLY_TARGET, entry.first, "ResourceIntegrityAccepted",
					peer.epoch, ResourceIntegritySession::ProtocolVersion);
			}
			else if (peer.failure != ResourceIntegritySession::Failure::None)
			{
				peer.verdictDelivered = true;
				kickClient(entry.first, integrityFailureText(peer.failure));
			}
		}
	}
	else if (m_network->isConnected())
	{
		ResourceIntegritySession::Response response;
		if (m_resourceIntegritySession.takeResponse(response))
			m_network->call(MessageType::SERVER_ONLY, "ResourceIntegrityResponse", response.epoch,
				response.version, response.digest);
	}
}

bool OnlineSessionManager::finishResourceIntegrityFailure()
{
	if (!m_resourceIntegrityRequired) return false;
	const bool localFailure = m_resourceIntegritySession.localState() == ResourceIntegritySession::LocalState::Failed;
	const auto protocolFailure = m_resourceIntegritySession.clientFailure();
	if (!localFailure && protocolFailure == ResourceIntegritySession::Failure::None) return false;
	const std::string message = integrityFailureText(localFailure
		? ResourceIntegritySession::Failure::Unavailable : protocolFailure);
	// Called outside NetworkManager::update(): never destroy an executing RPC.
	leaveSession();
	m_returnToMainMenuRequested = true;
	setError(message);
	return true;
}

void OnlineSessionManager::broadcastDirectPlayerCount()
{
	if (m_kind != OnlineSessionKind::DIRECT_IP || !m_host || m_network == nullptr)
	{
		return;
	}
	m_directPlayerCount = 1 + m_network->getClientCount();
	m_network->call(MessageType::CLIENT_ONLY, "SessionPlayerCount", m_directPlayerCount);
}

void OnlineSessionManager::sendDirectPlayerNames(int targetClientId)
{
	if (m_kind != OnlineSessionKind::DIRECT_IP || !m_host || m_network == nullptr)
	{
		return;
	}
	for (const auto& player : m_directPlayerNames)
	{
		if (targetClientId >= 0)
		{
			m_network->call(
				MessageType::CLIENT_ONLY_TARGET,
				targetClientId,
				"DirectPlayerName",
				player.first,
				player.second);
		}
		else
		{
			m_network->call(MessageType::CLIENT_ONLY, "DirectPlayerName", player.first, player.second);
		}
	}
}

bool OnlineSessionManager::isNetworkReady() const
{
	return m_network != nullptr && m_network->isConnected();
}

int OnlineSessionManager::getPlayerCount() const
{
	if (m_kind == OnlineSessionKind::STEAM_LOBBY && m_lobbyId.IsValid() && SteamMatchmaking() != nullptr)
	{
		return SteamMatchmaking()->GetNumLobbyMembers(m_lobbyId);
	}
	if (m_kind == OnlineSessionKind::DIRECT_IP && m_network != nullptr)
	{
		return m_directPlayerCount;
	}
	return 1;
}

std::vector<OnlinePlayerInfo> OnlineSessionManager::getPlayers() const
{
	std::vector<OnlinePlayerInfo> players;
	if (m_kind == OnlineSessionKind::STEAM_LOBBY && m_lobbyId.IsValid() &&
		SteamMatchmaking() != nullptr && SteamFriends() != nullptr)
	{
		const CSteamID localSteamId = SteamUser() == nullptr ? CSteamID() : SteamUser()->GetSteamID();
		const CSteamID owner = SteamMatchmaking()->GetLobbyOwner(m_lobbyId);
		const std::map<std::uint64_t, int> pings =
			m_network == nullptr ? std::map<std::uint64_t, int>() : m_network->getSteamPeerPings();
		const int memberCount = SteamMatchmaking()->GetNumLobbyMembers(m_lobbyId);
		players.reserve(memberCount);
		for (int index = 0; index < memberCount; ++index)
		{
			const CSteamID member = SteamMatchmaking()->GetLobbyMemberByIndex(m_lobbyId, index);
			OnlinePlayerInfo player;
			player.steamId = member.ConvertToUint64();
			player.name = SteamFriends()->GetFriendPersonaName(member);
			if (player.name.empty()) player.name = NetworkText::get(NetworkText::Id::OnlineSteamPlayer);
			player.local = localSteamId.IsValid() && member == localSteamId;
			player.host = owner.IsValid() && member == owner;
			if (player.local && player.host)
			{
				player.pingMs = 0;
			}
			else
			{
				// A client's own latency is the RTT of its connection to the host.
				const std::uint64_t peerId = player.local ? owner.ConvertToUint64() : player.steamId;
				const auto ping = pings.find(peerId);
				if (ping != pings.end()) player.pingMs = ping->second;
			}
			players.push_back(std::move(player));
		}
		return players;
	}

	if (m_kind == OnlineSessionKind::DIRECT_IP)
	{
		const int localNetworkId = m_host
			? 0
			: (m_network == nullptr ? -1 : m_network->getClientId());
		players.reserve(m_directPlayerNames.size());
		for (const auto& member : m_directPlayerNames)
		{
			OnlinePlayerInfo player;
			player.networkId = member.first;
			player.name = member.second;
			player.local = member.first == localNetworkId;
			player.host = member.first == 0;
			player.pingMs = player.local ? 0 : -1;
			players.push_back(std::move(player));
		}
	}

	if (players.empty())
	{
		OnlinePlayerInfo player;
		player.networkId = m_host ? 0 : -1;
		// Preserve the local account identity while Steam is creating the lobby.
		if (m_kind == OnlineSessionKind::STEAM_LOBBY && m_steamAvailable)
		{
			if (auto* user = SteamUser())
				player.steamId = user->GetSteamID().ConvertToUint64();
		}
		player.name = m_localPlayerName;
		player.local = true;
		player.host = m_host;
		player.pingMs = m_kind == OnlineSessionKind::STEAM_LOBBY && !m_host ? -1 : 0;
		players.push_back(std::move(player));
	}
	return players;
}

std::vector<std::string> OnlineSessionManager::getMemberNames() const
{
	std::vector<std::string> names;
	const std::vector<OnlinePlayerInfo> players = getPlayers();
	names.reserve(players.size());
	for (const OnlinePlayerInfo& player : players)
	{
		names.push_back(player.name);
	}
	return names;
}

void OnlineSessionManager::setMinimumPlayers(int count)
{
	m_minimumPlayers = std::clamp(count, 1, 16);
}

bool OnlineSessionManager::canStartGame(bool allowDebugSolo) const
{
	if (!m_host || m_network == nullptr || !m_network->isConnected())
	{
		return false;
	}
	return allowDebugSolo || getPlayerCount() >= m_minimumPlayers;
}

bool OnlineSessionManager::startGame(bool allowDebugSolo)
{
	if (!canStartGame(allowDebugSolo))
	{
		return false;
	}
	m_network->call(MessageType::SERVER_CLIENT, "StartGame");
	return true;
}

void OnlineSessionManager::setLocalPlayerName(const std::string& name)
{
	m_directPlayerName = trimLobbyName(limitLobbyName(trimLobbyName(name), MaximumDirectPlayerNameBytes));
	if (m_directPlayerName.empty())
		m_directPlayerName = trimLobbyName(limitLobbyName(trimLobbyName(m_steamPlayerName), MaximumDirectPlayerNameBytes));
	if (m_directPlayerName.empty()) m_directPlayerName = NetworkText::get(NetworkText::Id::CommonPlayerTitle);
	if (m_kind == OnlineSessionKind::STEAM_LOBBY) return;
	m_localPlayerName = m_directPlayerName;
	if (m_kind == OnlineSessionKind::DIRECT_IP && m_host)
	{
		m_directPlayerNames[0] = m_localPlayerName;
		sendDirectPlayerNames();
	}
}


void OnlineSessionManager::startDebugSolo()
{
	leaveSession();
	m_host = true;
	m_launchRequested = true;
}

bool OnlineSessionManager::consumeLaunchRequest()
{
	const bool requested = m_launchRequested;
	m_launchRequested = false;
	if (requested) m_sceneLaunched = true;
	return requested;
}

std::uint64_t OnlineSessionManager::consumeLobbyInvite()
{
	const std::uint64_t lobbyId = m_invitedLobbyId;
	m_invitedLobbyId = 0;
	return lobbyId;
}

std::uint64_t OnlineSessionManager::consumeLobbyPasswordRequest()
{
	const std::uint64_t lobbyId = m_lobbyPasswordRequestId;
	m_lobbyPasswordRequestId = 0;
	return lobbyId;
}

bool OnlineSessionManager::consumeReturnToMainMenuRequest()
{
	const bool requested = m_returnToMainMenuRequested;
	m_returnToMainMenuRequested = false;
	return requested;
}

bool OnlineSessionManager::consumeDepartedSteamHost(OnlineHostDeparture& departure)
{
	if (!m_host || !m_departedHostPending)
	{
		return false;
	}
	departure = m_departedHost;
	m_departedHostPending = false;
	return true;
}

bool OnlineSessionManager::kickClient(int networkId, const std::string& reason)
{
	if (!m_host || m_network == nullptr || !m_network->kickClient(networkId)) return false;
	m_network->call(MessageType::CLIENT_ONLY_TARGET, networkId, "SessionKicked", NetworkText::clean(reason, 120));
	return true;
}

bool OnlineSessionManager::finishPendingKick()
{
	if (m_host || (!m_kickPending && (m_network == nullptr || !m_network->wasKicked()))) return false;
	const std::string reason = m_kickReason;
	leaveSession();
	m_returnToMainMenuRequested = true;
	setError(reason.empty() ? NetworkText::get(NetworkText::Id::OnlineKicked) :
		NetworkText::format(NetworkText::get(NetworkText::Id::OnlineKickedReason), reason));
	return true;
}

void OnlineSessionManager::leaveSession()
{
	transferSteamLobbyOwnershipBeforeLeave();
	++m_requestGeneration;
	m_queuedLobbyOperation = PendingLobbyOperation::NONE;
	m_queuedJoinLobby.Clear();
	m_lobbyListResult.Cancel();
	m_lobbySearchPurpose = LobbySearchPurpose::NONE;
	if (m_network != nullptr)
	{
		m_network->stop();
		m_network.reset();
	}
	if (m_steamAvailable && m_lobbyId.IsValid() && SteamMatchmaking() != nullptr)
	{
		SteamMatchmaking()->LeaveLobby(m_lobbyId);
	}
	resetSessionState();
}

void OnlineSessionManager::observeSteamLobbyOwner()
{
	if (m_kind != OnlineSessionKind::STEAM_LOBBY || !m_lobbyId.IsValid() ||
		SteamMatchmaking() == nullptr)
	{
		return;
	}

	const CSteamID owner = SteamMatchmaking()->GetLobbyOwner(m_lobbyId);
	if (!owner.IsValid())
	{
		return;
	}
	if (!m_steamNetworkHost.IsValid())
	{
		m_steamNetworkHost = owner;
		return;
	}
	if (!m_migratingHost && owner != m_steamNetworkHost)
	{
		beginSteamHostMigration(m_steamNetworkHost);
	}
}

void OnlineSessionManager::beginSteamHostMigration(CSteamID previousHost)
{
	if (m_kind != OnlineSessionKind::STEAM_LOBBY || !m_lobbyId.IsValid() || m_migratingHost)
	{
		return;
	}

	m_migratingHost = true;
	m_resourceIntegritySession.clearTransport();
	m_host = false;
	m_hostTransportLost = false;
	m_state = OnlineSessionState::MIGRATING_HOST;
	m_migrationPreviousHost = previousHost.IsValid() ? previousHost : m_steamNetworkHost;
	m_expectedDepartingHost = m_migrationPreviousHost;
	m_migrationTargetHost.Clear();
	m_migrationStarted = std::chrono::steady_clock::now();
	m_nextMigrationRetry = m_migrationStarted;
	if (m_network != nullptr)
	{
		m_network->stop();
		m_network.reset();
	}
	Debug::Info(
		"Steam host migration started after %llu",
		static_cast<unsigned long long>(m_migrationPreviousHost.ConvertToUint64()));
}

void OnlineSessionManager::updateSteamHostMigration()
{
	if (!m_migratingHost)
	{
		return;
	}

	const auto now = std::chrono::steady_clock::now();
	if (now - m_migrationStarted >= HostMigrationTimeout)
	{
		failSteamHostMigration(NetworkText::get(NetworkText::Id::OnlineMigrationTimeout));
		return;
	}
	if (SteamMatchmaking() == nullptr || SteamUser() == nullptr || !m_lobbyId.IsValid())
	{
		failSteamHostMigration(NetworkText::get(NetworkText::Id::OnlineMigrationUnavailable));
		return;
	}

	const CSteamID owner = SteamMatchmaking()->GetLobbyOwner(m_lobbyId);
	// A P2P interruption can leave the current host in the Steam lobby.
	// Retry that owner too; Steam alone decides whether ownership changes.
	if (!owner.IsValid())
	{
		return;
	}

	if (owner != m_migrationTargetHost)
	{
		if (m_network != nullptr)
		{
			m_network->stop();
			m_network.reset();
		}
		m_migrationTargetHost = owner;
		m_nextMigrationRetry = now;
	}
	if (now < m_nextMigrationRetry)
	{
		return;
	}

	if (m_network == nullptr)
	{
		m_host = owner == SteamUser()->GetSteamID();
		if (m_host)
		{
			startSteamHostNetwork();
		}
		else
		{
			startSteamClientNetwork(owner);
		}

		if (m_network == nullptr || !m_network->isRunning())
		{
			if (m_network != nullptr)
			{
				m_network->stop();
				m_network.reset();
			}
			m_nextMigrationRetry = now + HostMigrationRetryDelay;
			return;
		}
		if (m_host)
		{
			finishSteamHostMigration(owner);
			return;
		}
	}

	NetworkManager* network = m_network.get();
	network->update();
	updateResourceIntegrity();
	if (finishPendingKick() || finishResourceIntegrityFailure()) return;
	if (network->wasAdmissionRejected())
	{
		failSteamHostMigration(NetworkText::get(NetworkText::Id::OnlineAdmissionRefused));
		return;
	}
	if (m_hostTransportLost)
	{
		m_hostTransportLost = false;
		network->stop();
		m_network.reset();
		m_nextMigrationRetry = now + HostMigrationRetryDelay;
		return;
	}
	if (network->isConnected())
	{
		finishSteamHostMigration(owner);
		return;
	}
	if (network->getConnectionStatus() == NetworkConnectionStatus::ERROR_STATUS)
	{
		network->stop();
		m_network.reset();
		m_nextMigrationRetry = now + HostMigrationRetryDelay;
	}
}

void OnlineSessionManager::finishSteamHostMigration(CSteamID newHost)
{
	m_steamNetworkHost = newHost;
	m_migrationPreviousHost.Clear();
	m_migrationTargetHost.Clear();
	m_migratingHost = false;
	m_hostTransportLost = false;
	m_state = OnlineSessionState::IN_LOBBY;
	requestInitialScene();
	m_lastError.clear();
	if (!m_host)
	{
		m_departedHostPending = false;
	}
	if (m_host && SteamMatchmaking() != nullptr)
	{
		SteamMatchmaking()->SetLobbyGameServer(m_lobbyId, 0, 0, newHost);
	}
	Debug::Info(
		"Steam host migration completed. New host: %llu",
		static_cast<unsigned long long>(newHost.ConvertToUint64()));
}

void OnlineSessionManager::failSteamHostMigration(const std::string& message)
{
	const bool returnToSceneMenu = m_sceneLaunched;
	const std::string error = message;
	leaveSession();
	m_returnToMainMenuRequested = returnToSceneMenu;
	setError(error);
}


void OnlineSessionManager::rememberDepartedSteamHost(CSteamID departedHost)
{
	if (!departedHost.IsValid() ||
		(m_departedHostPending && m_departedHost.steamId == departedHost.ConvertToUint64()))
	{
		return;
	}

	m_departedHost.steamId = departedHost.ConvertToUint64();
	m_departedHostPending = true;
}

CSteamID OnlineSessionManager::selectSteamMigrationOwner() const
{
	if (m_kind != OnlineSessionKind::STEAM_LOBBY || !m_lobbyId.IsValid() ||
		SteamMatchmaking() == nullptr || SteamUser() == nullptr)
	{
		return CSteamID();
	}

	const CSteamID localSteamId = SteamUser()->GetSteamID();
	const std::map<std::uint64_t, int> pings =
		m_network == nullptr ? std::map<std::uint64_t, int>() : m_network->getSteamPeerPings();
	CSteamID fallback;
	CSteamID best;
	int bestPing = std::numeric_limits<int>::max();
	const int memberCount = SteamMatchmaking()->GetNumLobbyMembers(m_lobbyId);
	for (int index = 0; index < memberCount; ++index)
	{
		const CSteamID member = SteamMatchmaking()->GetLobbyMemberByIndex(m_lobbyId, index);
		if (!member.IsValid() || member == localSteamId)
		{
			continue;
		}
		if (!fallback.IsValid()) fallback = member;
		const auto ping = pings.find(member.ConvertToUint64());
		if (ping != pings.end() && ping->second >= 0 && ping->second < bestPing)
		{
			bestPing = ping->second;
			best = member;
		}
	}
	return best.IsValid() ? best : fallback;
}

void OnlineSessionManager::transferSteamLobbyOwnershipBeforeLeave()
{
	if (m_kind != OnlineSessionKind::STEAM_LOBBY || !m_host || !m_lobbyId.IsValid() ||
		SteamMatchmaking() == nullptr || SteamUser() == nullptr ||
		SteamMatchmaking()->GetLobbyOwner(m_lobbyId) != SteamUser()->GetSteamID())
	{
		return;
	}

	const CSteamID successor = selectSteamMigrationOwner();
	if (successor.IsValid() && SteamMatchmaking()->SetLobbyOwner(m_lobbyId, successor))
	{
		Debug::Info(
			"Steam lobby ownership transferred to %llu before leaving",
			static_cast<unsigned long long>(successor.ConvertToUint64()));
	}
}

void OnlineSessionManager::resetSessionState()
{
	m_resourceIntegrityRequired = false;
	m_resourceIntegritySession.cancelLocalCheck();
	m_resourceIntegritySession.clearTransport();
	m_kind = OnlineSessionKind::NONE;
	m_state = OnlineSessionState::IDLE;
	m_host = false;
	m_launchRequested = false;
	m_sceneLaunched = false;
	m_lobbyPasswordRequestId = 0;
	m_returnToMainMenuRequested = false;
	m_hostTransportLost = false;
	m_kickPending = false;
	m_kickReason.clear();
	m_migratingHost = false;
	m_departedHostPending = false;
	m_departedHost = OnlineHostDeparture();
	m_directPlayerCount = 1;
	m_directPlayerNames.clear();
	m_directNameSent = false;
	m_lobbyId.Clear();
	m_steamNetworkHost.Clear();
	m_migrationPreviousHost.Clear();
	m_migrationTargetHost.Clear();
	m_expectedDepartingHost.Clear();
	m_pendingJoinLobby.Clear();
	m_pendingPassword.clear();
	m_pendingLobbyName.clear();
	m_pendingLobbyLanguage.clear();
	m_lastError.clear();
}

void OnlineSessionManager::setError(const std::string& message)
{
	m_lastError = message.empty() ? NetworkText::get(NetworkText::Id::OnlineUnknownError) : message;
	m_state = OnlineSessionState::ERROR_STATUS;
	Debug::Error("OnlineSessionManager: %s", m_lastError.c_str());
}
