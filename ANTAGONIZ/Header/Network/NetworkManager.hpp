#ifndef __NETWORK_MANAGER__
#define __NETWORK_MANAGER__

#include <TcpClient.h>
#include <TcpServer.h>
#include <steam/steam_api.h>

#include "FunctionRegistry.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class HashUtil
{
public:
	static constexpr std::uint32_t DEFAULT_SEED32 = 0x811C9DC5u;
	static constexpr std::uint64_t DEFAULT_SEED64 = 0xcbf29ce484222325ULL;

	static std::uint32_t hash32(const std::string& value, std::uint32_t seed = DEFAULT_SEED32)
	{
		std::uint32_t hash = seed;
		for (unsigned char character : value)
		{
			hash ^= character;
			hash *= 16777619u;
		}
		return hash;
	}

	static std::uint64_t hash64(const std::string& value, std::uint64_t seed = DEFAULT_SEED64)
	{
		std::uint64_t hash = seed;
		for (unsigned char character : value)
		{
			hash ^= character;
			hash *= 1099511628211ULL;
		}
		return hash;
	}
};

enum class Role
{
	SERVER,
	CLIENT,
	BOTH
};

enum class MessageType
{
	SERVER_ONLY,
	SERVER_CLIENT,
	CLIENT_ONLY,
	CLIENT_ONLY_TARGET,
	BRODCAST,
};

enum class NetworkFunctionAccess
{
	ANY,
	CLIENT_TO_SERVER,
	SERVER_TO_CLIENT
};

enum class NetworkTransport
{
	DIRECT_IP,
	STEAM_P2P
};

enum class NetworkConnectionStatus
{
	STOPPED,
	STARTING,
	LISTENING,
	CONNECTING,
	CONNECTED,
	ERROR_STATUS
};

class NetworkManager
{
public:
	using FunctionWrapper = std::function<void(const std::vector<std::string>&)>;

	// Direct TCP/IP transport. The public API is kept compatible with the
	// original NetworkManager constructor.
	NetworkManager(Role role, const std::string& ip, int port);
	// Steam P2P transport. remoteHost is required for clients and ignored by hosts.
	NetworkManager(Role role, CSteamID remoteHost);
	~NetworkManager();

	NetworkManager(const NetworkManager&) = delete;
	NetworkManager& operator=(const NetworkManager&) = delete;

	bool start();
	void stop();
	void update();
	// Main-thread moderation. Block requests immediately, then let the notice flush.
	bool kickClient(int clientId);
	bool wasKicked() const { return m_wasKicked.load(); }
	// Session-level asset admission, independent of scene Behaviours. Enable
	// before start(); resetting it also revokes every previous peer verdict.
	void setResourceIntegrityRequired(bool required);
	void setClientResourceIntegrityValidated(int clientId, bool validated);
	bool isClientResourceIntegrityValidated(int clientId) const;
	static constexpr int VoteKickCloseReason = 1001;
	static constexpr int SteamAdmissionCloseReason = 1002;
	// A previous route may still be closing when the same member reconnects.
	static constexpr int SteamDuplicateConnectionCloseReason = 1004;
	bool wasAdmissionRejected() const { return m_admissionRejected.load(); }
	// Configure before start(). Capacity includes the host. Credentials travel
	// only over Steam's authenticated/encrypted connection, never lobby metadata.
	void configureSteamAdmission(
		std::function<bool(std::uint64_t, const std::string&)> validator,
		const std::string& credential,
		int capacity);

	bool isServer() const { return role == Role::SERVER || role == Role::BOTH; }
	bool isClient() const { return role == Role::CLIENT || role == Role::BOTH; }
	bool isConnected() const;
	bool isRunning() const { return m_running.load(); }
	int getClientId() const { return idClient.load(); }
	int getClientCount() const;
	std::uint64_t getInstanceId() const { return m_instanceId; }
	int getExecutingClientId() const { return m_executingClientId; }
	std::uint64_t getSteamPeerId(int clientId) const;
	std::map<std::uint64_t, int> getSteamPeerPings() const;
	NetworkTransport getTransport() const { return m_transport; }
	NetworkConnectionStatus getConnectionStatus() const { return m_status.load(); }
	std::string getLastError() const;

	template<typename... Args>
	void registerFunction(const std::string& name, std::function<void(Args...)> function)
	{
		registerFunctionWithAccess(
			name,
			std::move(function),
			NetworkFunctionAccess::ANY);
	}

