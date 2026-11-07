#include "NetworkManager.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>

using Bytes = FunctionRegistry::RawData;
template<class T> void append(Bytes& bytes, const T& value)
{
    const auto* p = reinterpret_cast<const std::uint8_t*>(&value);
    bytes.insert(bytes.end(), p, p + sizeof(value));
}
Bytes frame(const std::string& name, MessageType type, Bytes args = {}, int target = -1)
{
    Bytes bytes;
    const std::uint32_t length = sizeof(MessageType) + sizeof(std::int32_t) + sizeof(std::uint32_t) + args.size();
    append(bytes, length); append(bytes, type); append(bytes, std::int32_t(target));
    append(bytes, HashUtil::hash32(name)); bytes.insert(bytes.end(), args.begin(), args.end());
    return bytes;
}
Bytes admission(const std::string& password, std::uint32_t version = 1)
{
    Bytes bytes;
    append(bytes, std::uint32_t(0x454E4431u)); append(bytes, version);
    append(bytes, static_cast<std::uint32_t>(password.size()));
    bytes.insert(bytes.end(), password.begin(), password.end());
    return bytes;
}
void resetSteam()
{
    assert(mockCallbacks.empty());
    assert(mockSteam.inbox.empty());
    mockSteam = ISteamNetworkingSockets{};
}
void policy(NetworkManager& host, int capacity = 3)
{
    host.configureSteamAdmission([](std::uint64_t id, const std::string& password)
    { return id >= 1000 && id < 2000 && password == "secret"; }, "", capacity);
}
void admit(NetworkManager& host, unsigned handle, std::uint64_t id)
{
    mockSteam.incoming(handle, id);
    mockSteam.inject(handle, admission("secret"));
    host.update();
}


void directStaleWork()
{
    NetworkManager host(Role::SERVER, "127.0.0.1", 27015); assert(host.start());
    auto* server = hv::lastStartedServer;
    int connected = 0, invoked = 0, disconnected = 0;
    host.addClientConnectFunction("scene", [&](int id)
    {
        ++connected;
        host.addClientDisconnectFunction(id, "scene", [&](int) { ++disconnected; });
    });
    host.registerClientToServerFunction<>("Action", std::function<void()>([&] { ++invoked; }));
    auto first = std::make_shared<hv::SocketChannel>(); first->connected = true;
    // The IO thread can observe the whole connection before the next game frame.
    server->onConnection(first);
    hv::Buffer bytes(frame("Action", MessageType::SERVER_ONLY)); server->onMessage(first, &bytes);
    first->connected = false; server->onConnection(first);
    host.update();
    assert(connected == 0 && invoked == 0 && first->writes.empty());
    // An already announced client can also disappear with a queued command.
    auto second = std::make_shared<hv::SocketChannel>(); second->connected = true;
    server->onConnection(second); host.update(); assert(connected == 1);
    server->onMessage(second, &bytes);
    second->connected = false; server->onConnection(second);
    host.update(); host.update();
    assert(invoked == 0 && disconnected == 1 && host.getClientCount() == 0);
    auto third = std::make_shared<hv::SocketChannel>(); third->connected = true;
    host.addClientConnectFunction("scene", [&](int id)
    {
        ++connected;
        // Deterministic IO/main interleaving: disconnection precedes listener registration.
        third->connected = false; server->onConnection(third);
        host.addClientDisconnectFunction(id, "scene", [&](int) { ++disconnected; });
    });
    server->onConnection(third); host.update(); host.update();
    assert(connected == 2 && disconnected == 2);
}

