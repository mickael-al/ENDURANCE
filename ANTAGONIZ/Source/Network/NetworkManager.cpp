#include "NetworkManager.hpp"
#include "NetworkText.hpp"

#include "Debug.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <thread>

using namespace Ge;

namespace
{
	constexpr std::uint32_t MaximumMessageSize = 16u * 1024u * 1024u;
	std::atomic<std::uint64_t> NextNetworkManagerInstanceId{ 1 };
	constexpr std::uint32_t SteamAdmissionMagic = 0x454E4431u;
	constexpr std::uint32_t SteamAdmissionVersion = 1u;
	constexpr std::size_t MaximumSteamCredentialSize = 128;
	constexpr std::size_t MaximumPendingSteamBytes = 4u * 1024u * 1024u;
	constexpr std::size_t MaximumPendingSteamMessages = 4096;
	constexpr std::chrono::seconds SteamAdmissionTimeout(10);
	constexpr std::chrono::seconds SteamSendTimeout(10);
	constexpr int SteamSendFailureCloseReason = 1003;

	bool readMessageType(const FunctionRegistry::RawData& message, MessageType& type)
	{
		if (message.size() < sizeof(MessageType))
		{
			return false;
		}
		std::memcpy(&type, message.data(), sizeof(type));
		return true;
	}
}

NetworkManager::NetworkManager(Role networkRole, const std::string& ip, int port)
	: m_instanceId(NextNetworkManagerInstanceId.fetch_add(1)),
	  role(networkRole),
	  m_transport(NetworkTransport::DIRECT_IP),
	  server_ip(ip),
	  server_port(port)
{
	registerServerToClientFunction<int>("SetClientID", std::function<void(int)>([this](int clientId)
	{
		idClient = clientId;
		Debug::Log("Set client ID : %d", clientId);
	}));
}

NetworkManager::NetworkManager(Role networkRole, CSteamID remoteHost)
	: m_instanceId(NextNetworkManagerInstanceId.fetch_add(1)),
	  role(networkRole),
	  m_transport(NetworkTransport::STEAM_P2P),
	  m_remoteSteamHost(remoteHost)
{
	registerServerToClientFunction<int>("SetClientID", std::function<void(int)>([this](int clientId)
	{
		if (!isClient() || clientId <= 0 || m_steamServerConnection == k_HSteamNetConnection_Invalid)
			return;
		idClient = clientId;
		connectedToServeur = true;
		m_status = NetworkConnectionStatus::CONNECTED;
		Debug::Log("Steam admission accepted, client ID : %d", clientId);
	}));
}

void NetworkManager::configureSteamAdmission(
	std::function<bool(std::uint64_t, const std::string&)> validator,
	const std::string& credential,
	int capacity)
{
	if (m_running.load()) return;
	m_steamAdmissionValidator = std::move(validator);
	m_steamCredential = credential;
	m_steamCapacity = (std::max)(1, (std::min)(250, capacity));
}

void NetworkManager::setResourceIntegrityRequired(bool required)
{
	std::lock_guard<std::mutex> lock(m_resourceIntegrityMutex);
	m_resourceIntegrityValidatedClients.clear();
	m_resourceIntegrityRequired = required;
}

void NetworkManager::setClientResourceIntegrityValidated(int clientId, bool validated)
{
	if (clientId <= 0) return;
	std::lock_guard<std::mutex> lock(m_resourceIntegrityMutex);
	if (validated) m_resourceIntegrityValidatedClients.insert(clientId);
	else m_resourceIntegrityValidatedClients.erase(clientId);
}

bool NetworkManager::isClientResourceIntegrityValidated(int clientId) const
{
	if (!m_resourceIntegrityRequired.load()) return true;
	std::lock_guard<std::mutex> lock(m_resourceIntegrityMutex);
	return m_resourceIntegrityValidatedClients.find(clientId) != m_resourceIntegrityValidatedClients.end();
}

bool NetworkManager::resourceIntegrityAllowsRequest(int clientId, std::uint32_t functionHash, MessageType type) const
{
	if (clientId <= 0 || isClientResourceIntegrityValidated(clientId)) return true;
	static const std::uint32_t response = HashUtil::hash32("ResourceIntegrityResponse");
	static const std::uint32_t directName = HashUtil::hash32("RegisterDirectPlayerName");
	// Bootstrap is strictly client-to-server, including names which are not
	// registered at all on Steam. Never allow an unvalidated peer to relay it.
	return type == MessageType::SERVER_ONLY && (functionHash == response ||
		(m_transport == NetworkTransport::DIRECT_IP && functionHash == directName));
}

bool NetworkManager::resourceIntegrityAllowsSend(int clientId, const FunctionRegistry::RawData& framed) const
{
	if (isClientResourceIntegrityValidated(clientId)) return true;
	constexpr std::size_t offset = sizeof(std::uint32_t) + sizeof(MessageType) + sizeof(std::int32_t);
	if (framed.size() < offset + sizeof(std::uint32_t)) return false;
	std::uint32_t functionHash = 0;
	std::memcpy(&functionHash, framed.data() + offset, sizeof(functionHash));
	static const std::uint32_t allowed[] = {
		HashUtil::hash32("SetClientID"), HashUtil::hash32("StartGame"),
		HashUtil::hash32("SessionKicked"), HashUtil::hash32("SessionPlayerCount"),
		HashUtil::hash32("DirectPlayerName"), HashUtil::hash32("DirectPlayerLeft"),
		HashUtil::hash32("ResourceIntegrityChallenge"), HashUtil::hash32("ResourceIntegrityAccepted")
	};
	return std::find(std::begin(allowed), std::end(allowed), functionHash) != std::end(allowed);
}

NetworkManager::~NetworkManager()
{
	stop();
}

bool NetworkManager::start()
{
	if (m_running.load())
	{
		return true;
	}

	{
		std::lock_guard<std::mutex> lock(m_errorMutex);
		m_lastError.clear();
	}
	setResourceIntegrityRequired(m_resourceIntegrityRequired.load());
	m_status = NetworkConnectionStatus::STARTING;
	m_wasKicked = false;
	m_admissionRejected = false;
	m_pendingClientKicks.clear();
	idClient = -1;
	disconnectFromServeur = false;
	connectedToServeur = false;

	bool result = false;
	if (m_transport == NetworkTransport::STEAM_P2P)
	{
		result = isServer() ? startSteamServer() : startSteamClient();
	}
	else
	{
		result = isServer() ? startServer() : startClient();
	}

	m_running = result;
	if (!result)
	{
		const std::string error = m_status.load() == NetworkConnectionStatus::ERROR_STATUS
			? getLastError() : NetworkText::get(NetworkText::Id::NetworkTransportStartFailed);
		// Roll back partially created listeners, workers and callback registrations
		// before a retry can overwrite their handles. Preserve the failure for UI.
		stop();
		setError(error);
	}
	return result;
}