	template<typename... Args>
	void registerClientToServerFunction(
		const std::string& name,
		std::function<void(Args...)> function)
	{
		registerFunctionWithAccess(
			name,
			std::move(function),
			NetworkFunctionAccess::CLIENT_TO_SERVER);
	}

	template<typename... Args>
	void registerServerToClientFunction(
		const std::string& name,
		std::function<void(Args...)> function)
	{
		registerFunctionWithAccess(
			name,
			std::move(function),
			NetworkFunctionAccess::SERVER_TO_CLIENT);
	}

	template<typename... Args>
	void call(MessageType type, int targetClientId, const std::string& name, Args... args)
	{
		const std::uint32_t hashName = HashUtil::hash32(name);
		FunctionRegistry::RawData argsData = registry.extract(hashName, args...);
		FunctionRegistry::RawData message;
		message.insert(message.end(), reinterpret_cast<const std::uint8_t*>(&type), reinterpret_cast<const std::uint8_t*>(&type) + sizeof(type));
		message.insert(message.end(), reinterpret_cast<const std::uint8_t*>(&targetClientId), reinterpret_cast<const std::uint8_t*>(&targetClientId) + sizeof(targetClientId));
		message.insert(message.end(), reinterpret_cast<const std::uint8_t*>(&hashName), reinterpret_cast<const std::uint8_t*>(&hashName) + sizeof(hashName));
		message.insert(message.end(), argsData.begin(), argsData.end());

		const std::uint32_t length = static_cast<std::uint32_t>(message.size());
		FunctionRegistry::RawData framedMessage;
		framedMessage.insert(framedMessage.end(), reinterpret_cast<const std::uint8_t*>(&length), reinterpret_cast<const std::uint8_t*>(&length) + sizeof(length));
		framedMessage.insert(framedMessage.end(), message.begin(), message.end());

		switch (type)
		{
		case MessageType::SERVER_ONLY:
			if (isServer())
			{
				executeSerializedCall(message);
			}
			else
			{
				sendToServer(framedMessage);
			}
			break;
		case MessageType::SERVER_CLIENT:
			if (isServer())
			{
				executeSerializedCall(message);
				broadcast(framedMessage);
			}
			else
			{
				sendToServer(framedMessage);
			}
			break;
		case MessageType::CLIENT_ONLY:
			if (role == Role::SERVER)
			{
				broadcast(framedMessage);
			}
			else
			{
				sendToServer(framedMessage);
			}
			break;
		case MessageType::CLIENT_ONLY_TARGET:
			if (isServer())
			{
				sendToClient(targetClientId, framedMessage);
			}
			else
			{
				sendToServer(framedMessage);
			}
			break;
		case MessageType::BRODCAST:
			if (isServer())
			{
				broadcast(framedMessage);
			}
			break;
		}
	}

	template<typename... Args>
	void call(MessageType type, const std::string& name, Args... args)
	{
		call(type, -1, name, args...);
	}

	// Voice and other short-lived real-time data should not wait behind
	// reliable Steam messages. Direct-IP sessions intentionally keep the TCP
	// fallback because that transport has no datagram socket.
	template<typename... Args>
	void callUnreliable(MessageType type, int targetClientId, const std::string& name, Args... args)
	{
		const std::uint32_t hashName = HashUtil::hash32(name);
		FunctionRegistry::RawData argsData = registry.extract(hashName, args...);
		FunctionRegistry::RawData message;
		message.insert(message.end(), reinterpret_cast<const std::uint8_t*>(&type), reinterpret_cast<const std::uint8_t*>(&type) + sizeof(type));
		message.insert(message.end(), reinterpret_cast<const std::uint8_t*>(&targetClientId), reinterpret_cast<const std::uint8_t*>(&targetClientId) + sizeof(targetClientId));
		message.insert(message.end(), reinterpret_cast<const std::uint8_t*>(&hashName), reinterpret_cast<const std::uint8_t*>(&hashName) + sizeof(hashName));
		message.insert(message.end(), argsData.begin(), argsData.end());

		const std::uint32_t length = static_cast<std::uint32_t>(message.size());
		FunctionRegistry::RawData framedMessage;
		framedMessage.insert(framedMessage.end(), reinterpret_cast<const std::uint8_t*>(&length), reinterpret_cast<const std::uint8_t*>(&length) + sizeof(length));
		framedMessage.insert(framedMessage.end(), message.begin(), message.end());

		switch (type)
		{
		case MessageType::SERVER_ONLY:
			if (isServer()) executeSerializedCall(message);
			else sendToServerUnreliable(framedMessage);
			break;
		case MessageType::SERVER_CLIENT:
			if (isServer())
			{
				executeSerializedCall(message);
				broadcastUnreliable(framedMessage);
			}
			else sendToServerUnreliable(framedMessage);
			break;
		case MessageType::CLIENT_ONLY:
			if (role == Role::SERVER) broadcastUnreliable(framedMessage);
			else sendToServerUnreliable(framedMessage);
			break;
		case MessageType::CLIENT_ONLY_TARGET:
			if (isServer()) sendToClientUnreliable(targetClientId, framedMessage);
			else sendToServerUnreliable(framedMessage);
			break;
		case MessageType::BRODCAST:
			if (isServer()) broadcastUnreliable(framedMessage);
			break;
		}
	}