void canceledListeners()
{
    NetworkManager host(Role::SERVER, "127.0.0.1", 27015); assert(host.start());
    auto* server = hv::lastStartedServer;
    int oldCalls = 0, newCalls = 0, disconnectCalls = 0;
    host.addClientConnectFunction("scene", [&](int) { ++oldCalls; });
    auto first = std::make_shared<hv::SocketChannel>(); first->connected = true;
    server->onConnection(first);
    // Scene teardown removes a listener after IO queued it, before game update.
    host.removeClientConnectFunction("scene");
    host.addClientConnectFunction("scene", [&](int) { ++newCalls; });
    host.update(); assert(oldCalls == 0 && newCalls == 0);
    auto second = std::make_shared<hv::SocketChannel>(); second->connected = true;
    server->onConnection(second); host.update(); assert(newCalls == 1);
    host.addClientDisconnectFunction(1, "scene", [&](int) { ++disconnectCalls; });
    first->connected = false; server->onConnection(first);
    host.removeClientDisconnectFunction(1, "scene");
    host.update(); assert(disconnectCalls == 0);
    // stop() during a listener must cancel other work already drained this frame.
    host.addClientConnectFunction("a_stop", [&](int) { host.stop(); });
    auto third = std::make_shared<hv::SocketChannel>(); third->connected = true;
    server->onConnection(third); host.update(); assert(newCalls == 1);
}

void steamHandleIsolation()
{
    NetworkManager host(Role::SERVER, CSteamID{}); policy(host); assert(host.start());
    admit(host, 20, 1001);
    const auto before = mockSteam.closed.size();
    // Steam status callbacks are global: a different subsystem owns handle 900.
    mockSteam.state(900, k_ESteamNetworkingConnectionState_ClosedByPeer);
    assert(mockSteam.closed.size() == before && host.getClientCount() == 1);
    NetworkManager client(Role::CLIENT, CSteamID(123));
    client.configureSteamAdmission({}, "secret", 3); assert(client.start());
    mockSteam.state(500, k_ESteamNetworkingConnectionState_ClosedByPeer);
    assert(std::count(mockSteam.closed.begin(), mockSteam.closed.end(), 500) == 1);
    mockSteam.state(20, k_ESteamNetworkingConnectionState_ClosedByPeer);
    mockSteam.state(20, k_ESteamNetworkingConnectionState_ClosedByPeer);
    assert(std::count(mockSteam.closed.begin(), mockSteam.closed.end(), 20) == 1);
    assert(host.getClientCount() == 0 && host.isConnected());
}

void failedStartupCleanup()
{
    {
        NetworkManager host(Role::SERVER, CSteamID{}); policy(host);
        mockSteam.failPollGroup = true;
        assert(!host.start()); assert(mockCallbacks.empty());
        assert(host.getConnectionStatus() == NetworkConnectionStatus::ERROR_STATUS);
        mockSteam.failPollGroup = false; mockSteam.failListen = true;
        assert(!host.start()); assert(mockCallbacks.empty());
        assert(mockSteam.createdPollGroups == mockSteam.destroyedPollGroups);
        mockSteam.failListen = false;
        assert(host.start()); host.stop();
        assert(mockSteam.createdPollGroups == mockSteam.destroyedPollGroups);
    }
    resetSteam();
    NetworkManager client(Role::CLIENT, CSteamID(123));
    client.configureSteamAdmission({}, "secret", 3); mockSteam.failConnect = true;
    assert(!client.start()); assert(mockCallbacks.empty());
    assert(client.getConnectionStatus() == NetworkConnectionStatus::ERROR_STATUS);
    mockSteam.failConnect = false; assert(client.start()); client.stop();
    assert(mockSteam.closed.size() == 1 && mockCallbacks.empty());
}