void NetworkManager::stop()
{
	if (!m_running.exchange(false) && m_status.load() == NetworkConnectionStatus::STOPPED)
	{
		return;
	}

	if (m_transport == NetworkTransport::STEAM_P2P)
	{
		stopSteam();
	}
	else
	{
		stopDirectIp();
	}

	{
		std::lock_guard<std::mutex> lock(m_clientMutex);
		clients.clear();
		recvBufferServer.clear();
		m_steamClients.clear();
		m_steamClientIds.clear();
		m_steamReceiveBuffers.clear();
	}
	m_pendingSteamAdmissions.clear();
	m_pendingSteamSends.clear();
	m_pendingSteamSendBytes.clear();
	m_failedSteamConnections.clear();
	{
		std::lock_guard<std::mutex> lock(m_callMutex);
		m_pendingClientCalls.clear();
		onClientDisconnect.clear();
		onClientConnect.clear();
	}
	{
		std::lock_guard<std::mutex> lock(m_executorMutex);
		executors.clear();
	}
	recvBufferClient.clear();
	onServeurDisconnectClient.clear();
	connectedToServeur = false;
	disconnectFromServeur = false;
	idClient = -1;
	m_status = NetworkConnectionStatus::STOPPED;
	m_pendingClientKicks.clear();
	setResourceIntegrityRequired(m_resourceIntegrityRequired.load());
}

bool NetworkManager::isConnected() const
{
	if (isServer())
	{
		return m_running.load() &&
			(m_status.load() == NetworkConnectionStatus::LISTENING ||
			 m_status.load() == NetworkConnectionStatus::CONNECTED);
	}
	return connectedToServeur.load();
}

int NetworkManager::getClientCount() const
{
	std::lock_guard<std::mutex> lock(m_clientMutex);
	return static_cast<int>(
		m_transport == NetworkTransport::STEAM_P2P ? m_steamClients.size() : clients.size());
}

std::uint64_t NetworkManager::getSteamPeerId(int clientId) const
{
	if (m_transport != NetworkTransport::STEAM_P2P || !isServer())
	{
		return 0;
	}

	ISteamNetworkingSockets* sockets = SteamNetworkingSockets();
	if (sockets == nullptr)
	{
		return 0;
	}

	std::lock_guard<std::mutex> lock(m_clientMutex);
	const auto client = m_steamClients.find(clientId);
	if (client == m_steamClients.end())
	{
		return 0;
	}

	SteamNetConnectionInfo_t connectionInfo = {};
	if (!sockets->GetConnectionInfo(client->second, &connectionInfo))
	{
		return 0;
	}
	return connectionInfo.m_identityRemote.GetSteamID64();
}

std::map<std::uint64_t, int> NetworkManager::getSteamPeerPings() const
{
	std::map<std::uint64_t, int> pings;
	if (m_transport != NetworkTransport::STEAM_P2P)
	{
		return pings;
	}

	ISteamNetworkingSockets* sockets = SteamNetworkingSockets();
	if (sockets == nullptr)
	{
		return pings;
	}

	auto readPing = [sockets, &pings](HSteamNetConnection connection)
	{
		if (connection == k_HSteamNetConnection_Invalid)
		{
			return;
		}

		SteamNetConnectionInfo_t connectionInfo = {};
		SteamNetConnectionRealTimeStatus_t status = {};
		if (!sockets->GetConnectionInfo(connection, &connectionInfo) ||
			sockets->GetConnectionRealTimeStatus(connection, &status, 0, nullptr) != k_EResultOK)
		{
			return;
		}

		const std::uint64_t steamId = connectionInfo.m_identityRemote.GetSteamID64();
		if (steamId != 0 && status.m_nPing >= 0)
		{
			pings[steamId] = status.m_nPing;
		}
	};

	std::lock_guard<std::mutex> lock(m_clientMutex);
	if (isServer())
	{
		for (const auto& client : m_steamClients)
		{
			readPing(client.second);
		}
	}
	else
	{
		readPing(m_steamServerConnection);
	}
	return pings;
}

std::string NetworkManager::getLastError() const
{
	std::lock_guard<std::mutex> lock(m_errorMutex);
	return m_lastError;
}

void NetworkManager::setError(const std::string& message)
{
	{
		std::lock_guard<std::mutex> lock(m_errorMutex);
		m_lastError = message;
	}
	m_status = NetworkConnectionStatus::ERROR_STATUS;
	Debug::Error("NetworkManager: %s", message.c_str());
}

void NetworkManager::update()
{
	if (m_transport == NetworkTransport::STEAM_P2P && m_running.load())
	{
		flushSteamSends();
		updateSteamAdmission();
		finishSteamFailures();
		receiveSteamMessages();
		finishSteamFailures();
	}

	std::vector<std::pair<int, std::function<void(int)>>> pendingCalls;
	{
		std::lock_guard<std::mutex> lock(m_callMutex);
		pendingCalls.swap(m_pendingClientCalls);
	}
	for (auto& call : pendingCalls)
	{
		if (!m_running.load()) return;
		call.second(call.first);
	}

	if (isClient() && disconnectFromServeur.exchange(false))
	{
		// A callback may stop the transport or register a listener for a later
		// connection; neither operation may invalidate this iteration.
		auto disconnectedCallbacks = std::move(onServeurDisconnectClient);
		onServeurDisconnectClient.clear();
		for (auto& function : disconnectedCallbacks)
		{
			if (!m_running.load()) return;
			function();
		}
	}

	std::vector<std::pair<int, FunctionRegistry::RawData>> pendingExecutors;
	{
		std::lock_guard<std::mutex> lock(m_executorMutex);
		pendingExecutors.swap(executors);
	}
	for (const auto& pending : pendingExecutors)
	{
		if (!m_running.load()) return;
		try
		{
			executeSerializedCall(pending.second, pending.first);
		}
		catch (const std::exception& exception)
		{
			Debug::Warn(
				"Rejected network call from client %d: %s",
				pending.first,
				exception.what());
		}
	}
	updateClientKicks();
	if (m_transport == NetworkTransport::STEAM_P2P) finishSteamFailures();
}