	template<typename... Args>
	void callUnreliable(MessageType type, const std::string& name, Args... args)
	{
		callUnreliable(type, -1, name, args...);
	}

	template<typename... Args>
	void execf(const std::string& name, Args... args)
	{
		const std::uint32_t hashName = HashUtil::hash32(name);
		FunctionRegistry::RawData argsData = registry.extract(hashName, args...);
		registry.receive(hashName, argsData);
	}

	void addClientConnectFunction(const std::string& name, std::function<void(int)> function);
	void addClientDisconnectFunction(int clientId, const std::string& name, std::function<void(int)> function);
	void removeClientConnectFunction(const std::string& name);
	void removeClientDisconnectFunction(int clientId, const std::string& name);
	std::vector<std::function<void()>>& getOnServeurDisconnectClient() { return onServeurDisconnectClient; }

private:
	template<typename... Args>
	void registerFunctionWithAccess(
		const std::string& name,
		std::function<void(Args...)> function,
		NetworkFunctionAccess access)
	{
		const std::uint32_t hash = HashUtil::hash32(name);
		registry.registerFunction(hash, std::move(function));
		std::lock_guard<std::mutex> lock(m_functionAccessMutex);
		m_functionAccess[hash] = access;
	}

	bool startClient();
	bool startServer();
	bool startSteamClient();
	bool startSteamServer();
	void stopDirectIp();
	void stopSteam();
	void updateClientKicks();

	void executeSerializedCall(const FunctionRegistry::RawData& data, int sourceClientId = -1);
	void queueSerializedCall(const FunctionRegistry::RawData& data, int sourceClientId = -1);
	bool resourceIntegrityAllowsRequest(int clientId, std::uint32_t functionHash, MessageType type) const;
	bool resourceIntegrityAllowsSend(int clientId, const FunctionRegistry::RawData& framed) const;
	void handleServerMessage(int sourceClientId, const FunctionRegistry::RawData& message, const FunctionRegistry::RawData& framedMessage);
	void consumeServerBuffer(int sourceClientId, FunctionRegistry::RawData& receiveBuffer);
	void consumeClientBuffer(FunctionRegistry::RawData& receiveBuffer);

	void sendToServer(const FunctionRegistry::RawData& data);
	void sendToClient(int id, const FunctionRegistry::RawData& data);
	void broadcast(const FunctionRegistry::RawData& data);
	void broadcast(const hv::SocketChannelPtr& ignoredChannel, const FunctionRegistry::RawData& data);
	void broadcast(HSteamNetConnection ignoredConnection, const FunctionRegistry::RawData& data);
	void sendSteam(
		HSteamNetConnection connection,
		const FunctionRegistry::RawData& data,
		int sendFlags = k_nSteamNetworkingSend_Reliable);
	void receiveSteamMessages();
	void sendSteamAdmission();
	void receiveSteamAdmission(HSteamNetConnection connection, const void* data, int size);
	void updateSteamAdmission();
	void flushSteamSends();
	void deferSteamFailure(HSteamNetConnection connection, int reason);
	void finishSteamFailures();
	void sendToServerUnreliable(const FunctionRegistry::RawData& data);
	void sendToClientUnreliable(int id, const FunctionRegistry::RawData& data);
	void broadcastUnreliable(const FunctionRegistry::RawData& data);