void duplicateReconnect()
{
    NetworkManager host(Role::SERVER, CSteamID{}); policy(host); assert(host.start());
    admit(host, 20, 1001);
    mockSteam.incoming(21, 1001);
    assert(mockSteam.closeReasons[21] != NetworkManager::SteamAdmissionCloseReason);
    assert(host.getClientCount() == 1); // A live duplicate must not displace its peer.
    NetworkManager client(Role::CLIENT, CSteamID(123));
    client.configureSteamAdmission({}, "secret", 3); assert(client.start());
    mockSteam.state(500, k_ESteamNetworkingConnectionState_ClosedByPeer, mockSteam.closeReasons[21]);
    assert(!client.wasAdmissionRejected() && !client.wasKicked());
    int disconnected = 0;
    host.addClientDisconnectFunction(1, "old", [&](int) { ++disconnected; });
    // The terminal state is observable before its queued status callback arrives.
    mockSteam.peers[20].m_eState = k_ESteamNetworkingConnectionState_ClosedByPeer;
    admit(host, 22, 1001);
    assert(host.getClientCount() == 1 && host.getSteamPeerId(2) == 1001 && disconnected == 1);
    mockSteam.state(20, k_ESteamNetworkingConnectionState_ClosedByPeer); host.update();
    assert(host.getClientCount() == 1 && disconnected == 1);
}


void concurrentDirectRegistration()
{
    NetworkManager host(Role::SERVER, "127.0.0.1", 27015); assert(host.start());
    auto* server = hv::lastStartedServer;
    host.registerClientToServerFunction<>("Action", std::function<void()>([] {}));
    std::atomic<bool> begin{false};
    std::thread io([&]
    {
        while (!begin.load()) std::this_thread::yield();
        for (int i = 0; i < 500; ++i)
        {
            auto channel = std::make_shared<hv::SocketChannel>(); channel->connected = true;
            server->onConnection(channel);
            hv::Buffer bytes(frame("Action", MessageType::SERVER_ONLY)); server->onMessage(channel, &bytes);
            channel->connected = false; server->onConnection(channel);
        }
    });
    begin = true;
    for (int i = 0; i < 500; ++i)
    {
        host.registerClientToServerFunction<>("Action", std::function<void()>([] {}));
        host.addClientConnectFunction("scene", [](int) {});
        host.addClientDisconnectFunction(i + 1, "scene", [](int) {});
        host.removeClientConnectFunction("scene");
        host.removeClientDisconnectFunction(i + 1, "scene");
    }
    io.join(); host.update();
    assert(host.getClientCount() == 0 && host.isConnected());
}

void disconnectListenerMutation()
{
    {
        NetworkManager client(Role::CLIENT, CSteamID(123));
        client.configureSteamAdmission({}, "secret", 3); assert(client.start());
        int calls = 0;
        client.getOnServeurDisconnectClient().push_back([&]
        {
            ++calls;
            client.getOnServeurDisconnectClient().push_back([&] { ++calls; });
        });
        mockSteam.state(500, k_ESteamNetworkingConnectionState_ClosedByPeer);
        client.update();
        assert(calls == 1 && client.getOnServeurDisconnectClient().size() == 1);
        client.stop(); assert(client.getOnServeurDisconnectClient().empty());
    }
    resetSteam();
    NetworkManager client(Role::CLIENT, CSteamID(123));
    client.configureSteamAdmission({}, "secret", 3); assert(client.start());
    int staleCalls = 0;
    client.getOnServeurDisconnectClient().push_back([&] { client.stop(); });
    client.getOnServeurDisconnectClient().push_back([&] { ++staleCalls; });
    mockSteam.state(500, k_ESteamNetworkingConnectionState_ClosedByPeer);
    client.update(); assert(staleCalls == 0);
}

void releaseRegression(const std::string& name)
{
    resetSteam();
    if (name == "direct_stale") directStaleWork();
    else if (name == "listeners") canceledListeners();
    else if (name == "handles") steamHandleIsolation();
    else if (name == "startup") failedStartupCleanup();
    else if (name == "reconnect") duplicateReconnect();
    else if (name == "concurrent") concurrentDirectRegistration();
    else if (name == "disconnect_mutation") disconnectListenerMutation();
    else assert(false && "Unknown regression case");
    std::cout << "PASS release regression: " << name << '\n';
}