bool NetworkManager::startServer()
{
	const int listenFileDescriptor = server.createsocket(server_port, "0.0.0.0");
	if (listenFileDescriptor < 0)
	{
		setError(NetworkText::get(NetworkText::Id::NetworkDirectServerPortFailed));
		return false;
	}

	server.onConnection = [this](const hv::SocketChannelPtr& channel)
	{
		if (channel->isConnected())
		{
			int clientId = 0;
			{
				std::lock_guard<std::mutex> lock(m_clientMutex);
				clientId = nextClientId++;
				clients[clientId] = channel;
			}
			queueClientConnected(clientId);
			Debug::Log("[DIRECT SERVER] Client %d connected", clientId);
			return;
		}

		int disconnectedClient = -1;
		{
			std::lock_guard<std::mutex> lock(m_clientMutex);
			for (auto iterator = clients.begin(); iterator != clients.end(); ++iterator)
			{
				if (iterator->second == channel)
				{
					disconnectedClient = iterator->first;
					clients.erase(iterator);
					break;
				}
			}
			recvBufferServer.erase(channel);
		}
		if (disconnectedClient >= 0)
		{
			queueClientDisconnected(disconnectedClient);
			Debug::Log("[DIRECT SERVER] Client %d disconnected", disconnectedClient);
		}
	};

	server.onMessage = [this](const hv::SocketChannelPtr& channel, hv::Buffer* buffer)
	{
		const int sourceClientId = directClientId(channel);
		if (sourceClientId < 0)
		{
			return;
		}

		FunctionRegistry::RawData* receiveBuffer = nullptr;
		{
			std::lock_guard<std::mutex> lock(m_clientMutex);
			receiveBuffer = &recvBufferServer[channel];
			receiveBuffer->insert(
				receiveBuffer->end(),
				reinterpret_cast<std::uint8_t*>(buffer->data()),
				reinterpret_cast<std::uint8_t*>(buffer->data()) + buffer->size());
		}
		consumeServerBuffer(sourceClientId, *receiveBuffer);
	};

	server.max_connections = 32;
	server.host = "0.0.0.0";
	// A single IO worker keeps the per-channel framing buffers deterministic.
	server.setThreadNum(1);
	server.start();
	m_status = NetworkConnectionStatus::LISTENING;
	Debug::Log("Direct-IP server listening on port %d", server_port);

	if (role == Role::BOTH)
	{
		return startClient();
	}
	return true;
}

bool NetworkManager::startClient()
{
	m_status = NetworkConnectionStatus::CONNECTING;
	const int connectionFileDescriptor = client.createsocket(server_port, server_ip.c_str());
	if (connectionFileDescriptor < 0)
	{
		setError(NetworkText::get(NetworkText::Id::NetworkDirectClientSocketFailed));
		return false;
	}

	client.onConnection = [this](const hv::SocketChannelPtr& channel)
	{
		if (channel->isConnected())
		{
			connectedToServeur = true;
			m_status = NetworkConnectionStatus::CONNECTED;
			Debug::Log("[DIRECT CLIENT] Connected to server");
		}
		else
		{
			connectedToServeur = false;
			disconnectFromServeur = true;
			if (m_running.load())
			{
				setError(NetworkText::get(NetworkText::Id::NetworkDirectServerClosed));
			}
		}
	};

	client.onMessage = [this](const hv::SocketChannelPtr&, hv::Buffer* buffer)
	{
		recvBufferClient.insert(
			recvBufferClient.end(),
			reinterpret_cast<std::uint8_t*>(buffer->data()),
			reinterpret_cast<std::uint8_t*>(buffer->data()) + buffer->size());
		consumeClientBuffer(recvBufferClient);
	};

	constexpr int TimeoutMilliseconds = 3000;
	client.setConnectTimeout(TimeoutMilliseconds);
	client.start();

	int waited = 0;
	constexpr int IntervalMilliseconds = 50;
	while (!connectedToServeur.load() && waited < TimeoutMilliseconds)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(IntervalMilliseconds));
		waited += IntervalMilliseconds;
	}

	if (!connectedToServeur.load())
	{
		client.closesocket();
		setError(NetworkText::get(NetworkText::Id::NetworkDirectTimeout));
		return false;
	}
	return true;
}

bool NetworkManager::startSteamServer()
{
	// A Steam listener without a session policy must never admit gameplay peers.
	if (!m_steamAdmissionValidator)
	{
		setError(NetworkText::get(NetworkText::Id::OnlineAccessRefused));
		return false;
	}
	ISteamNetworkingSockets* sockets = SteamNetworkingSockets();
	if (sockets == nullptr)
	{
		setError(NetworkText::get(NetworkText::Id::NetworkSteamSocketsUnavailable));
		return false;
	}

	m_steamConnectionCallback.Register(this, &NetworkManager::onSteamConnectionStatusChanged);
	m_steamCallbackRegistered = true;
	m_steamPollGroup = sockets->CreatePollGroup();
	if (m_steamPollGroup == k_HSteamNetPollGroup_Invalid)
	{
		setError(NetworkText::get(NetworkText::Id::NetworkPollGroupFailed));
		return false;
	}

	m_steamListenSocket = sockets->CreateListenSocketP2P(0, 0, nullptr);
	if (m_steamListenSocket == k_HSteamListenSocket_Invalid)
	{
		setError(NetworkText::get(NetworkText::Id::NetworkListenSocketFailed));
		return false;
	}

	m_status = NetworkConnectionStatus::LISTENING;
	Debug::Log("Steam P2P host is listening");
	return true;
}

bool NetworkManager::startSteamClient()
{
	if (!m_remoteSteamHost.IsValid())
	{
		setError(NetworkText::get(NetworkText::Id::NetworkInvalidLobbyOwner));
		return false;
	}

	ISteamNetworkingSockets* sockets = SteamNetworkingSockets();
	if (sockets == nullptr)
	{
		setError(NetworkText::get(NetworkText::Id::NetworkSteamSocketsUnavailable));
		return false;
	}

	m_steamConnectionCallback.Register(this, &NetworkManager::onSteamConnectionStatusChanged);
	m_steamCallbackRegistered = true;
	SteamNetworkingIdentity identity;
	identity.Clear();
	identity.SetSteamID(m_remoteSteamHost);
	m_steamServerConnection = sockets->ConnectP2P(identity, 0, 0, nullptr);
	if (m_steamServerConnection == k_HSteamNetConnection_Invalid)
	{
		setError(NetworkText::get(NetworkText::Id::NetworkP2PConnectionFailed));
		return false;
	}

	m_steamAdmissionStarted = std::chrono::steady_clock::now();
	m_status = NetworkConnectionStatus::CONNECTING;
	Debug::Log("Connecting to Steam P2P host %llu", static_cast<unsigned long long>(m_remoteSteamHost.ConvertToUint64()));
	return true;
}

void NetworkManager::stopDirectIp()
{
	if (isClient())
	{
		client.stop();
	}
	if (isServer())
	{
		server.stop();
	}
}