	void queueClientConnected(int clientId);
	void queueClientDisconnected(int clientId);
	bool hasClient(int clientId) const;
	int directClientId(const hv::SocketChannelPtr& channel) const;
	void setError(const std::string& message);
	void onSteamConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* event);

private:
	std::uint64_t m_instanceId = 0;
	std::atomic<bool> m_resourceIntegrityRequired{ false };
	mutable std::mutex m_resourceIntegrityMutex;
	std::unordered_set<int> m_resourceIntegrityValidatedClients;
	Role role;
	NetworkTransport m_transport = NetworkTransport::DIRECT_IP;
	std::string server_ip;
	int server_port = 0;
	CSteamID m_remoteSteamHost;

	std::atomic<bool> m_running{ false };
	std::atomic<NetworkConnectionStatus> m_status{ NetworkConnectionStatus::STOPPED };
	mutable std::mutex m_errorMutex;
	std::string m_lastError;

	// Direct IP server.
	hv::TcpServer server;
	std::unordered_map<int, hv::SocketChannelPtr> clients;
	std::unordered_map<hv::SocketChannelPtr, FunctionRegistry::RawData> recvBufferServer;
	// Direct IP client.
	hv::TcpClient client;
	FunctionRegistry::RawData recvBufferClient;

	// Steam P2P state.
	HSteamListenSocket m_steamListenSocket = k_HSteamListenSocket_Invalid;
	HSteamNetPollGroup m_steamPollGroup = k_HSteamNetPollGroup_Invalid;
	HSteamNetConnection m_steamServerConnection = k_HSteamNetConnection_Invalid;
	std::unordered_map<int, HSteamNetConnection> m_steamClients;
	std::unordered_map<HSteamNetConnection, int> m_steamClientIds;
	std::unordered_map<HSteamNetConnection, FunctionRegistry::RawData> m_steamReceiveBuffers;
	CCallbackManual<NetworkManager, SteamNetConnectionStatusChangedCallback_t> m_steamConnectionCallback;
	bool m_steamCallbackRegistered = false;
	struct PendingSteamAdmission
	{
		std::uint64_t steamId = 0;
		std::chrono::steady_clock::time_point started;
	};
	struct PendingSteamSend
	{
		FunctionRegistry::RawData data;
		int flags = 0;
		std::chrono::steady_clock::time_point queued;
	};
	std::function<bool(std::uint64_t, const std::string&)> m_steamAdmissionValidator;
	std::string m_steamCredential;
	int m_steamCapacity = 1;
	std::atomic<bool> m_admissionRejected{ false };
	std::chrono::steady_clock::time_point m_steamAdmissionStarted;
	std::unordered_map<HSteamNetConnection, PendingSteamAdmission> m_pendingSteamAdmissions;
	std::unordered_map<HSteamNetConnection, std::deque<PendingSteamSend>> m_pendingSteamSends;
	std::unordered_map<HSteamNetConnection, std::size_t> m_pendingSteamSendBytes;
	// Defer removal until no broadcast/client map is being iterated.
	std::unordered_map<HSteamNetConnection, int> m_failedSteamConnections;

	using ClientCallback = std::shared_ptr<std::function<void(int)>>;
	// Deferred notifications keep weak references so removing a scene listener
	// also cancels notifications queued before its owner was destroyed.
	std::map<std::string, ClientCallback> onClientConnect;
	std::map<int, std::map<std::string, ClientCallback>> onClientDisconnect;
	mutable std::mutex m_clientMutex;
	std::mutex m_callMutex;
	std::vector<std::pair<int, std::function<void(int)>>> m_pendingClientCalls;
	int nextClientId = 1;

	std::atomic<int> idClient{ -1 };
	std::vector<std::function<void()>> onServeurDisconnectClient;
	std::atomic<bool> disconnectFromServeur{ false };
	std::atomic<bool> connectedToServeur{ false };

	FunctionRegistry registry;
	std::mutex m_functionAccessMutex;
	std::unordered_map<std::uint32_t, NetworkFunctionAccess> m_functionAccess;
	std::mutex m_executorMutex;
	std::vector<std::pair<int, FunctionRegistry::RawData>> executors;
	int m_executingClientId = -1;
	std::atomic<bool> m_wasKicked{ false };
	std::map<int, std::chrono::steady_clock::time_point> m_pendingClientKicks;
};

#endif //!__NETWORK_MANAGER__