int main(int argc, char** argv)
{
    if (argc == 2) { releaseRegression(argv[1]); return 0; }
    for (const auto* name : { "direct_stale", "listeners", "handles", "startup", "reconnect", "concurrent", "disconnect_mutation" })
        releaseRegression(name);
    resetSteam();
    {
        NetworkManager unconfigured(Role::SERVER, CSteamID{});
        assert(!unconfigured.start()); // A forgotten policy fails closed.
    }
    resetSteam();
    {
        NetworkManager host(Role::SERVER, CSteamID{}); policy(host); assert(host.start());
        int connected = 0, invoked = 0, caller = -1, protectedCalls = 0;
        host.addClientConnectFunction("test", [&](int) { ++connected; });
        host.registerClientToServerFunction<int>("Request", std::function<void(int)>([&](int n)
        { invoked += n; caller = host.getExecutingClientId(); }));
        host.registerServerToClientFunction<int>("State", std::function<void(int)>([&](int) { ++protectedCalls; }));
        host.registerServerToClientFunction<>("Ready", std::function<void()>([] {}));
        Bytes argument; append(argument, 3);

        mockSteam.incoming(10, 9999); host.update();
        assert(host.getClientCount() == 0 && connected == 0 && mockSteam.sent.empty());
        mockSteam.inject(10, admission("secret")); host.update();
        assert(mockSteam.closeReasons[10] == NetworkManager::SteamAdmissionCloseReason);
        assert(host.getClientCount() == 0 && mockSteam.sent.empty());

        mockSteam.incoming(11, 1001);
        mockSteam.inject(11, frame("Request", MessageType::SERVER_ONLY, argument)); host.update();
        assert(invoked == 0 && connected == 0 && mockSteam.sent.empty());
        assert(mockSteam.closeReasons[11] == NetworkManager::SteamAdmissionCloseReason);
        mockSteam.incoming(12, 1001); mockSteam.inject(12, admission("wrong")); host.update();
        mockSteam.incoming(13, 1001); mockSteam.inject(13, admission("secret", 2)); host.update();
        assert(host.getClientCount() == 0 && mockSteam.sent.empty());
        // A different game sharing the development App ID must not be admitted.
        auto otherGame = admission("secret");
        const std::uint32_t previousGameMagic = 0x50415332u;
        std::memcpy(otherGame.data(), &previousGameMagic, sizeof(previousGameMagic));
        mockSteam.incoming(14, 1001); mockSteam.inject(14, otherGame); host.update();
        assert(mockSteam.closeReasons[14] == NetworkManager::SteamAdmissionCloseReason);
        assert(host.getClientCount() == 0 && mockSteam.sent.empty());

        admit(host, 20, 1001);
        assert(host.getClientCount() == 1 && connected == 1 && host.getSteamPeerId(1) == 1001);
        assert(mockSteam.sent.size() == 1); // SetClientID, only after authorization.
        const auto acceptCount = mockSteam.accepted.size();
        mockSteam.incoming(21, 1001); host.update();
        assert(mockSteam.accepted.size() == acceptCount); // Duplicate identity.
        admit(host, 22, 1002); assert(host.getClientCount() == 2);
        mockSteam.incoming(23, 1003); host.update();
        assert(mockSteam.closeReasons[23] == NetworkManager::SteamAdmissionCloseReason);
        assert(host.getClientCount() == 2); // Total capacity includes host.

        mockSteam.inject(20, frame("Request", MessageType::SERVER_ONLY, argument)); host.update();
        assert(invoked == 3 && caller == 1);
        mockSteam.inject(20, frame("State", MessageType::SERVER_ONLY, argument));
        mockSteam.inject(20, frame("Request", MessageType::CLIENT_ONLY, argument));
        mockSteam.inject(20, frame("Request", MessageType::SERVER_ONLY, Bytes{1}));
        auto trailing = argument; trailing.push_back(0);
        mockSteam.inject(20, frame("Request", MessageType::SERVER_ONLY, trailing)); host.update();
        assert(protectedCalls == 0 && invoked == 3);
        Bytes oversized; append(oversized, std::uint32_t(16 * 1024 * 1024 + 1));
        mockSteam.inject(20, oversized); host.update(); assert(host.isConnected());
        mockSteam.inject(20, frame("Request", MessageType::SERVER_ONLY, argument)); host.update();
        assert(invoked == 6);

        mockSteam.sent.clear(); mockSteam.nextSendResult = k_EResultLimitExceeded;
        host.call(MessageType::CLIENT_ONLY_TARGET, 1, "State", 42);
        host.call(MessageType::CLIENT_ONLY_TARGET, 1, "Ready");
        host.callUnreliable(MessageType::CLIENT_ONLY_TARGET, 1, "State", 99);
        assert(mockSteam.sent.size() == 1); // ACK and ephemeral traffic cannot overtake.
        host.update();
        assert(mockSteam.sent.size() == 3);
        assert(mockSteam.sent[0].result == k_EResultLimitExceeded);
        assert(mockSteam.sent[1].result == k_EResultOK && mockSteam.sent[2].result == k_EResultOK);
        assert(mockSteam.sent[0].bytes == mockSteam.sent[1].bytes);
        // Verify the ACK follows the successful state retry.
        std::uint32_t readyHash = 0;
        std::memcpy(&readyHash, mockSteam.sent[2].bytes.data() + 12, sizeof(readyHash));
        assert(readyHash == HashUtil::hash32("Ready"));

        int disconnected = 0;
        host.addClientDisconnectFunction(1, "test", [&](int id) { assert(id == 1); ++disconnected; });
        mockSteam.state(20, k_ESteamNetworkingConnectionState_ClosedByPeer);
        host.update(); host.update();
        assert(disconnected == 1 && host.getClientCount() == 1);

        int failedPeerDisconnected = 0;
        host.addClientDisconnectFunction(2, "test", [&](int) { ++failedPeerDisconnected; });
        mockSteam.sent.clear(); mockSteam.nextSendResult = k_EResultInvalidState;
        host.call(MessageType::CLIENT_ONLY_TARGET, 2, "State", 7);
        host.call(MessageType::CLIENT_ONLY_TARGET, 2, "Ready");
        mockSteam.inject(22, frame("Request", MessageType::SERVER_ONLY, argument));
        host.update(); host.update();
        assert(mockSteam.sent.size() == 1 && failedPeerDisconnected == 1);
        assert(host.getClientCount() == 0 && host.isConnected() && invoked == 6);
        host.stop(); assert(mockCallbacks.empty());
    }
    std::cout << "PASS admission, identity/capacity, RPC guards, reliable retry ordering, fatal send and disconnect cleanup\n";
    resetSteam();
    {
        NetworkManager host(Role::SERVER, CSteamID{}); policy(host, 250); assert(host.start());
        for (unsigned i = 0; i < 40; ++i) mockSteam.incoming(100 + i, 1000 + i);
        assert(mockSteam.accepted.size() == 32); // Bounded unauthenticated sockets.
        host.stop();
        assert(mockSteam.closed.size() == 40);
    }
    resetSteam();
    {
        NetworkManager client(Role::CLIENT, CSteamID(123));
        client.configureSteamAdmission({}, "secret", 3); assert(client.start());
        mockSteam.state(500, k_ESteamNetworkingConnectionState_Connected);
        client.update();
        assert(!client.isConnected() && client.getClientId() == -1);
        assert(mockSteam.sent.size() == 1 && mockSteam.sent[0].bytes == admission("secret"));
        Bytes id; append(id, 7);
        mockSteam.inject(500, frame("SetClientID", MessageType::CLIENT_ONLY_TARGET, id)); client.update();
        assert(client.isConnected() && client.getClientId() == 7);
        mockSteam.state(500, k_ESteamNetworkingConnectionState_ClosedByPeer, NetworkManager::SteamAdmissionCloseReason);
        client.update(); assert(!client.isConnected() && client.wasAdmissionRejected());
    }
    std::cout << "PASS bounded pending peers and client readiness only after host admission\n";
    resetSteam();
    {
        NetworkManager host(Role::SERVER, CSteamID{}); policy(host); assert(host.start()); admit(host, 70, 1001);
        host.registerServerToClientFunction<int>("State", std::function<void(int)>([](int) {}));
        mockSteam.nextSendResult = k_EResultLimitExceeded;
        for (int i = 0; i < 4200; ++i) host.call(MessageType::CLIENT_ONLY_TARGET, 1, "State", i);
        host.update(); assert(host.getClientCount() == 0 && host.isConnected());
    }
    std::cout << "PASS bounded reliable backlog closes affected peer\n";
    resetSteam();
    {
        NetworkManager host(Role::SERVER, CSteamID{}); policy(host); assert(host.start()); admit(host, 75, 1001);
        host.registerServerToClientFunction<std::string>("Blob", std::function<void(std::string)>([](std::string) {}));
        mockSteam.nextSendResult = k_EResultLimitExceeded;
        const std::string payload(1024 * 1024, 'x');
        for (int i = 0; i < 5; ++i) host.call(MessageType::CLIENT_ONLY_TARGET, 1, "Blob", payload);
        host.update(); assert(host.getClientCount() == 0 && host.isConnected());
    }
    std::cout << "PASS reliable backlog byte bound\n";
    resetSteam();
    {
        NetworkManager host(Role::SERVER, CSteamID{}); policy(host); assert(host.start()); admit(host, 80, 1001);
        mockSteam.incoming(81, 1002);
        host.registerServerToClientFunction<int>("State", std::function<void(int)>([](int) {}));
        mockSteam.nextSendResult = k_EResultLimitExceeded;
        host.call(MessageType::CLIENT_ONLY_TARGET, 1, "State", 1);
        NetworkManager client(Role::CLIENT, CSteamID(123));
        client.configureSteamAdmission({}, "secret", 3); assert(client.start());
        std::this_thread::sleep_for(std::chrono::milliseconds(10050));
        host.update();
        assert(mockSteam.closeReasons[81] == NetworkManager::SteamAdmissionCloseReason);
        assert(host.getClientCount() == 0 && host.isConnected());
        client.update(); assert(!client.isConnected() && client.wasAdmissionRejected());
    }
    std::cout << "PASS admission and reliable backlog deadlines\n";
    resetSteam();
    {
        NetworkManager host(Role::SERVER, CSteamID{}); policy(host, 4);
        host.setResourceIntegrityRequired(true); assert(host.start());
        int gameplay = 0, responseSender = -1, relay = 0, unexpectedDirectName = 0;
        host.registerClientToServerFunction<int>("GameplayHello", std::function<void(int)>([&](int n) { gameplay += n; }));
        host.registerFunction<int>("Relay", std::function<void(int)>([&](int n) { relay += n; }));
        host.registerServerToClientFunction<int>("State", std::function<void(int)>([](int) {}));
        host.registerServerToClientFunction<int>("ResourceIntegrityChallenge", std::function<void(int)>([](int) {}));
        // An RPC payload cannot choose its authenticated connection identity.
        host.registerClientToServerFunction<int>("ResourceIntegrityResponse", std::function<void(int)>([&](int)
        { responseSender = host.getExecutingClientId(); host.setClientResourceIntegrityValidated(responseSender, false); }));
        admit(host, 90, 1001); admit(host, 91, 1002); admit(host, 92, 1003);
        assert(host.getClientCount() == 3 && !host.isClientResourceIntegrityValidated(1));
        Bytes argument; append(argument, 2);
        mockSteam.sent.clear();
        for (auto type : { MessageType::SERVER_ONLY, MessageType::SERVER_CLIENT, MessageType::CLIENT_ONLY, MessageType::CLIENT_ONLY_TARGET })
            mockSteam.inject(90, frame("Relay", type, argument, 2));
        mockSteam.inject(90, frame("GameplayHello", MessageType::SERVER_ONLY, argument)); host.update();
        assert(gameplay == 0 && relay == 0 && mockSteam.sent.empty());
        mockSteam.inject(90, frame("ResourceIntegrityResponse", MessageType::SERVER_ONLY, argument)); host.update();
        assert(responseSender == 1 && !host.isClientResourceIntegrityValidated(2));
        host.call(MessageType::CLIENT_ONLY_TARGET, 1, "ResourceIntegrityChallenge", 1);
        assert(mockSteam.sent.size() == 1 && mockSteam.sent[0].conn == 90);

        // Bootstrap names cannot be used to relay arbitrary payloads. The Steam
        // host does not even register the Direct-IP name RPC, so direction must
        // be enforced by admission itself, not only by RPC registration.
        host.setClientResourceIntegrityValidated(2, true);
        mockSteam.sent.clear();
        for (auto type : { MessageType::SERVER_CLIENT, MessageType::CLIENT_ONLY, MessageType::CLIENT_ONLY_TARGET })
            mockSteam.inject(90, frame("RegisterDirectPlayerName", type, argument, 2));
        host.update(); assert(mockSteam.sent.empty());
        host.registerClientToServerFunction<int>("RegisterDirectPlayerName", std::function<void(int)>([&](int) { ++unexpectedDirectName; }));
        mockSteam.inject(90, frame("RegisterDirectPlayerName", MessageType::SERVER_ONLY, argument));
        host.update(); assert(unexpectedDirectName == 0);

        host.setClientResourceIntegrityValidated(1, true);
        host.setClientResourceIntegrityValidated(2, true);
        mockSteam.sent.clear();
        host.call(MessageType::CLIENT_ONLY, "State", 1);
        host.callUnreliable(MessageType::CLIENT_ONLY, "State", 1);
        host.call(MessageType::CLIENT_ONLY_TARGET, 3, "State", 1);
        host.callUnreliable(MessageType::CLIENT_ONLY_TARGET, 3, "State", 1);
        assert(mockSteam.sent.size() == 4);
        for (const auto& sent : mockSteam.sent) assert(sent.conn == 90 || sent.conn == 91);
        mockSteam.sent.clear();
        mockSteam.inject(90, frame("Relay", MessageType::SERVER_CLIENT, argument));
        mockSteam.inject(90, frame("GameplayHello", MessageType::SERVER_ONLY, argument)); host.update();
        assert(gameplay == 2 && relay == 2);
        assert(mockSteam.sent.size() == 1 && mockSteam.sent[0].conn == 91);

        // Revocation applies again at execution, even to already queued RPCs.
        mockSteam.inject(90, frame("ResourceIntegrityResponse", MessageType::SERVER_ONLY, argument));
        mockSteam.inject(90, frame("GameplayHello", MessageType::SERVER_ONLY, argument)); host.update();
        assert(gameplay == 2 && !host.isClientResourceIntegrityValidated(1));
        host.setClientResourceIntegrityValidated(1, true);
        mockSteam.sent.clear(); mockSteam.nextSendResult = k_EResultLimitExceeded;
        host.call(MessageType::CLIENT_ONLY_TARGET, 1, "State", 1);
        assert(mockSteam.sent.size() == 1);
        host.setClientResourceIntegrityValidated(1, false); host.update();
        assert(mockSteam.sent.size() == 1); // An old queued state cannot leak after revocation.
        host.setClientResourceIntegrityValidated(1, true);
        assert(host.kickClient(1));
        assert(!host.isClientResourceIntegrityValidated(1));
        mockSteam.inject(90, frame("GameplayHello", MessageType::SERVER_ONLY, argument)); host.update();
        assert(gameplay == 2);
        mockSteam.state(91, k_ESteamNetworkingConnectionState_ClosedByPeer); host.update();
        assert(!host.isClientResourceIntegrityValidated(2));
        admit(host, 93, 1002);
        assert(!host.isClientResourceIntegrityValidated(4)); // Reconnecting never inherits approval.
        host.setClientResourceIntegrityValidated(4, true);
        host.setResourceIntegrityRequired(true);
        assert(!host.isClientResourceIntegrityValidated(4)); // New scene invalidates old admission.
        host.setResourceIntegrityRequired(false);
        assert(host.isClientResourceIntegrityValidated(4));
        host.stop();
    }
    std::cout << "PASS Steam integrity gate: gameplay/relay/state suppression, bootstrap, sender binding, revocation, reconnect and reset\n";
    resetSteam();
    {
        NetworkManager host(Role::SERVER, "127.0.0.1", 27015);
        host.setResourceIntegrityRequired(true); assert(host.start());
        auto* server = hv::lastStartedServer; assert(server != nullptr);
        auto first = std::make_shared<hv::SocketChannel>(); first->connected = true;
        auto second = std::make_shared<hv::SocketChannel>(); second->connected = true;
        server->onConnection(first); server->onConnection(second); host.update();
        assert(host.getClientCount() == 2 && first->writes.size() == 1 && second->writes.size() == 1);
        int gameplay = 0, relay = 0, sender = -1;
        host.registerClientToServerFunction<int>("GameplayHello", std::function<void(int)>([&](int n) { gameplay += n; }));
        host.registerFunction<int>("Relay", std::function<void(int)>([&](int n) { relay += n; }));
        host.registerClientToServerFunction<int>("ResourceIntegrityResponse", std::function<void(int)>([&](int) { sender = host.getExecutingClientId(); }));
        host.registerServerToClientFunction<int>("State", std::function<void(int)>([](int) {}));
        Bytes argument; append(argument, 2);
        auto inject = [&](const hv::SocketChannelPtr& channel, const Bytes& bytes)
        { hv::Buffer buffer(bytes); server->onMessage(channel, &buffer); };
        first->writes.clear(); second->writes.clear();
        for (auto type : { MessageType::SERVER_ONLY, MessageType::SERVER_CLIENT, MessageType::CLIENT_ONLY, MessageType::CLIENT_ONLY_TARGET })
            inject(first, frame("Relay", type, argument, 2));
        inject(first, frame("GameplayHello", MessageType::SERVER_ONLY, argument)); host.update();
        assert(gameplay == 0 && relay == 0 && first->writes.empty() && second->writes.empty());
        inject(first, frame("ResourceIntegrityResponse", MessageType::SERVER_ONLY, argument)); host.update();
        assert(sender == 1 && !host.isClientResourceIntegrityValidated(2));
        host.setClientResourceIntegrityValidated(1, true);
        host.call(MessageType::CLIENT_ONLY, "State", 1);
        host.callUnreliable(MessageType::CLIENT_ONLY, "State", 1);
        host.call(MessageType::CLIENT_ONLY_TARGET, 2, "State", 1);
        host.callUnreliable(MessageType::CLIENT_ONLY_TARGET, 2, "State", 1);
        assert(first->writes.size() == 2 && second->writes.empty());
        first->writes.clear();
        inject(first, frame("Relay", MessageType::SERVER_CLIENT, argument)); host.update();
        assert(relay == 2 && second->writes.empty());
        host.setClientResourceIntegrityValidated(2, true);
        inject(first, frame("Relay", MessageType::CLIENT_ONLY_TARGET, argument, 2)); host.update();
        assert(second->writes.size() == 1);
        inject(first, frame("GameplayHello", MessageType::SERVER_ONLY, argument));
        host.setClientResourceIntegrityValidated(1, false); host.update();
        assert(gameplay == 0); // IO accepted it, but main-thread admission was revoked.
        host.setClientResourceIntegrityValidated(1, true);
        first->connected = false; server->onConnection(first); host.update();
        assert(!host.isClientResourceIntegrityValidated(1));
        auto rejoined = std::make_shared<hv::SocketChannel>(); rejoined->connected = true;
        server->onConnection(rejoined); host.update();
        assert(!host.isClientResourceIntegrityValidated(3));
        host.stop();
    }
    std::cout << "PASS Direct-IP integrity gate: gameplay/relay/state suppression, bootstrap, queued revocation and reconnect\n";
}