void NetworkManager::stopSteam()
{
	ISteamNetworkingSockets* sockets = SteamNetworkingSockets();
	if (m_steamCallbackRegistered)
	{
		m_steamConnectionCallback.Unregister();
		m_steamCallbackRegistered = false;
	}
	if (sockets == nullptr)
	{
		return;
	}

	if (m_steamServerConnection != k_HSteamNetConnection_Invalid)
	{
		sockets->CloseConnection(m_steamServerConnection, 0, "Session closed", true);
		m_steamServerConnection = k_HSteamNetConnection_Invalid;
	}
	for (const auto& clientEntry : m_steamClients)
	{
		sockets->CloseConnection(clientEntry.second, 0, "Host closed", true);
	}
	for (const auto& pending : m_pendingSteamAdmissions)
	{
		sockets->CloseConnection(pending.first, 0, "Host closed", false);
	}
	if (m_steamListenSocket != k_HSteamListenSocket_Invalid)
	{
		sockets->CloseListenSocket(m_steamListenSocket);
		m_steamListenSocket = k_HSteamListenSocket_Invalid;
	}
	if (m_steamPollGroup != k_HSteamNetPollGroup_Invalid)
	{
		sockets->DestroyPollGroup(m_steamPollGroup);
		m_steamPollGroup = k_HSteamNetPollGroup_Invalid;
	}
}

void NetworkManager::onSteamConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* event)
{
	if (event == nullptr || m_transport != NetworkTransport::STEAM_P2P || !m_running.load())
	{
		return;
	}

	ISteamNetworkingSockets* sockets = SteamNetworkingSockets();
	if (sockets == nullptr)
	{
		return;
	}

	const HSteamNetConnection connection = event->m_hConn;
	const ESteamNetworkingConnectionState state = event->m_info.m_eState;
	const bool incoming = event->m_info.m_hListenSocket == m_steamListenSocket &&
		m_steamListenSocket != k_HSteamListenSocket_Invalid;

	if (incoming && state == k_ESteamNetworkingConnectionState_Connecting)
	{
		const std::uint64_t steamId = event->m_info.m_identityRemote.GetSteamID64();
		// Steam queues status callbacks. A previous route may already be dead
		// although its disconnect callback has not been dispatched yet.
		auto isClosed = [sockets](HSteamNetConnection peer)
		{
			SteamNetConnectionInfo_t info = {};
			return !sockets->GetConnectionInfo(peer, &info) ||
				info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer ||
				info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally;
		};
		std::vector<HSteamNetConnection> closed;
		for (const auto& pending : m_pendingSteamAdmissions)
			if (isClosed(pending.first)) closed.push_back(pending.first);
		{
			std::lock_guard<std::mutex> lock(m_clientMutex);
			for (const auto& peer : m_steamClients)
				if (isClosed(peer.second)) closed.push_back(peer.second);
		}
		for (const auto peer : closed) deferSteamFailure(peer, SteamSendFailureCloseReason);
		finishSteamFailures();

		bool duplicate = false;
		for (const auto& pending : m_pendingSteamAdmissions)
			duplicate = duplicate || pending.second.steamId == steamId;
		{
			std::lock_guard<std::mutex> lock(m_clientMutex);
			for (const auto& peer : m_steamClients)
			{
				SteamNetConnectionInfo_t info = {};
				if (sockets->GetConnectionInfo(peer.second, &info))
					duplicate = duplicate || info.m_identityRemote.GetSteamID64() == steamId;
			}
		}
		const std::size_t pendingLimit = static_cast<std::size_t>((std::min)(32, (std::max)(4, m_steamCapacity)));
		if (duplicate)
		{
			sockets->CloseConnection(connection, SteamDuplicateConnectionCloseReason,
				"Previous connection is still active", false);
			return;
		}
		if (steamId == 0 || !m_steamAdmissionValidator ||
			getClientCount() >= m_steamCapacity - 1 || m_pendingSteamAdmissions.size() >= pendingLimit)
		{
			sockets->CloseConnection(connection, SteamAdmissionCloseReason, "Session admission refused", false);
			return;
		}
		if (sockets->AcceptConnection(connection) != k_EResultOK ||
			!sockets->SetConnectionPollGroup(connection, m_steamPollGroup))
		{
			sockets->CloseConnection(connection, SteamAdmissionCloseReason, "Connection rejected", false);
			return;
		}
		m_pendingSteamAdmissions.emplace(connection,
			PendingSteamAdmission{ steamId, std::chrono::steady_clock::now() });
		return;
	}

	if (!incoming && connection == m_steamServerConnection &&
		state == k_ESteamNetworkingConnectionState_Connected)
	{
		// The socket alone is not a session: wait for the host's admission reply.
		sendSteamAdmission();
		return;
	}

	if (state != k_ESteamNetworkingConnectionState_ClosedByPeer &&
		state != k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
	{
		return;
	}

	if (connection == m_steamServerConnection)
	{
		connectedToServeur = false;
		disconnectFromServeur = true;
		m_wasKicked = event->m_info.m_eEndReason == VoteKickCloseReason;
		m_admissionRejected = event->m_info.m_eEndReason == SteamAdmissionCloseReason;
		m_steamServerConnection = k_HSteamNetConnection_Invalid;
		if (event->m_info.m_szEndDebug[0] != '\0')
		{
			Debug::Warn("Steam host disconnected: %s", event->m_info.m_szEndDebug);
		}
		setError(NetworkText::get(NetworkText::Id::NetworkSteamHostDisconnected));
	}
	else
	{
		int disconnectedClient = -1;
		{
			std::lock_guard<std::mutex> lock(m_clientMutex);
			const auto clientIterator = m_steamClientIds.find(connection);
			if (clientIterator != m_steamClientIds.end())
			{
				disconnectedClient = clientIterator->second;
				m_steamClients.erase(disconnectedClient);
				m_steamClientIds.erase(clientIterator);
			}
			m_steamReceiveBuffers.erase(connection);
		}
		if (disconnectedClient < 0 &&
			m_pendingSteamAdmissions.find(connection) == m_pendingSteamAdmissions.end()) return;
		if (disconnectedClient >= 0)
		{
			queueClientDisconnected(disconnectedClient);
		}
	}
	m_pendingSteamAdmissions.erase(connection);
	m_pendingSteamSends.erase(connection);
	m_pendingSteamSendBytes.erase(connection);
	m_failedSteamConnections.erase(connection);
	sockets->CloseConnection(connection, 0, nullptr, false);
}

void NetworkManager::receiveSteamMessages()
{
	ISteamNetworkingSockets* sockets = SteamNetworkingSockets();
	if (sockets == nullptr)
	{
		return;
	}

	SteamNetworkingMessage_t* messages[32] = {};
	if (isServer() && m_steamPollGroup != k_HSteamNetPollGroup_Invalid)
	{
		int count = 0;
		while ((count = sockets->ReceiveMessagesOnPollGroup(m_steamPollGroup, messages, 32)) > 0)
		{
			for (int index = 0; index < count; ++index)
			{
				SteamNetworkingMessage_t* message = messages[index];
				if (m_pendingSteamAdmissions.find(message->m_conn) != m_pendingSteamAdmissions.end())
				{
					receiveSteamAdmission(message->m_conn, message->m_pData, message->m_cbSize);
					message->Release();
					continue;
				}
				int sourceClientId = -1;
				{
					std::lock_guard<std::mutex> lock(m_clientMutex);
					const auto clientIterator = m_steamClientIds.find(message->m_conn);
					if (clientIterator != m_steamClientIds.end())
					{
						sourceClientId = clientIterator->second;
						FunctionRegistry::RawData& receiveBuffer = m_steamReceiveBuffers[message->m_conn];
						receiveBuffer.insert(
							receiveBuffer.end(),
							reinterpret_cast<std::uint8_t*>(message->m_pData),
							reinterpret_cast<std::uint8_t*>(message->m_pData) + message->m_cbSize);
					}
				}
				if (sourceClientId >= 0)
				{
					consumeServerBuffer(sourceClientId, m_steamReceiveBuffers[message->m_conn]);
				}
				message->Release();
			}
		}
	}
	else if (m_steamServerConnection != k_HSteamNetConnection_Invalid)
	{
		int count = 0;
		while ((count = sockets->ReceiveMessagesOnConnection(m_steamServerConnection, messages, 32)) > 0)
		{
			for (int index = 0; index < count; ++index)
			{
				SteamNetworkingMessage_t* message = messages[index];
				recvBufferClient.insert(
					recvBufferClient.end(),
					reinterpret_cast<std::uint8_t*>(message->m_pData),
					reinterpret_cast<std::uint8_t*>(message->m_pData) + message->m_cbSize);
				message->Release();
			}
			consumeClientBuffer(recvBufferClient);
		}
	}
}

void NetworkManager::consumeServerBuffer(int sourceClientId, FunctionRegistry::RawData& receiveBuffer)
{
	while (receiveBuffer.size() >= sizeof(std::uint32_t))
	{
		std::uint32_t size = 0;
		std::memcpy(&size, receiveBuffer.data(), sizeof(size));
		if (size > MaximumMessageSize)
		{
			receiveBuffer.clear();
			// A malformed client packet belongs to that client. It must not put
			// the host's entire transport into ERROR_STATUS.
			Debug::Warn(
				"Rejected oversized network message from client %d",
				sourceClientId);
			return;
		}
		if (receiveBuffer.size() < sizeof(size) + size)
		{
			return;
		}

		FunctionRegistry::RawData message(
			receiveBuffer.begin() + sizeof(size),
			receiveBuffer.begin() + sizeof(size) + size);
		FunctionRegistry::RawData framedMessage(
			receiveBuffer.begin(),
			receiveBuffer.begin() + sizeof(size) + size);
		receiveBuffer.erase(receiveBuffer.begin(), receiveBuffer.begin() + sizeof(size) + size);
		handleServerMessage(sourceClientId, message, framedMessage);
	}
}

void NetworkManager::consumeClientBuffer(FunctionRegistry::RawData& receiveBuffer)
{
	while (receiveBuffer.size() >= sizeof(std::uint32_t))
	{
		std::uint32_t size = 0;
		std::memcpy(&size, receiveBuffer.data(), sizeof(size));
		if (size > MaximumMessageSize)
		{
			receiveBuffer.clear();
			setError(NetworkText::get(NetworkText::Id::NetworkMessageTooLarge));
			return;
		}
		if (receiveBuffer.size() < sizeof(size) + size)
		{
			return;
		}

		FunctionRegistry::RawData message(
			receiveBuffer.begin() + sizeof(size),
			receiveBuffer.begin() + sizeof(size) + size);
		receiveBuffer.erase(receiveBuffer.begin(), receiveBuffer.begin() + sizeof(size) + size);
		queueSerializedCall(message);
	}
}

void NetworkManager::handleServerMessage(
	int sourceClientId,
	const FunctionRegistry::RawData& message,
	const FunctionRegistry::RawData& framedMessage)
{
	MessageType type;
	if (!readMessageType(message, type))
	{
		return;
	}
	const std::size_t functionOffset = sizeof(MessageType) + sizeof(std::int32_t);
	if (message.size() < functionOffset + sizeof(std::uint32_t))
	{
		return;
	}
	std::uint32_t functionHash = 0;
	std::memcpy(&functionHash, message.data() + functionOffset, sizeof(functionHash));
	if (!resourceIntegrityAllowsRequest(sourceClientId, functionHash, type)) return;
	NetworkFunctionAccess access = NetworkFunctionAccess::ANY;
	{
		std::lock_guard<std::mutex> lock(m_functionAccessMutex);
		const auto registered = m_functionAccess.find(functionHash);
		if (registered != m_functionAccess.end()) access = registered->second;
	}
	if (access == NetworkFunctionAccess::SERVER_TO_CLIENT)
	{
		Debug::Warn("Client %d attempted a server-owned network call", sourceClientId);
		return;
	}
	if (access == NetworkFunctionAccess::CLIENT_TO_SERVER &&
		type != MessageType::SERVER_ONLY)
	{
		Debug::Warn("Client %d used an invalid network-call direction", sourceClientId);
		return;
	}

	switch (type)
	{
	case MessageType::SERVER_ONLY:
		queueSerializedCall(message, sourceClientId);
		break;
	case MessageType::SERVER_CLIENT:
		queueSerializedCall(message, sourceClientId);
		if (m_transport == NetworkTransport::STEAM_P2P)
		{
			HSteamNetConnection ignored = k_HSteamNetConnection_Invalid;
			{
				std::lock_guard<std::mutex> lock(m_clientMutex);
				const auto iterator = m_steamClients.find(sourceClientId);
				if (iterator != m_steamClients.end()) ignored = iterator->second;
			}
			broadcast(ignored, framedMessage);
		}
		else
		{
			hv::SocketChannelPtr ignored;
			{
				std::lock_guard<std::mutex> lock(m_clientMutex);
				const auto iterator = clients.find(sourceClientId);
				if (iterator != clients.end()) ignored = iterator->second;
			}
			broadcast(ignored, framedMessage);
		}
		break;
	case MessageType::CLIENT_ONLY:
		if (m_transport == NetworkTransport::STEAM_P2P)
		{
			HSteamNetConnection ignored = k_HSteamNetConnection_Invalid;
			{
				std::lock_guard<std::mutex> lock(m_clientMutex);
				const auto iterator = m_steamClients.find(sourceClientId);
				if (iterator != m_steamClients.end()) ignored = iterator->second;
			}
			broadcast(ignored, framedMessage);
		}
		else
		{
			hv::SocketChannelPtr ignored;
			{
				std::lock_guard<std::mutex> lock(m_clientMutex);
				const auto iterator = clients.find(sourceClientId);
				if (iterator != clients.end()) ignored = iterator->second;
			}
			broadcast(ignored, framedMessage);
		}
		break;
	case MessageType::CLIENT_ONLY_TARGET:
		if (message.size() >= sizeof(MessageType) + sizeof(std::int32_t))
		{
			std::int32_t targetClient = -1;
			std::memcpy(&targetClient, message.data() + sizeof(MessageType), sizeof(targetClient));
			sendToClient(targetClient, framedMessage);
		}
		break;
	case MessageType::BRODCAST:
		// Clients are never allowed to issue an unrestricted broadcast.
		break;
	}
}

void NetworkManager::executeSerializedCall(
	const FunctionRegistry::RawData& message,
	int sourceClientId)
{
	// Both transports can disconnect after IO queues a command but before the
	// game thread consumes it. Connection IDs are never reused by this manager.
	if (sourceClientId > 0 && !hasClient(sourceClientId)) return;
	// Also reject commands already queued before the vote passed.
	if (sourceClientId > 0 && m_pendingClientKicks.find(sourceClientId) != m_pendingClientKicks.end()) return;
	const std::size_t headerSize = sizeof(MessageType) + sizeof(std::int32_t) + sizeof(std::uint32_t);
	if (message.size() < headerSize)
	{
		return;
	}

	const std::uint8_t* pointer = message.data() + sizeof(MessageType) + sizeof(std::int32_t);
	std::uint32_t hashName = 0;
	std::memcpy(&hashName, pointer, sizeof(hashName));
	MessageType type;
	if (!readMessageType(message, type) || !resourceIntegrityAllowsRequest(sourceClientId, hashName, type)) return;
	pointer += sizeof(hashName);
	FunctionRegistry::RawData arguments(pointer, message.data() + message.size());
	const int previousClientId = m_executingClientId;
	m_executingClientId = sourceClientId;
	try
	{
		registry.receive(hashName, arguments);
	}
	catch (...)
	{
		m_executingClientId = previousClientId;
		throw;
	}
	m_executingClientId = previousClientId;
}

void NetworkManager::queueSerializedCall(
	const FunctionRegistry::RawData& message,
	int sourceClientId)
{
	std::lock_guard<std::mutex> lock(m_executorMutex);
	executors.emplace_back(sourceClientId, message);
}

void NetworkManager::sendSteam(
	HSteamNetConnection connection,
	const FunctionRegistry::RawData& data,
	int sendFlags)
{
	if (connection == k_HSteamNetConnection_Invalid || data.empty() ||
		m_failedSteamConnections.find(connection) != m_failedSteamConnections.end()) return;
	ISteamNetworkingSockets* sockets = SteamNetworkingSockets();
	if (sockets == nullptr)
	{
		deferSteamFailure(connection, SteamSendFailureCloseReason);
		return;
	}
	const bool reliable = (sendFlags & k_nSteamNetworkingSend_Reliable) != 0;
	const auto queued = m_pendingSteamSends.find(connection);
	const bool hasBacklog = queued != m_pendingSteamSends.end() && !queued->second.empty();
	// Expiring voice/poses may be dropped; they must not compete with recovery.
	if (!reliable && hasBacklog) return;
	EResult result = k_EResultLimitExceeded;
	if (!hasBacklog)
		result = sockets->SendMessageToConnection(connection, data.data(),
			static_cast<std::uint32_t>(data.size()), sendFlags, nullptr);
	if (result == k_EResultOK) return;
	if (!reliable && (result == k_EResultLimitExceeded || result == k_EResultIgnored)) return;
	if (!reliable || result != k_EResultLimitExceeded)
	{
		deferSteamFailure(connection, SteamSendFailureCloseReason);
		return;
	}
	const std::size_t bytes = m_pendingSteamSendBytes[connection];
	if (data.size() > MaximumPendingSteamBytes - bytes ||
		m_pendingSteamSends[connection].size() >= MaximumPendingSteamMessages)
	{
		deferSteamFailure(connection, SteamSendFailureCloseReason);
		return;
	}
	m_pendingSteamSends[connection].push_back(
		PendingSteamSend{ data, sendFlags, std::chrono::steady_clock::now() });
	m_pendingSteamSendBytes[connection] = bytes + data.size();
}

void NetworkManager::flushSteamSends()
{
	ISteamNetworkingSockets* sockets = SteamNetworkingSockets();
	const auto now = std::chrono::steady_clock::now();
	for (auto& entry : m_pendingSteamSends)
	{
		if (m_failedSteamConnections.find(entry.first) != m_failedSteamConnections.end()) continue;
		auto& queue = entry.second;
		while (!queue.empty())
		{
			const PendingSteamSend& pending = queue.front();
			const auto peer = m_steamClientIds.find(entry.first);
			if (isServer() && peer != m_steamClientIds.end() &&
				!resourceIntegrityAllowsSend(peer->second, pending.data))
			{
				m_pendingSteamSendBytes[entry.first] -= pending.data.size();
				queue.pop_front();
				continue;
			}
			if (sockets == nullptr || now - pending.queued >= SteamSendTimeout)
			{
				deferSteamFailure(entry.first, SteamSendFailureCloseReason);
				break;
			}
			const EResult result = sockets->SendMessageToConnection(entry.first,
				pending.data.data(), static_cast<std::uint32_t>(pending.data.size()), pending.flags, nullptr);
			if (result == k_EResultLimitExceeded) break;
			if (result != k_EResultOK)
			{
				deferSteamFailure(entry.first, SteamSendFailureCloseReason);
				break;
			}
			m_pendingSteamSendBytes[entry.first] -= pending.data.size();
			queue.pop_front();
		}
	}
	for (auto it = m_pendingSteamSends.begin(); it != m_pendingSteamSends.end();)
	{
		if (it->second.empty())
		{
			m_pendingSteamSendBytes.erase(it->first);
			it = m_pendingSteamSends.erase(it);
		}
		else ++it;
	}
}

void NetworkManager::deferSteamFailure(HSteamNetConnection connection, int reason)
{
	m_failedSteamConnections.emplace(connection, reason);
}

void NetworkManager::finishSteamFailures()
{
	auto failures = std::move(m_failedSteamConnections);
	m_failedSteamConnections.clear();
	for (const auto& failed : failures)
	{
		const auto connection = failed.first;
		m_pendingSteamAdmissions.erase(connection);
		m_pendingSteamSends.erase(connection);
		m_pendingSteamSendBytes.erase(connection);
		if (connection == m_steamServerConnection)
		{
			m_steamServerConnection = k_HSteamNetConnection_Invalid;
			connectedToServeur = false;
			disconnectFromServeur = true;
			m_admissionRejected = failed.second == SteamAdmissionCloseReason;
			setError(NetworkText::get(m_admissionRejected ? NetworkText::Id::OnlineAccessRefused : NetworkText::Id::NetworkSteamHostDisconnected));
		}
		else
		{
			int clientId = -1;
			{
				std::lock_guard<std::mutex> lock(m_clientMutex);
				const auto peer = m_steamClientIds.find(connection);
				if (peer != m_steamClientIds.end())
				{
					clientId = peer->second;
					m_steamClients.erase(clientId);
					m_steamClientIds.erase(peer);
				}
				m_steamReceiveBuffers.erase(connection);
			}
			if (clientId >= 0) queueClientDisconnected(clientId);
		}
		if (ISteamNetworkingSockets* sockets = SteamNetworkingSockets())
			sockets->CloseConnection(connection, failed.second,
				failed.second == SteamAdmissionCloseReason ? "Session admission refused" : "Reliable send failed", false);
	}
}

void NetworkManager::sendSteamAdmission()
{
	if (m_steamCredential.size() > MaximumSteamCredentialSize)
	{
		deferSteamFailure(m_steamServerConnection, SteamAdmissionCloseReason);
		return;
	}
	const std::uint32_t header[] = { SteamAdmissionMagic, SteamAdmissionVersion,
		static_cast<std::uint32_t>(m_steamCredential.size()) };
	FunctionRegistry::RawData message(sizeof(header) + m_steamCredential.size());
	std::memcpy(message.data(), header, sizeof(header));
	std::memcpy(message.data() + sizeof(header), m_steamCredential.data(), m_steamCredential.size());
	sendSteam(m_steamServerConnection, message);
}

void NetworkManager::receiveSteamAdmission(HSteamNetConnection connection, const void* data, int size)
{
	const auto pending = m_pendingSteamAdmissions.find(connection);
	if (pending == m_pendingSteamAdmissions.end() ||
		m_failedSteamConnections.find(connection) != m_failedSteamConnections.end()) return;
	std::uint32_t header[3] = {};
	if (data == nullptr || size < static_cast<int>(sizeof(header)) ||
		size > static_cast<int>(sizeof(header) + MaximumSteamCredentialSize))
	{
		deferSteamFailure(connection, SteamAdmissionCloseReason);
		return;
	}
	std::memcpy(header, data, sizeof(header));
	if (header[0] != SteamAdmissionMagic || header[1] != SteamAdmissionVersion ||
		header[2] != static_cast<std::uint32_t>(size - sizeof(header)))
	{
		deferSteamFailure(connection, SteamAdmissionCloseReason);
		return;
	}
	const char* password = static_cast<const char*>(data) + sizeof(header);
	bool allowed = false;
	try
	{
		allowed = m_steamAdmissionValidator &&
			m_steamAdmissionValidator(pending->second.steamId, std::string(password, header[2]));
	}
	catch (...) { allowed = false; }
	if (!allowed || getClientCount() >= m_steamCapacity - 1)
	{
		deferSteamFailure(connection, SteamAdmissionCloseReason);
		return;
	}
	int clientId = 0;
	{
		std::lock_guard<std::mutex> lock(m_clientMutex);
		clientId = nextClientId++;
		m_steamClients[clientId] = connection;
		m_steamClientIds[connection] = clientId;
	}
	m_pendingSteamAdmissions.erase(pending);
	queueClientConnected(clientId);
}

void NetworkManager::updateSteamAdmission()
{
	const auto now = std::chrono::steady_clock::now();
	for (const auto& pending : m_pendingSteamAdmissions)
		if (now - pending.second.started >= SteamAdmissionTimeout)
			deferSteamFailure(pending.first, SteamAdmissionCloseReason);
	if (isClient() && m_steamServerConnection != k_HSteamNetConnection_Invalid &&
		!connectedToServeur.load() && now - m_steamAdmissionStarted >= SteamAdmissionTimeout)
		deferSteamFailure(m_steamServerConnection, SteamAdmissionCloseReason);
}

void NetworkManager::sendToServerUnreliable(const FunctionRegistry::RawData& data)
{
	if (m_transport == NetworkTransport::STEAM_P2P)
	{
		sendSteam(
			m_steamServerConnection,
			data,
			k_nSteamNetworkingSend_UnreliableNoNagle);
	}
	else if (client.isConnected())
	{
		client.send(data.data(), data.size());
	}
}

void NetworkManager::sendToClientUnreliable(
	int id,
	const FunctionRegistry::RawData& data)
{
	if (!resourceIntegrityAllowsSend(id, data)) return;
	std::lock_guard<std::mutex> lock(m_clientMutex);
	if (m_transport == NetworkTransport::STEAM_P2P)
	{
		const auto iterator = m_steamClients.find(id);
		if (iterator != m_steamClients.end())
		{
			sendSteam(
				iterator->second,
				data,
				k_nSteamNetworkingSend_UnreliableNoNagle);
		}
		return;
	}

	const auto iterator = clients.find(id);
	if (iterator != clients.end() && iterator->second->isConnected())
	{
		iterator->second->write(data.data(), data.size());
	}
}

void NetworkManager::broadcastUnreliable(const FunctionRegistry::RawData& data)
{
	std::lock_guard<std::mutex> lock(m_clientMutex);
	if (m_transport == NetworkTransport::STEAM_P2P)
	{
		for (const auto& entry : m_steamClients)
		{
			if (!resourceIntegrityAllowsSend(entry.first, data)) continue;
			sendSteam(
				entry.second,
				data,
				k_nSteamNetworkingSend_UnreliableNoNagle);
		}
		return;
	}

	for (const auto& entry : clients)
	{
		if (!resourceIntegrityAllowsSend(entry.first, data)) continue;
		if (entry.second->isConnected())
		{
			entry.second->write(data.data(), data.size());
		}
	}
}

void NetworkManager::sendToServer(const FunctionRegistry::RawData& data)
{
	if (m_transport == NetworkTransport::STEAM_P2P)
	{
		sendSteam(m_steamServerConnection, data);
	}
	else if (client.isConnected())
	{
		client.send(data.data(), data.size());
	}
}

void NetworkManager::sendToClient(int id, const FunctionRegistry::RawData& data)
{
	if (!resourceIntegrityAllowsSend(id, data)) return;
	std::lock_guard<std::mutex> lock(m_clientMutex);
	if (m_transport == NetworkTransport::STEAM_P2P)
	{
		const auto iterator = m_steamClients.find(id);
		if (iterator != m_steamClients.end()) sendSteam(iterator->second, data);
		return;
	}

	const auto iterator = clients.find(id);
	if (iterator != clients.end() && iterator->second->isConnected())
	{
		iterator->second->write(data.data(), data.size());
	}
}

void NetworkManager::broadcast(const FunctionRegistry::RawData& data)
{
	if (m_transport == NetworkTransport::STEAM_P2P)
	{
		broadcast(k_HSteamNetConnection_Invalid, data);
	}
	else
	{
		broadcast(hv::SocketChannelPtr(), data);
	}
}

void NetworkManager::broadcast(const hv::SocketChannelPtr& ignoredChannel, const FunctionRegistry::RawData& data)
{
	std::lock_guard<std::mutex> lock(m_clientMutex);
	for (const auto& entry : clients)
	{
		if (entry.second != ignoredChannel && entry.second->isConnected() &&
			resourceIntegrityAllowsSend(entry.first, data))
		{
			entry.second->write(data.data(), data.size());
		}
	}
}

void NetworkManager::broadcast(HSteamNetConnection ignoredConnection, const FunctionRegistry::RawData& data)
{
	std::lock_guard<std::mutex> lock(m_clientMutex);
	for (const auto& entry : m_steamClients)
	{
		if (entry.second != ignoredConnection && resourceIntegrityAllowsSend(entry.first, data))
		{
			sendSteam(entry.second, data);
		}
	}
}

bool NetworkManager::kickClient(int clientId)
{
	if (!isServer() || clientId <= 0) return false;
	{
		std::lock_guard<std::mutex> lock(m_clientMutex);
		const bool present = m_transport == NetworkTransport::STEAM_P2P
			? m_steamClients.find(clientId) != m_steamClients.end()
			: clients.find(clientId) != clients.end();
		if (!present) return false;
	}
	setClientResourceIntegrityValidated(clientId, false);
	m_pendingClientKicks.emplace(clientId,
		std::chrono::steady_clock::now() + std::chrono::milliseconds(250));
	return true;
}

void NetworkManager::updateClientKicks()
{
	const auto now = std::chrono::steady_clock::now();
	for (auto& kick : m_pendingClientKicks)
	{
		if (now < kick.second) continue;
		// Keep this monotonically allocated connection ID blocked until stop().
		kick.second = std::chrono::steady_clock::time_point::max();
		if (m_transport == NetworkTransport::STEAM_P2P)
		{
			HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
			{
				std::lock_guard<std::mutex> lock(m_clientMutex);
				const auto client = m_steamClients.find(kick.first);
				if (client == m_steamClients.end()) continue;
				connection = client->second;
				m_steamClients.erase(client);
				m_steamClientIds.erase(connection);
				m_steamReceiveBuffers.erase(connection);
			}
			if (ISteamNetworkingSockets* sockets = SteamNetworkingSockets())
				sockets->CloseConnection(connection, VoteKickCloseReason, "Vote kick", true);
			m_pendingSteamSends.erase(connection);
			m_pendingSteamSendBytes.erase(connection);
			m_failedSteamConnections.erase(connection);
			queueClientDisconnected(kick.first);
		}
		else
		{
			hv::SocketChannelPtr channel;
			{
				std::lock_guard<std::mutex> lock(m_clientMutex);
				const auto client = clients.find(kick.first);
				if (client != clients.end()) channel = client->second;
			}
			// The regular onConnection callback removes the peer and queues cleanup.
			if (channel != nullptr) channel->close(true);
		}
	}
}

void NetworkManager::queueClientConnected(int clientId)
{
	setClientResourceIntegrityValidated(clientId, false);
	std::lock_guard<std::mutex> lock(m_callMutex);
	m_pendingClientCalls.emplace_back(clientId, [this](int id)
	{
		if (!hasClient(id)) return;
		call(MessageType::CLIENT_ONLY_TARGET, id, "SetClientID", id);
	});
	for (const auto& callback : onClientConnect)
	{
		std::weak_ptr<std::function<void(int)>> listener = callback.second;
		m_pendingClientCalls.emplace_back(clientId, [this, listener](int id)
		{
			if (!hasClient(id)) return;
			if (const auto function = listener.lock()) (*function)(id);
		});
	}
}

void NetworkManager::queueClientDisconnected(int clientId)
{
	setClientResourceIntegrityValidated(clientId, false);
	std::lock_guard<std::mutex> lock(m_callMutex);
	m_pendingClientCalls.emplace_back(clientId, [this](int id)
	{
		// Resolve listeners on the game thread: IO can disconnect while a
		// connect listener is still installing that player's cleanup callbacks.
		std::vector<std::pair<std::string, std::weak_ptr<std::function<void(int)>>>> listeners;
		{
			std::lock_guard<std::mutex> lock(m_callMutex);
			const auto client = onClientDisconnect.find(id);
			if (client == onClientDisconnect.end()) return;
			for (const auto& callback : client->second)
				listeners.emplace_back(callback.first, callback.second);
		}
		for (const auto& listener : listeners)
		{
			const auto function = listener.second.lock();
			if (!function) continue;
			{
				std::lock_guard<std::mutex> lock(m_callMutex);
				const auto client = onClientDisconnect.find(id);
				if (client == onClientDisconnect.end()) continue;
				const auto current = client->second.find(listener.first);
				if (current == client->second.end() || current->second != function) continue;
				client->second.erase(current);
				if (client->second.empty()) onClientDisconnect.erase(client);
			}
			(*function)(id);
		}
	});
}

bool NetworkManager::hasClient(int clientId) const
{
	std::lock_guard<std::mutex> lock(m_clientMutex);
	return m_transport == NetworkTransport::STEAM_P2P
		? m_steamClients.find(clientId) != m_steamClients.end()
		: clients.find(clientId) != clients.end();
}

int NetworkManager::directClientId(const hv::SocketChannelPtr& channel) const
{
	std::lock_guard<std::mutex> lock(m_clientMutex);
	for (const auto& entry : clients)
	{
		if (entry.second == channel)
		{
			return entry.first;
		}
	}
	return -1;
}

void NetworkManager::addClientConnectFunction(const std::string& name, std::function<void(int)> function)
{
	std::lock_guard<std::mutex> lock(m_callMutex);
	onClientConnect[name] = std::make_shared<std::function<void(int)>>(std::move(function));
}

void NetworkManager::addClientDisconnectFunction(int clientId, const std::string& name, std::function<void(int)> function)
{
	std::lock_guard<std::mutex> lock(m_callMutex);
	onClientDisconnect[clientId][name] = std::make_shared<std::function<void(int)>>(std::move(function));
}

void NetworkManager::removeClientConnectFunction(const std::string& name)
{
	std::lock_guard<std::mutex> lock(m_callMutex);
	onClientConnect.erase(name);
}

void NetworkManager::removeClientDisconnectFunction(int clientId, const std::string& name)
{
	std::lock_guard<std::mutex> lock(m_callMutex);
	const auto clientIterator = onClientDisconnect.find(clientId);
	if (clientIterator == onClientDisconnect.end())
	{
		return;
	}
	clientIterator->second.erase(name);
	if (clientIterator->second.empty()) onClientDisconnect.erase(clientIterator);
}
