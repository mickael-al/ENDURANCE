"""Compile the complete production session implementation with deterministic services.

Only Steam and transport are simulated; no OnlineSessionManager method is replaced.
The network-only text helper is used. This is not a live Steam test.
"""
from pathlib import Path
import argparse
import os
import shlex
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitize', action='store_true')
parser.add_argument('--sdk-check', action='store_true')
args = parser.parse_args()
compiler = shlex.split(os.environ.get('CXX', 'g++'))
header = (ROOT / 'ANTAGONIZ/Header/Network/OnlineSessionManager.hpp').read_text()
source = (ROOT / 'ANTAGONIZ/Source/Network/OnlineSessionManager.cpp').read_text()
network_header = (ROOT / 'ANTAGONIZ/Header/Network/NetworkManager.hpp').read_text()
hash_util = network_header[network_header.index('class HashUtil'):network_header.index('enum class Role')]
header = header.replace('#include "NetworkManager.hpp"', '').replace('private:', 'public:')
source = source.replace('#include "OnlineSessionManager.hpp"', '').replace('#include "Debug.hpp"', '')

stubs = r'''
#include <algorithm>
#include <any>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include "ResourceIntegritySession.hpp"
using uint64 = unsigned long long;
using SteamAPICall_t = uint64;
constexpr SteamAPICall_t k_uAPICallInvalid = 0;
constexpr int k_EResultOK = 1, k_EChatRoomEnterResponseSuccess = 1;
constexpr unsigned k_EChatMemberStateChangeLeft = 1, k_EChatMemberStateChangeDisconnected = 2,
    k_EChatMemberStateChangeKicked = 4, k_EChatMemberStateChangeBanned = 8;
enum ELobbyType { k_ELobbyTypePrivate, k_ELobbyTypePublic };
enum ELobbyComparison { k_ELobbyComparisonEqual };
enum ELobbyDistanceFilter { k_ELobbyDistanceFilterDefault, k_ELobbyDistanceFilterWorldwide };
struct CSteamID {
    uint64 id = 0;
    CSteamID() = default;
    explicit CSteamID(uint64 n): id(n) {}
    bool IsValid() const { return id != 0; }
    bool IsLobby() const { return id >= 100; }
    void Clear() { id = 0; }
    uint64 ConvertToUint64() const { return id; }
    bool operator==(CSteamID b) const { return id == b.id; }
    bool operator!=(CSteamID b) const { return id != b.id; }
};
struct LobbyCreated_t { int m_eResult; uint64 m_ulSteamIDLobby; };
struct LobbyEnter_t { uint64 m_ulSteamIDLobby; unsigned m_EChatRoomEnterResponse; };
struct LobbyMatchList_t { unsigned m_nLobbiesMatching; };
struct LobbyDataUpdate_t { uint64 m_ulSteamIDLobby; unsigned char m_bSuccess; };
struct LobbyChatUpdate_t { uint64 m_ulSteamIDLobby, m_ulSteamIDUserChanged; unsigned m_rgfChatMemberStateChange; };
struct GameLobbyJoinRequested_t { CSteamID m_steamIDLobby; };
inline std::map<SteamAPICall_t, std::function<void(void*, bool)>> mockResults;
template<class T, class E> struct CCallResult {
    SteamAPICall_t handle = 0;
    ~CCallResult() { Cancel(); }
    void Cancel() { mockResults.erase(handle); handle = 0; }
    void Set(SteamAPICall_t call, T* object, void(T::*method)(E*,bool)) {
        Cancel(); handle = call;
        mockResults[call] = [object,method](void* value, bool failed) { (object->*method)(static_cast<E*>(value),failed); };
    }
};
template<class T, class E> struct CCallbackManual {
    void Register(T*, void(T::*)(E*)) {}
    void Unregister() {}
};
template<class E> void complete(SteamAPICall_t call, E result, bool failed = false) {
    const auto found = mockResults.find(call);
    assert(found != mockResults.end());
    auto callback = found->second; mockResults.erase(found); callback(&result, failed);
}
struct FakeLobby {
    CSteamID owner{7};
    std::vector<CSteamID> members{CSteamID(42), CSteamID(7), CSteamID(8)};
    int capacity = 8;
    std::map<std::string,std::string> data{{"endurance_game","endurance"},{"endurance_version","1"},{"private","0"},{"password_required","0"}};
};
struct ISteamMatchmaking {
    std::map<uint64, FakeLobby> lobbies;
    SteamAPICall_t lastCall = 0;
    bool rejectNext = false;
    int creates = 0, joins = 0, searches = 0;
    SteamAPICall_t activeSearch = 0;
    int resultLimit = 50, lastResultLimit = 0;
    ELobbyDistanceFilter distance = k_ELobbyDistanceFilterDefault, lastDistance = k_ELobbyDistanceFilterDefault;
    ELobbyType createdType = k_ELobbyTypePrivate;
    std::vector<uint64> leaves, requestedData;
    std::vector<std::pair<std::string,std::string>> filters, lastFilters;
    std::vector<CSteamID> searchResults;
    SteamAPICall_t allocate() { if (rejectNext) { rejectNext=false; return 0; } return ++lastCall; }
    SteamAPICall_t CreateLobby(ELobbyType type, int) { ++creates; createdType=type; return allocate(); }
    SteamAPICall_t JoinLobby(CSteamID) { ++joins; return allocate(); }
    SteamAPICall_t RequestLobbyList() {
        ++searches;
        // Steam has one active search; requesting another cancels its callback.
        mockResults.erase(activeSearch);
        lastFilters=filters; filters.clear();
        lastResultLimit=resultLimit; resultLimit=50;
        lastDistance=distance; distance=k_ELobbyDistanceFilterDefault;
        activeSearch=allocate(); return activeSearch;
    }
    bool RequestLobbyData(CSteamID id) { requestedData.push_back(id.id); return true; }
    void LeaveLobby(CSteamID id) { leaves.push_back(id.id); }
    CSteamID GetLobbyOwner(CSteamID id) { return lobbies[id.id].owner; }
    bool SetLobbyOwner(CSteamID id, CSteamID owner) { lobbies[id.id].owner=owner; return true; }
    const char* GetLobbyData(CSteamID id, const char* key) { return lobbies[id.id].data[key].c_str(); }
    void SetLobbyData(CSteamID id, const char* key, const char* value) { lobbies[id.id].data[key]=value; }
    void SetLobbyMemberData(CSteamID, const char*, const char*) {}
    const char* GetLobbyMemberData(CSteamID, CSteamID, const char*) { return "0"; }
    int GetNumLobbyMembers(CSteamID id) { return int(lobbies[id.id].members.size()); }
    int GetLobbyMemberLimit(CSteamID id) { return lobbies[id.id].capacity; }
    CSteamID GetLobbyMemberByIndex(CSteamID id, int i) { return lobbies[id.id].members.at(i); }
    void SetLobbyGameServer(CSteamID, int, int, CSteamID) {}
    void SetLobbyJoinable(CSteamID, bool) {}
    void AddRequestLobbyListStringFilter(const char* key, const char* value, ELobbyComparison) { filters.emplace_back(key,value); }
    void AddRequestLobbyListResultCountFilter(int maximum) { resultLimit=maximum; }
    void AddRequestLobbyListDistanceFilter(ELobbyDistanceFilter value) { distance=value; }
    CSteamID GetLobbyByIndex(int i) { return searchResults.at(i); }
};
inline ISteamMatchmaking matchmaking;
ISteamMatchmaking* SteamMatchmaking() { return &matchmaking; }
struct FakeSteamUser { CSteamID GetSteamID() { return CSteamID(42); } };
inline std::string steamPersona = "Tester";
struct FakeSteamFriends {
    const char* GetPersonaName() { return steamPersona.c_str(); }
    const char* GetFriendPersonaName(CSteamID id) { return id.id == 42 ? steamPersona.c_str() : "Friend"; }
};
struct FakeSteamApps { const char* GetCurrentGameLanguage() { return "french"; } };
struct FakeSteamUtils { void InitRelayNetworkAccess() {} };
inline bool steamUserAvailable = true;
FakeSteamUser* SteamUser() { static FakeSteamUser value; return steamUserAvailable ? &value : nullptr; }
FakeSteamFriends* SteamFriends() { static FakeSteamFriends value; return &value; }
FakeSteamApps* SteamApps() { static FakeSteamApps value; return &value; }
FakeSteamUtils* SteamNetworkingUtils() { static FakeSteamUtils value; return &value; }
inline bool steamAlive = false;
inline bool callbacksPumped = false;
bool SteamAPI_Init() { steamAlive = true; return true; }
void SteamAPI_RunCallbacks() { assert(steamAlive); callbacksPumped = true; }
void SteamAPI_Shutdown() { steamAlive = false; }
namespace Ge { namespace Debug {
    template<class... T> void Info(T...) {}
    template<class... T> void Warn(T...) {}
    template<class... T> void Error(T...) {}
} }
enum class Role { CLIENT, SERVER };
enum class NetworkConnectionStatus { CONNECTING, CONNECTED, ERROR_STATUS };
enum class MessageType { SERVER_ONLY, SERVER_CLIENT, CLIENT_ONLY, CLIENT_ONLY_TARGET };
inline bool failTransportStart = false;
class NetworkManager {
public:
    inline static std::uint64_t nextInstance=0;
    const std::uint64_t instance=++nextInstance;
    Role role;
    CSteamID remoteHost;
    bool running = false, connected = false, rejected = false, kickedByHost = false;
    int clientId = -1, executingClientId = -1;
    bool integrityRequired = false;
    std::set<int> integrityValidated, peers, kicked;
    std::map<std::string,std::any> functions;
    std::map<std::string,std::function<void(int)>> connectCallbacks;
    std::map<int,std::map<std::string,std::function<void(int)>>> disconnectCallbacks;
    struct SentCall { MessageType type; int target; std::string name; std::any args; };
    std::vector<SentCall> sent;
    std::string credential;
    int capacity = 0;
    std::function<bool(std::uint64_t,const std::string&)> validator;
    std::vector<std::function<void()>> disconnects;
    NetworkManager(Role r, CSteamID host): role(r), remoteHost(host) {}
    NetworkManager(Role r, const std::string&, int): role(r) {}
    bool start() { running=!failTransportStart; connected=running && role==Role::SERVER; return running; }
    void stop() { running=connected=false; }
    bool isRunning() const { return running; }
    bool isConnected() const { return connected; }
    bool isServer() const { return role==Role::SERVER; }
    void update() {}
    bool wasAdmissionRejected() const { return rejected; }
    bool wasKicked() const { return kickedByHost; }
    bool kickClient(int id) { kicked.insert(id); integrityValidated.erase(id); return true; }
    int getClientId() const { return clientId; }
    std::uint64_t getInstanceId() const { return instance; }
    int getClientCount() const { return int(peers.size()); }
    int getExecutingClientId() const { return executingClientId; }
    void setResourceIntegrityRequired(bool required) { integrityRequired=required; integrityValidated.clear(); }
    void setClientResourceIntegrityValidated(int id, bool value) { if(value) integrityValidated.insert(id); else integrityValidated.erase(id); }
    bool isClientResourceIntegrityValidated(int id) const { return !integrityRequired || integrityValidated.count(id)>0; }
    std::string getLastError() const { return "injected start failure"; }
    NetworkConnectionStatus getConnectionStatus() const { return !running ? NetworkConnectionStatus::ERROR_STATUS : connected ? NetworkConnectionStatus::CONNECTED : NetworkConnectionStatus::CONNECTING; }
    std::map<std::uint64_t,int> getSteamPeerPings() const { return {}; }
    std::vector<std::function<void()>>& getOnServeurDisconnectClient() { return disconnects; }
    void configureSteamAdmission(std::function<bool(std::uint64_t,const std::string&)> auth, const std::string& value, int total) { validator=auth; credential=value; capacity=total; }
    template<class... T> void registerServerToClientFunction(const std::string& name, std::function<void(T...)> function) { functions[name]=std::move(function); }
    template<class... T> void registerClientToServerFunction(const std::string& name, std::function<void(T...)> function) { functions[name]=std::move(function); }
    template<class... T> void call(MessageType type, int target, const std::string& name, T... args) { sent.push_back({type,target,name,std::make_tuple(args...)}); }
    template<class... T> void call(MessageType type, const std::string& name, T... args) { call(type,-1,name,args...); }
    template<class T> void addClientConnectFunction(const std::string& name, T function) { connectCallbacks[name]=std::move(function); }
    template<class T> void addClientDisconnectFunction(int id, const std::string& name, T function) { disconnectCallbacks[id][name]=std::move(function); }
    template<class... T> void receive(int sender, const std::string& name, T... args) {
        const auto previous=executingClientId; executingClientId=sender;
        std::any_cast<std::function<void(T...)>>(functions.at(name))(args...);
        executingClientId=previous;
    }
    void connectPeer(int id) {
        peers.insert(id); integrityValidated.erase(id);
        auto callbacks=connectCallbacks;
        for(const auto& entry:callbacks) entry.second(id);
    }
    void disconnectPeer(int id) {
        peers.erase(id); integrityValidated.erase(id);
        const auto found=disconnectCallbacks.find(id);
        if(found==disconnectCallbacks.end()) return;
        const auto callbacks=found->second; disconnectCallbacks.erase(found);
        for(const auto& entry:callbacks) entry.second(id);
    }
};
'''

cases = r'''
void dataReady(OnlineSessionManager& session, uint64 id) {
    LobbyDataUpdate_t result{id,1}; session.onLobbyDataUpdated(&result);
}
SteamAPICall_t startJoin(OnlineSessionManager& session, uint64 id) {
    assert(session.joinSteamLobby(id,"")); dataReady(session,id); return matchmaking.lastCall;
}
void finishNameCheck(OnlineSessionManager& session, unsigned matches = 0) {
    assert(session.m_lobbySearchPurpose == OnlineSessionManager::LobbySearchPurpose::CHECK_CREATE_NAME);
    assert(session.getState() == OnlineSessionState::CREATING);
    complete(matchmaking.lastCall, LobbyMatchList_t{matches});
}
const NetworkManager::SentCall& lastIntegrityCall(const NetworkManager& network, const std::string& name, int target=-1) {
    const auto found=std::find_if(network.sent.rbegin(),network.sent.rend(),[&](const auto& call) {
        return call.name==name && call.target==target;
    });
    assert(found!=network.sent.rend()); return *found;
}
std::size_t integrityCallCount(const NetworkManager& network, const std::string& name) {
    return std::count_if(network.sent.begin(),network.sent.end(),[&](const auto& call) { return call.name==name; });
}
void testIntegrityStateMachine() {
    using Check=ResourceIntegritySession;
    using Failure=Check::Failure;
    const std::string digest(64,'a'), other(64,'b');
    constexpr int version=Check::ProtocolVersion;
    assert(Check::validDigest(digest));
    for(const auto& invalid: {std::string(),std::string(63,'a'),std::string(65,'a'),std::string(64,'A'),std::string(64,'g')})
        assert(!Check::validDigest(invalid));

    Check host, client; host.bindTransport(1,true); client.bindTransport(2,false);
    host.beginLocalCheck(); host.addPeer(1,100);
    const auto epoch=host.peers().at(1).epoch;
    client.receiveChallenge(epoch,version);
    Check::Response response;
    assert(!client.takeResponse(response) && !host.accepted() && !client.accepted());
    client.beginLocalCheck(); client.completeLocalCheck(digest,"");
    client.receiveAccepted(epoch,version); assert(!client.accepted()); // ACK cannot precede proof.
    assert(client.takeResponse(response) && response.epoch==epoch && response.digest==digest);
    assert(!client.takeResponse(response));
    host.receiveResponse(999,epoch,version,digest,101); // No client-supplied peer ID can create admission.
    assert(host.peers().size()==1 && !host.peers().at(1).responseReceived);
    host.receiveResponse(1,epoch+1,version,digest,101);
    assert(!host.peers().at(1).responseReceived);
    host.receiveResponse(1,epoch,version,digest,101);
    assert(host.peers().at(1).responseReceived && !host.peers().at(1).accepted);
    host.update(999999); assert(host.peers().at(1).deadline==0); // Loading time does not consume timeout.
    host.completeLocalCheck(digest,""); host.update(1000000);
    assert(host.accepted() && host.peers().at(1).accepted);
    client.receiveAccepted(epoch+1,version); assert(!client.accepted());
    client.receiveAccepted(epoch,version); assert(client.accepted());
    client.receiveChallenge(epoch,version); assert(client.accepted()); // Idempotent duplicate challenge.
    client.receiveAccepted(epoch,version+1);
    assert(!client.accepted() && client.clientFailure()==Failure::ProtocolMismatch);
    host.receiveResponse(1,epoch,version,other,1000001);
    assert(host.peers().at(1).accepted); // A duplicate cannot overwrite a completed verdict.

    host.addPeer(2,1000100); const auto secondEpoch=host.peers().at(2).epoch;
    assert(secondEpoch!=epoch && host.peers().at(2).deadline==1120100);
    host.receiveResponse(2,epoch,version,digest,1000101);
    assert(!host.peers().at(2).responseReceived); // Another peer's proof cannot validate this connection.
    host.receiveResponse(2,secondEpoch,version,other,1000101);
    assert(host.peers().at(2).failure==Failure::Mismatch);
    host.removePeer(2); host.addPeer(2,1000200);
    assert(host.peers().at(2).epoch!=secondEpoch);
    host.receiveResponse(2,secondEpoch,version,digest,1000201);
    assert(!host.peers().at(2).accepted && !host.peers().at(2).responseReceived);
    host.receiveResponse(2,host.peers().at(2).epoch,version,digest,1000201);
    assert(host.peers().at(2).accepted);

    int id=3;
    for(const auto& invalid: {std::string(),std::string(63,'a'),std::string(65,'a'),std::string(64,'A'),std::string(64,'z')}) {
        host.addPeer(id,1000300);
        host.receiveResponse(id,host.peers().at(id).epoch,version,invalid,1000301);
        assert(host.peers().at(id).failure==(invalid.empty()?Failure::Unavailable:Failure::ProtocolMismatch));
        ++id;
    }
    host.addPeer(id,1000300);
    host.receiveResponse(id,host.peers().at(id).epoch,version+1,digest,1000301);
    assert(host.peers().at(id).failure==Failure::ProtocolMismatch);
    ++id; host.addPeer(id,1000300); host.update(1120299);
    assert(host.peers().at(id).failure==Failure::None);
    host.update(1120300); assert(host.peers().at(id).failure==Failure::Timeout);
    host.receiveResponse(id,host.peers().at(id).epoch,version,digest,1120301);
    assert(!host.peers().at(id).accepted);
    ++id; host.addPeer(id,2000000);
    host.receiveResponse(id,host.peers().at(id).epoch,version,digest,2120000);
    assert(host.peers().at(id).failure==Failure::Timeout); // Arrival itself must enforce the deadline.

    const auto oldEpoch=host.peers().at(1).epoch;
    host.beginLocalCheck(); assert(!host.accepted() && !host.peers().at(1).accepted);
    assert(host.peers().at(1).epoch!=oldEpoch && host.peers().at(1).deadline==0);
    host.completeLocalCheck(digest,""); host.update(3000000);
    host.receiveResponse(1,oldEpoch,version,digest,3000001);
    assert(!host.peers().at(1).accepted);
    host.receiveResponse(1,host.peers().at(1).epoch,version,digest,3000001);
    assert(host.peers().at(1).accepted);

    client.clearTransport(); assert(client.localDigest()==digest && !client.accepted());
    client.bindTransport(10,false); client.receiveAccepted(epoch,version); assert(!client.accepted());
    client.receiveChallenge(20,version); assert(client.takeResponse(response));
    client.receiveAccepted(20,version); assert(client.accepted());
    client.clearTransport(); client.bindTransport(11,true);
    assert(client.accepted() && client.localDigest()==digest); // Promoted host retains the original snapshot.
    client.addPeer(1,10); assert(!client.peers().at(1).accepted);
    client.cancelLocalCheck(); assert(client.localState()==Check::LocalState::None);
    assert(client.localDigest().empty() && !client.peers().at(1).accepted && !client.accepted());
    client.clearTransport(); assert(client.peers().empty());
    client.bindTransport(12,false); client.beginLocalCheck();
    client.completeLocalCheck(digest,"Cannot read shader");
    client.receiveChallenge(21,version);
    assert(client.takeResponse(response) && response.digest.empty() && !client.accepted());
    client.receiveAccepted(21,version); assert(!client.accepted());
    client.cancelLocalCheck(); client.beginLocalCheck(); client.completeLocalCheck(other,"");
    client.receiveChallenge(0,version); assert(client.clientFailure()==Failure::ProtocolMismatch);
    assert(!client.takeResponse(response));
    client.receiveChallenge(22,version+1); assert(!client.takeResponse(response));
    client.receiveChallenge(23,version); assert(client.takeResponse(response));
    client.receiveAccepted(23,version+1); assert(!client.accepted() && client.clientFailure()==Failure::ProtocolMismatch);
    std::cout<<"PASS: integrity digest syntax, loading, peer-bound epochs, mismatch/missing/invalid proofs, deadlines, replay, migration and reset\n";
}
void testIntegritySessionIntegration() {
    constexpr int version=ResourceIntegritySession::ProtocolVersion;
    const std::string digest(64,'a'), other(64,'b');
    {
        OnlineSessionManager offline; assert(offline.initialize());
        assert(!offline.m_network && offline.isResourceIntegrityAccepted());
        offline.cancelResourceIntegrityCheck(); assert(offline.isResourceIntegrityAccepted());
        offline.beginResourceIntegrityCheck(); offline.completeResourceIntegrityCheck(digest,"");
        assert(offline.isResourceIntegrityAccepted());
        offline.cancelResourceIntegrityCheck(); assert(!offline.isResourceIntegrityAccepted());
        offline.beginResourceIntegrityCheck(); offline.completeResourceIntegrityCheck("","Missing mesh");
        assert(!offline.isResourceIntegrityAccepted()); offline.update();
        assert(offline.getState()==OnlineSessionState::ERROR_STATUS && offline.consumeReturnToMainMenuRequest());
    }
    {
        OnlineSessionManager host; assert(host.initialize() && host.hostDirect(27015));
        assert(!host.m_network->integrityRequired && host.isResourceIntegrityAccepted());
        host.m_network->connectPeer(1);
        assert(host.m_network->isClientResourceIntegrityValidated(1));
        assert(integrityCallCount(*host.m_network,"ResourceIntegrityChallenge")==0);
        host.beginResourceIntegrityCheck();
        assert(host.m_network->integrityRequired && !host.m_network->isClientResourceIntegrityValidated(1)); host.completeResourceIntegrityCheck(digest,""); host.update();
        assert(host.isResourceIntegrityAccepted());
        auto& network=*host.m_network;
        const auto challenge=std::any_cast<std::tuple<std::uint64_t,int>>(lastIntegrityCall(network,"ResourceIntegrityChallenge",1).args);
        assert(std::get<1>(challenge)==version);
        network.receive(1,"ResourceIntegrityResponse",std::get<0>(challenge),version,digest); host.update();
        assert(network.isClientResourceIntegrityValidated(1));
        assert(integrityCallCount(network,"ResourceIntegrityAccepted")==1);
        host.update(); assert(integrityCallCount(network,"ResourceIntegrityAccepted")==1);
        network.connectPeer(2); network.connectPeer(3); host.update();
        const auto epoch2=std::get<0>(std::any_cast<std::tuple<std::uint64_t,int>>(lastIntegrityCall(network,"ResourceIntegrityChallenge",2).args));
        const auto epoch3=std::get<0>(std::any_cast<std::tuple<std::uint64_t,int>>(lastIntegrityCall(network,"ResourceIntegrityChallenge",3).args));
        network.receive(2,"ResourceIntegrityResponse",epoch3,version,digest); host.update();
        assert(!network.isClientResourceIntegrityValidated(2) && !network.isClientResourceIntegrityValidated(3));
        network.receive(3,"ResourceIntegrityResponse",epoch3,version,other); host.update();
        assert(network.kicked.count(3)==1 && !network.isClientResourceIntegrityValidated(3));
        assert(std::get<0>(std::any_cast<std::tuple<std::string>>(lastIntegrityCall(network,"SessionKicked",3).args))==NetworkText::get(NetworkText::Id::IntegrityMismatch));
        network.receive(2,"ResourceIntegrityResponse",epoch2,version,digest); host.update();
        assert(network.isClientResourceIntegrityValidated(2));

        network.disconnectPeer(2); network.connectPeer(2); host.update();
        const auto reconnected=std::get<0>(std::any_cast<std::tuple<std::uint64_t,int>>(lastIntegrityCall(network,"ResourceIntegrityChallenge",2).args));
        assert(reconnected!=epoch2 && !network.isClientResourceIntegrityValidated(2));
        network.receive(2,"ResourceIntegrityResponse",epoch2,version,digest); host.update();
        assert(!network.isClientResourceIntegrityValidated(2));
        network.receive(2,"ResourceIntegrityResponse",reconnected,version,digest); host.update();
        assert(network.isClientResourceIntegrityValidated(2));
        network.connectPeer(4); host.update();
        host.m_resourceIntegritySession.peers().at(4).deadline=1;
        host.update(); assert(network.kicked.count(4)==1);
        assert(std::get<0>(std::any_cast<std::tuple<std::string>>(lastIntegrityCall(network,"SessionKicked",4).args))==NetworkText::get(NetworkText::Id::IntegrityTimeout));
        auto rejectPeer=[&](int id,int proofVersion,const std::string& proofDigest,NetworkText::Id reason) {
            network.connectPeer(id);
            const auto peerEpoch=std::get<0>(std::any_cast<std::tuple<std::uint64_t,int>>(lastIntegrityCall(network,"ResourceIntegrityChallenge",id).args));
            network.receive(id,"ResourceIntegrityResponse",peerEpoch,proofVersion,proofDigest); host.update();
            assert(network.kicked.count(id)==1 && !network.isClientResourceIntegrityValidated(id));
            assert(std::get<0>(std::any_cast<std::tuple<std::string>>(lastIntegrityCall(network,"SessionKicked",id).args))==NetworkText::clean(NetworkText::get(reason),120));
        };
        rejectPeer(5,version+1,digest,NetworkText::Id::IntegrityProtocolMismatch);
        rejectPeer(6,version,std::string(63,'a'),NetworkText::Id::IntegrityProtocolMismatch);
        rejectPeer(7,version,"",NetworkText::Id::IntegrityUnavailable);

        host.beginResourceIntegrityCheck(); host.completeResourceIntegrityCheck(other,""); host.update();
        assert(!network.isClientResourceIntegrityValidated(1));
        const auto renewed=std::get<0>(std::any_cast<std::tuple<std::uint64_t,int>>(lastIntegrityCall(network,"ResourceIntegrityChallenge",1).args));
        assert(renewed!=std::get<0>(challenge));
        network.receive(1,"ResourceIntegrityResponse",std::get<0>(challenge),version,digest); host.update();
        assert(!network.isClientResourceIntegrityValidated(1));
        network.receive(1,"ResourceIntegrityResponse",renewed,version,other); host.update();
        assert(network.isClientResourceIntegrityValidated(1));
        host.cancelResourceIntegrityCheck();
        assert(!host.isResourceIntegrityAccepted() && !network.isClientResourceIntegrityValidated(1));
        assert(host.m_resourceIntegritySession.peers().count(1)==1);
        host.beginResourceIntegrityCheck(); host.completeResourceIntegrityCheck(digest,""); host.update();
        const auto nextScene=std::get<0>(std::any_cast<std::tuple<std::uint64_t,int>>(lastIntegrityCall(network,"ResourceIntegrityChallenge",1).args));
        assert(nextScene!=renewed && !network.isClientResourceIntegrityValidated(1));
        network.receive(1,"ResourceIntegrityResponse",renewed,version,other); host.update();
        assert(!network.isClientResourceIntegrityValidated(1));
        network.receive(1,"ResourceIntegrityResponse",nextScene,version,digest); host.update();
        assert(network.isClientResourceIntegrityValidated(1));
        host.leaveSession();
        assert(host.m_resourceIntegritySession.localState()==ResourceIntegritySession::LocalState::None);
        assert(host.m_resourceIntegritySession.peers().empty());
    }
    {
        OnlineSessionManager client; assert(client.initialize() && client.joinDirect("127.0.0.1",27015));
        auto& network=*client.m_network; network.connected=true; network.clientId=1;
        assert(!network.integrityRequired && client.isResourceIntegrityAccepted());
        client.beginResourceIntegrityCheck();
        assert(network.integrityRequired && !client.isResourceIntegrityAccepted());
        network.receive(-1,"ResourceIntegrityChallenge",std::uint64_t(123),version); client.update();
        assert(integrityCallCount(network,"ResourceIntegrityResponse")==0);
        client.completeResourceIntegrityCheck(digest,""); client.update();
        const auto proof=std::any_cast<std::tuple<std::uint64_t,int,std::string>>(lastIntegrityCall(network,"ResourceIntegrityResponse").args);
        assert(std::get<0>(proof)==123 && std::get<1>(proof)==version && std::get<2>(proof)==digest);
        assert(!client.isResourceIntegrityAccepted());
        client.update(); assert(integrityCallCount(network,"ResourceIntegrityResponse")==1);
        network.receive(-1,"ResourceIntegrityAccepted",std::uint64_t(122),version);
        assert(!client.isResourceIntegrityAccepted());
        network.receive(-1,"ResourceIntegrityAccepted",std::uint64_t(123),version);
        assert(client.isResourceIntegrityAccepted());
        client.cancelResourceIntegrityCheck(); assert(!client.isResourceIntegrityAccepted());
        client.beginResourceIntegrityCheck();
        network.receive(-1,"ResourceIntegrityChallenge",std::uint64_t(124),version);
        client.completeResourceIntegrityCheck("","Missing shader");
        const auto failed=std::any_cast<std::tuple<std::uint64_t,int,std::string>>(lastIntegrityCall(network,"ResourceIntegrityResponse").args);
        assert(std::get<0>(failed)==124 && std::get<2>(failed).empty());
        client.update();
        assert(!client.hasSession() && client.consumeReturnToMainMenuRequest());
        assert(client.m_resourceIntegritySession.localState()==ResourceIntegritySession::LocalState::None);
        assert(client.joinDirect("127.0.0.1",27015));
        client.m_network->receive(-1,"SessionKicked",std::string(NetworkText::get(NetworkText::Id::IntegrityMismatch))); client.update();
        assert(!client.hasSession() && client.consumeReturnToMainMenuRequest());
        assert(client.joinDirect("127.0.0.1",27015));
        client.beginResourceIntegrityCheck();
        client.m_network->receive(-1,"ResourceIntegrityChallenge",std::uint64_t(125),version+1); client.update();
        assert(!client.hasSession() && client.consumeReturnToMainMenuRequest());
        assert(client.getLastError()==NetworkText::get(NetworkText::Id::IntegrityProtocolMismatch));
    }
    {
        OnlineSessionManager host; assert(host.initialize() && host.hostDirect(27015));
        host.beginResourceIntegrityCheck(); host.completeResourceIntegrityCheck("","Missing collider"); host.update();
        assert(host.isResourceIntegrityAccepted() && !host.hasSession());
        assert(host.getState()==OnlineSessionState::ERROR_STATUS);
    }
    {
        OnlineSessionManager session; assert(session.initialize());
        const auto call=startJoin(session,1701);
        complete(call,LobbyEnter_t{1701,k_EChatRoomEnterResponseSuccess});
        session.m_network->connected=true; session.update(); assert(session.consumeLaunchRequest());
        session.beginResourceIntegrityCheck(); session.completeResourceIntegrityCheck(digest,"");
        session.m_network->receive(-1,"ResourceIntegrityChallenge",std::uint64_t(700),version);
        session.m_network->receive(-1,"ResourceIntegrityAccepted",std::uint64_t(700),version);
        assert(session.isResourceIntegrityAccepted());
        session.beginSteamHostMigration(CSteamID(7));
        assert(!session.isResourceIntegrityAccepted() && session.m_resourceIntegritySession.localDigest()==digest);
        matchmaking.lobbies[1701].owner=CSteamID(42); session.updateSteamHostMigration();
        assert(session.isHost() && session.isResourceIntegrityAccepted() && !session.consumeLaunchRequest());
        session.m_network->connectPeer(1);
        assert(!session.m_network->isClientResourceIntegrityValidated(1));
        const auto proofEpoch=std::get<0>(std::any_cast<std::tuple<std::uint64_t,int>>(lastIntegrityCall(*session.m_network,"ResourceIntegrityChallenge",1).args));
        session.m_network->receive(1,"ResourceIntegrityResponse",proofEpoch,version,digest);
        assert(session.m_network->isClientResourceIntegrityValidated(1));
        session.beginSteamHostMigration(CSteamID(42));
        matchmaking.lobbies[1701].owner=CSteamID(8); session.updateSteamHostMigration();
        session.m_network->connected=true; session.updateSteamHostMigration();
        assert(!session.isHost() && !session.isMigratingHost() && !session.isResourceIntegrityAccepted());
        assert(session.m_resourceIntegritySession.localDigest()==digest && !session.consumeLaunchRequest());
        session.m_network->receive(-1,"ResourceIntegrityAccepted",std::uint64_t(700),version);
        assert(!session.isResourceIntegrityAccepted());
        session.m_network->receive(-1,"ResourceIntegrityChallenge",std::uint64_t(701),version);
        const auto renewed=std::any_cast<std::tuple<std::uint64_t,int,std::string>>(lastIntegrityCall(*session.m_network,"ResourceIntegrityResponse").args);
        assert(std::get<2>(renewed)==digest);
        session.m_network->receive(-1,"ResourceIntegrityAccepted",std::uint64_t(701),version);
        assert(session.isResourceIntegrityAccepted());
    }
    std::cout<<"PASS: real session RPC admission, localized rejection, replay/reconnect, scene recheck, client ACK, local failure, migration and menu cleanup\n";
}
void testDirectPlayerNames() {
    {
        OnlineSessionManager s; assert(s.initialize());
        assert(s.getDirectPlayerName()=="Tester" && s.getLocalPlayerName()=="Tester");
        s.setLocalPlayerName(" \tPhaze LAN \r\n");
        assert(s.getDirectPlayerName()=="Phaze LAN" && s.getLocalPlayerName()=="Phaze LAN");
        assert(s.hostDirect(27015));
        assert(s.getPlayers().front().name=="Phaze LAN");
        s.m_network->connectPeer(3);
        const auto hostAnnouncement=std::any_cast<std::tuple<int,std::string>>(
            lastIntegrityCall(*s.m_network,"DirectPlayerName",3).args);
        assert(std::get<0>(hostAnnouncement)==0 && std::get<1>(hostAnnouncement)=="Phaze LAN");
        s.m_network->receive(3,"RegisterDirectPlayerName",3,std::string("  Guest LAN  "));
        assert(s.m_directPlayerNames.at(3)=="Guest LAN");
        const auto guestAnnouncement=std::any_cast<std::tuple<int,std::string>>(
            lastIntegrityCall(*s.m_network,"DirectPlayerName").args);
        assert(std::get<0>(guestAnnouncement)==3 && std::get<1>(guestAnnouncement)=="Guest LAN");
        s.m_network->receive(3,"RegisterDirectPlayerName",0,std::string("Forged host"));
        assert(s.m_directPlayerNames.at(0)=="Phaze LAN");
        s.leaveSession();
        assert(s.joinDirect("127.0.0.1",27015));
        s.m_network->connected=true; s.m_network->clientId=3; s.update();
        const auto request=std::any_cast<std::tuple<int,std::string>>(
            lastIntegrityCall(*s.m_network,"RegisterDirectPlayerName").args);
        assert(std::get<0>(request)==3 && std::get<1>(request)=="Phaze LAN");
        s.m_network->receive(-1,"DirectPlayerName",0,std::string("Host LAN"));
        s.m_network->receive(-1,"DirectPlayerName",3,std::string("Phaze LAN"));
        const auto players=s.getPlayers();
        const auto local=std::find_if(players.begin(),players.end(),[](const auto& player){return player.local;});
        assert(local!=players.end() && local->name=="Phaze LAN");
        assert(s.getLocalPlayerName()=="Phaze LAN");
        std::cout<<"PASS: direct host and client custom names propagate through actual roster RPCs without identity spoofing\n";
    }
    {
        OnlineSessionManager s; assert(s.initialize());
        s.setLocalPlayerName("Direct alias");
        assert(s.hostDirect(27015));
        steamPersona="Steam Renamed";
        assert(s.createSteamLobby("Nickname isolation",false,"",8));
        assert(s.getLocalPlayerName()==steamPersona && s.getSteamPlayerName()==steamPersona);
        assert(s.getPlayers().front().name==steamPersona);
        assert(s.getDirectPlayerName()=="Direct alias");
        s.setLocalPlayerName("Another direct alias");
        assert(s.getLocalPlayerName()==steamPersona && s.getPlayers().front().name==steamPersona);
        s.leaveSession();
        assert(s.joinDirect("127.0.0.1",27015));
        assert(s.getLocalPlayerName()=="Another direct alias");
        s.requestLobbyInvite(2811);
        assert(s.consumeLobbyInvite()==2811);
        assert(s.joinSteamLobby(2811,""));
        assert(s.getLocalPlayerName()==steamPersona && s.getPlayers().front().name==steamPersona);
        dataReady(s,2811); complete(matchmaking.lastCall,LobbyEnter_t{2811,k_EChatRoomEnterResponseSuccess});
        const auto players=s.getPlayers();
        const auto local=std::find_if(players.begin(),players.end(),[](const auto& player){return player.local;});
        assert(local!=players.end() && local->name==steamPersona);
        assert(s.getLocalPlayerName()==steamPersona);
        s.setLocalPlayerName("Later direct alias");
        assert(s.getLocalPlayerName()==steamPersona);
        s.leaveSession(); assert(s.hostDirect(27015));
        assert(s.getLocalPlayerName()=="Later direct alias" && s.getPlayers().front().name=="Later direct alias");
        steamPersona="Tester";
        std::cout<<"PASS: pending/create/join/invite and live Steam use current persona; direct preference survives transitions\n";
    }
    {
        OnlineSessionManager s; assert(s.initialize());
        s.setLocalPlayerName(" \t\r\n ");
        assert(s.getDirectPlayerName()=="Tester");
        s.setLocalPlayerName(std::string(100,'x'));
        assert(s.getDirectPlayerName()==std::string(32,'x'));
        std::string unicode;
        for(int i=0;i<20;++i) unicode+=u8"樹";
        s.setLocalPlayerName("  "+unicode+"  ");
        assert(s.getDirectPlayerName()==unicode.substr(0,30));
        s.setLocalPlayerName(std::string(31,'a')+u8"é");
        assert(s.getDirectPlayerName()==std::string(31,'a'));
        assert(s.hostDirect(27015)); s.m_network->connectPeer(3);
        s.m_network->receive(3,"RegisterDirectPlayerName",3,unicode);
        assert(s.m_directPlayerNames.at(3)==unicode.substr(0,30));
        s.m_network->receive(3,"RegisterDirectPlayerName",3,std::string(" \t "));
        assert(s.m_directPlayerNames.at(3)==NetworkText::get(NetworkText::Id::OnlineDefaultPlayerPrefix)+std::string("3"));
        s.m_steamPlayerName.clear(); s.setLocalPlayerName("");
        assert(s.getDirectPlayerName()==NetworkText::get(NetworkText::Id::CommonPlayerTitle));
        std::cout<<"PASS: direct nickname trimming, blank fallback, byte limit and UTF-8 boundaries on sender and receiver\n";
    }
}

void testSteamRecoveryLifecycle() {
    {
        OnlineSessionManager s; assert(s.initialize());
        const auto call=startJoin(s,3001);
        complete(call,LobbyEnter_t{3001,k_EChatRoomEnterResponseSuccess});
        s.m_network->connected=true;
        s.update(); assert(s.consumeLaunchRequest());
        const auto original=s.m_network->instance;
        // A transport interruption does not necessarily remove the Steam owner.
        s.m_network->connected=false;
        const auto disconnects=s.m_network->disconnects;
        for(const auto& callback:disconnects) callback();
        s.update();
        assert(s.isMigratingHost());
        assert(s.m_network && s.m_network->instance!=original);
        assert(s.m_network->remoteHost==CSteamID(7));
        assert(!s.isHost() && !s.m_departedHostPending);
        s.m_network->connected=true;
        s.update();
        assert(!s.isMigratingHost() && s.getState()==OnlineSessionState::IN_LOBBY);
        assert(s.m_steamNetworkHost==CSteamID(7) && !s.consumeLaunchRequest());
        assert(!s.consumeReturnToMainMenuRequest() && !s.m_departedHostPending);
        std::cout<<"PASS: transient P2P failure reconnects to the same live lobby owner without inventing a host departure\n";
    }
    for(const auto flag:{k_EChatMemberStateChangeLeft,k_EChatMemberStateChangeDisconnected,
                        k_EChatMemberStateChangeKicked,k_EChatMemberStateChangeBanned}) {
        for(const bool localHost:{false,true}) {
            OnlineSessionManager s; assert(s.initialize());
            matchmaking.lobbies[3002].owner=CSteamID(localHost?42:7);
            matchmaking.lobbies[3002].members={CSteamID(42),CSteamID(7),CSteamID(8)};
            const auto call=startJoin(s,3002);
            complete(call,LobbyEnter_t{3002,k_EChatRoomEnterResponseSuccess});
            s.m_network->connected=true;
            s.update(); assert(s.consumeLaunchRequest());
            matchmaking.lobbies[3002].members={CSteamID(7),CSteamID(8)};
            LobbyChatUpdate_t departed{3002,42,flag};
            s.onLobbyMembersChanged(&departed);
            assert(!s.hasSession() && !s.m_network && !s.isMigratingHost());
            assert(s.getState()==OnlineSessionState::ERROR_STATUS);
            assert(s.consumeReturnToMainMenuRequest());
            s.update(); assert(!s.consumeLaunchRequest());
        }
    }
    std::cout<<"PASS: local Steam lobby removal closes client/host transport and requests one menu return\n";
}


void testSteamTwoPlayerDeparture() {
    const std::string digest(64,'a');
    // Both delivery orders occur in real Steam sessions: a graceful host can
    // transfer ownership before its P2P close; a crash can close P2P first.
    for (const bool ownershipFirst : {false, true}) {
        OnlineSessionManager s; assert(s.initialize());
        const uint64 lobbyId=ownershipFirst?3201:3202;
        auto& lobby=matchmaking.lobbies[lobbyId];
        lobby.owner=CSteamID(7); lobby.members={CSteamID(42),CSteamID(7)};
        const auto call=startJoin(s,lobbyId);
        complete(call,LobbyEnter_t{lobbyId,k_EChatRoomEnterResponseSuccess});
        s.m_network->connected=true; s.update(); assert(s.consumeLaunchRequest());
        s.beginResourceIntegrityCheck(); s.completeResourceIntegrityCheck(digest,"");
        const auto original=s.m_network->instance;
        if (!ownershipFirst) {
            s.m_network->connected=false;
            const auto callbacks=s.m_network->disconnects;
            for (const auto& callback:callbacks) callback();
            s.update();
            assert(s.isMigratingHost() && !s.isHost());
        }
        lobby.owner=CSteamID(42);
        if (ownershipFirst) {
            s.update();
            assert(s.isHost() && !s.isMigratingHost());
            assert(s.m_network->instance!=original && s.m_network->isServer());
            assert(!s.m_departedHostPending); // Ownership alone is not a departure.
        }
        lobby.members={CSteamID(42)};
        LobbyChatUpdate_t departed{lobbyId,7,k_EChatMemberStateChangeDisconnected};
        s.onLobbyMembersChanged(&departed); s.update();
        assert(s.isHost() && !s.isMigratingHost() && s.m_network->isServer());
        assert(s.m_network->isConnected() && s.getPlayerCount()==1);
        assert(s.isResourceIntegrityAccepted() && s.m_resourceIntegritySession.localDigest()==digest);
        assert(!s.consumeLaunchRequest() && !s.consumeReturnToMainMenuRequest());
        OnlineHostDeparture departure;
        assert(s.consumeDepartedSteamHost(departure) && departure.steamId==7);
        assert(departure.steamId == 7 && !s.consumeDepartedSteamHost(departure));
        const auto hostInstance=s.m_network->instance;
        for (int i=0;i<20;++i) s.update();
        assert(s.m_network->instance==hostInstance && s.isHost());
        // Duplicate late callbacks must not replace the new solo host transport.
        s.onLobbyMembersChanged(&departed); s.update();
        assert(s.m_network->instance==hostInstance && !s.consumeDepartedSteamHost(departure));
        s.leaveSession(); s.update();
        assert(!s.hasSession() && !s.m_network && !s.isMigratingHost());
    }
    {
        OnlineSessionManager s; assert(s.initialize());
        auto& lobby=matchmaking.lobbies[3203];
        lobby.owner=CSteamID(42); lobby.members={CSteamID(42),CSteamID(7)};
        const auto call=startJoin(s,3203);
        complete(call,LobbyEnter_t{3203,k_EChatRoomEnterResponseSuccess});
        assert(s.isHost()); s.leaveSession();
        assert(lobby.owner==CSteamID(7)); // The only remaining player is the successor.
        assert(!s.m_network && !s.hasSession());
    }
    std::cout<<"PASS: two-player host departure promotes the remaining member for both callback orders, preserves integrity and closes cleanly\n";
}

void testSteamMigrationOrderingAndAdmission() {
    {
        OnlineSessionManager s; assert(s.initialize());
        const auto call=startJoin(s,3101);
        complete(call,LobbyEnter_t{3101,k_EChatRoomEnterResponseSuccess});
        const auto first=s.m_network->instance;
        // Owner changes before the first transport handshake has completed.
        matchmaking.lobbies[3101].owner=CSteamID(8); s.update();
        assert(s.isMigratingHost() && !s.consumeLaunchRequest());
        assert(s.m_network->instance!=first && s.m_network->remoteHost==CSteamID(8));
        const auto interim=s.m_network->instance;
        // The replacement owner disappears before its handshake completes too.
        matchmaking.lobbies[3101].owner=CSteamID(42);
        matchmaking.lobbies[3101].members={CSteamID(42),CSteamID(7)};
        LobbyChatUpdate_t leftCandidate{3101,8,k_EChatMemberStateChangeDisconnected};
        s.onLobbyMembersChanged(&leftCandidate); s.update();
        assert(s.isHost() && !s.isMigratingHost() && s.m_network->instance!=interim);
        assert(s.consumeLaunchRequest() && !s.consumeLaunchRequest());
        OnlineHostDeparture departed;
        assert(!s.consumeDepartedSteamHost(departed)); // Ownership alone never emits a departure.
        // The original host's membership callback can follow ownership transfer.
        matchmaking.lobbies[3101].members={CSteamID(42)};
        LobbyChatUpdate_t leftOriginal{3101,7,k_EChatMemberStateChangeLeft};
        s.onLobbyMembersChanged(&leftOriginal);
        assert(s.consumeDepartedSteamHost(departed) && departed.steamId==7);
        assert(departed.steamId == 7 && !s.consumeDepartedSteamHost(departed));
        for(const uint64 owner:{8ULL,7ULL,42ULL,8ULL}) {
            matchmaking.lobbies[3101].members={CSteamID(42),CSteamID(7),CSteamID(8)};
            matchmaking.lobbies[3101].owner=CSteamID(owner); s.update();
            if(owner!=42) {
                assert(s.isMigratingHost() && s.m_network->remoteHost==CSteamID(owner));
                s.m_network->connected=true; s.update();
            }
            assert(!s.isMigratingHost() && s.m_steamNetworkHost==CSteamID(owner));
            assert(s.isHost()==(owner==42) && !s.consumeLaunchRequest());
        }
        std::cout<<"PASS: owner changes before admission, failed interim owner, late departure and four consecutive migrations\n";
    }
    {
        OnlineSessionManager s; assert(s.initialize());
        auto& lobby=matchmaking.lobbies[3102];
        lobby.data["private"]="1"; lobby.data["password_required"]="1";
        lobby.data["password_hash"]=s.passwordHash("secret",CSteamID(3102));
        assert(s.joinSteamLobby(3102,"secret")); dataReady(s,3102);
        complete(matchmaking.lastCall,LobbyEnter_t{3102,k_EChatRoomEnterResponseSuccess});
        assert(s.m_network->credential=="secret");
        s.m_network->connected=true; s.update(); assert(s.consumeLaunchRequest());
        s.beginResourceIntegrityCheck(); s.completeResourceIntegrityCheck(std::string(64,'a'), "");
        lobby.owner=CSteamID(42); s.update();
        assert(s.isHost() && !s.isMigratingHost() && s.m_network->integrityRequired);
        assert(s.m_network->validator(7,"secret"));
        assert(!s.m_network->validator(7,"wrong") && !s.m_network->validator(99,"secret"));
        lobby.owner=CSteamID(8); s.update();
        assert(s.m_network->credential=="secret" && s.m_network->integrityRequired);
        s.m_network->rejected=true; s.update();
        assert(!s.hasSession() && !s.isMigratingHost() && s.consumeReturnToMainMenuRequest());
        std::cout<<"PASS: migrated private lobby preserves credentials, member authorization, integrity gate and permanent rejection\n";
    }
    {
        OnlineSessionManager s; assert(s.initialize());
        const auto call=startJoin(s,3103);
        complete(call,LobbyEnter_t{3103,k_EChatRoomEnterResponseSuccess});
        s.m_network->connected=true; s.update(); assert(s.consumeLaunchRequest());
        // A close reason remains authoritative even without the explanatory RPC.
        s.m_network->kickedByHost=true; s.m_hostTransportLost=true; s.update();
        assert(!s.hasSession() && !s.isMigratingHost() && s.consumeReturnToMainMenuRequest());
        assert(s.getLastError()==NetworkText::get(NetworkText::Id::OnlineKicked));
        const auto next=startJoin(s,3104);
        complete(next,LobbyEnter_t{3104,k_EChatRoomEnterResponseSuccess});
        const auto instance=s.m_network->instance;
        LobbyChatUpdate_t stale{3103,42,k_EChatMemberStateChangeKicked};
        s.onLobbyMembersChanged(&stale); dataReady(s,3103);
        assert(s.getLobbyId()==3104 && s.m_network->instance==instance);
        s.leaveSession(); stale.m_ulSteamIDLobby=3104;
        s.onLobbyMembersChanged(&stale); dataReady(s,3104); s.update();
        assert(!s.hasSession() && !s.m_network && !s.consumeReturnToMainMenuRequest());
        std::cout<<"PASS: kick close reason cannot trigger reconnection; stale lobby callbacks cannot revive or close another session\n";
    }
    {
        OnlineSessionManager s; assert(s.initialize());
        const auto call=startJoin(s,3105);
        complete(call,LobbyEnter_t{3105,k_EChatRoomEnterResponseSuccess});
        s.m_network->connected=true; s.update(); assert(s.consumeLaunchRequest());
        failTransportStart=true;
        s.m_hostTransportLost=true; s.update();
        assert(s.isMigratingHost() && !s.m_network);
        const auto attempts=NetworkManager::nextInstance;
        s.update(); assert(NetworkManager::nextInstance==attempts); // 500 ms backoff.
        failTransportStart=false;
        s.m_nextMigrationRetry=std::chrono::steady_clock::now(); s.update();
        assert(s.m_network && s.m_network->remoteHost==CSteamID(7));
        const auto retry=s.m_network->instance;
        s.update(); assert(s.m_network->instance==retry); // Keep a pending attempt alive.
        s.m_network->running=false; s.update(); assert(!s.m_network);
        s.m_migrationStarted=std::chrono::steady_clock::now()-std::chrono::seconds(11);
        s.update();
        assert(!s.hasSession() && s.consumeReturnToMainMenuRequest());
        assert(s.getLastError()==NetworkText::get(NetworkText::Id::OnlineMigrationTimeout));
        std::cout<<"PASS: failed migration starts retry with bounded backoff and one overall deadline\n";
    }
}


void testLobbyLanguages() {
    {
        OnlineSessionManager s; assert(s.initialize());
        assert(NetworkText::isFrench());
        assert(s.createSteamLobby("Language default",false,"",8));
        assert(s.m_pendingLobbyLanguage=="fr");
        // The creation request keeps its language even if the UI changes before callbacks.
        NetworkText::initializeFromSteamLanguage("english");
        finishNameCheck(s);
        complete(matchmaking.lastCall,LobbyCreated_t{k_EResultOK,73001});
        assert(matchmaking.lobbies[73001].data["endurance_language"]=="fr");
        assert(matchmaking.lobbies[73001].data["name"]=="Language default");
        s.leaveSession(); assert(s.m_pendingLobbyLanguage.empty());
        assert(s.createSteamLobby("Language override",true,"secret",8,"de"));
        NetworkText::initializeFromSteamLanguage("french");
        finishNameCheck(s);
        complete(matchmaking.lastCall,LobbyCreated_t{k_EResultOK,73002});
        assert(matchmaking.lobbies[73002].data["endurance_language"]=="de");
        s.leaveSession();
        NetworkText::initializeFromSteamLanguage("english");
        assert(s.createSteamLobby("English default",false,"",8));
        finishNameCheck(s);
        complete(matchmaking.lastCall,LobbyCreated_t{k_EResultOK,73003});
        assert(matchmaking.lobbies[73003].data["endurance_language"]=="en");
        s.leaveSession();
        assert(s.createSteamLobby("Language fallback",false,"",8,"<script>"));
        finishNameCheck(s);
        complete(matchmaking.lastCall,LobbyCreated_t{k_EResultOK,73004});
        assert(matchmaking.lobbies[73004].data["endurance_language"]=="en");
        s.leaveSession();
        assert(s.createSteamLobby("Multilingual",false,"",8,"multi"));
        finishNameCheck(s);
        complete(matchmaking.lastCall,LobbyCreated_t{k_EResultOK,73005});
        assert(matchmaking.lobbies[73005].data["endurance_language"]=="multi");
        std::cout<<"PASS: captured default/explicit/multilingual lobby languages, invalid choice fallback, metadata and reset\n";
    }
    {
        OnlineSessionManager s; assert(s.initialize());
        for(uint64 id=73101;id<=73109;++id) matchmaking.lobbies[id]=FakeLobby();
        matchmaking.lobbies[73101].data["endurance_language"]="en";
        matchmaking.lobbies[73102].data["endurance_language"]="fr";
        // 73103 is a legacy lobby without language metadata.
        matchmaking.lobbies[73104].data["endurance_language"]="de";
        matchmaking.lobbies[73105].data["endurance_language"]="fr";
        matchmaking.lobbies[73106].data["endurance_language"]="multi";
        matchmaking.lobbies[73107].data["endurance_language"]=std::string(256,'x');
        matchmaking.lobbies[73108].data["endurance_language"]="fr";
        matchmaking.lobbies[73108].data["private"]="1";
        matchmaking.lobbies[73109].data["endurance_language"]="fr";
        matchmaking.lobbies[73109].data["endurance_version"]="old";
        matchmaking.searchResults.clear();
        for(uint64 id=73101;id<=73109;++id) matchmaking.searchResults.push_back(CSteamID(id));
        assert(s.requestSteamLobbyList());
        for(const auto& filter:matchmaking.lastFilters) assert(filter.first!="endurance_language");
        assert(matchmaking.lastResultLimit==50 && matchmaking.lastDistance==k_ELobbyDistanceFilterDefault);
        complete(matchmaking.lastCall,LobbyMatchList_t{9});
        const auto checkOrder=[&s](std::initializer_list<uint64> expected) {
            const auto& entries=s.getLobbyList(); assert(entries.size()==expected.size());
            std::size_t index=0;for(uint64 id:expected) assert(entries[index++].id==id);
        };
        checkOrder({73102,73105,73101,73103,73104,73106,73107});
        assert(s.getLobbyList()[3].language.empty());
        assert(s.getLobbyList()[5].language=="multi");
        assert(s.getLobbyList()[6].language.empty());
        const auto checkFilter=[&s](const char* filter, std::initializer_list<uint64> expected) {
            std::vector<uint64> visible;
            for(const auto& entry:s.getLobbyList())
                if(LobbyLanguage::matchesFilter(entry.language,filter)) visible.push_back(entry.id);
            assert(visible==std::vector<uint64>(expected));
        };
        checkFilter("fr",{73102,73105});
        checkFilter("en",{73101});
        checkFilter("multi",{73106});
        checkFilter("?",{73103,73107});
        checkFilter("ja",{});
        checkFilter("",{73102,73105,73101,73103,73104,73106,73107});
        NetworkText::initializeFromSteamLanguage("english");
        assert(s.requestSteamLobbyList());
        complete(matchmaking.lastCall,LobbyMatchList_t{9});
        checkOrder({73101,73102,73103,73104,73105,73106,73107});
        // With no matching language, the entire original Steam ordering survives.
        matchmaking.searchResults={CSteamID(73104),CSteamID(73103),CSteamID(73106)};
        assert(s.requestSteamLobbyList());
        complete(matchmaking.lastCall,LobbyMatchList_t{3});
        checkOrder({73104,73103,73106});
        std::cout<<"PASS: stable same-language priority; language/unknown/all filters; legacy/malformed tags retained; browser query unchanged\n";
    }
    {
        OnlineSessionManager s; assert(s.initialize());
        NetworkText::initializeFromSteamLanguage("english");
        matchmaking.lobbies[73201]=FakeLobby();
        matchmaking.lobbies[73201].data["endurance_language"]="fr";
        auto call=startJoin(s,73201);
        complete(call,LobbyEnter_t{73201,k_EChatRoomEnterResponseSuccess});
        s.m_network->connected=true; s.update(); assert(s.consumeLaunchRequest());
        s.beginSteamHostMigration(CSteamID(7));
        matchmaking.lobbies[73201].owner=CSteamID(42);
        s.updateSteamHostMigration();
        assert(s.isHost() && !s.isMigratingHost());
        assert(matchmaking.lobbies[73201].data["endurance_language"]=="fr");
        assert(!s.consumeLaunchRequest());
        std::cout<<"PASS: a new host with another UI language preserves the lobby language during migration\n";
    }
}


void testEnduranceDefaults() {
    {
        OnlineSessionManager s; assert(s.initialize());
        assert(!s.hasSession() && s.getNetworkManager()==nullptr);
        assert(!s.consumeLaunchRequest() && s.getMinimumPlayers()==2);
        assert(!s.canStartGame(false) && !s.canStartGame(true));
        assert(s.hostDirect(27015));
        assert(!s.canStartGame(false) && s.canStartGame(true));
        assert(!s.m_network->integrityRequired && s.isResourceIntegrityAccepted());
        s.cancelResourceIntegrityCheck();
        assert(!s.m_network->integrityRequired && s.isResourceIntegrityAccepted());
        s.m_network->connectPeer(1);
        assert(s.getPlayerCount()==2 && s.canStartGame(false));
        s.setMinimumPlayers(5);
        assert(s.getMinimumPlayers()==5 && !s.canStartGame(false) && s.canStartGame(true));
        for(int id=2;id<=4;++id) s.m_network->connectPeer(id);
        assert(s.getPlayerCount()==5 && s.canStartGame(false));
        s.setMinimumPlayers(0); assert(s.getMinimumPlayers()==1);
        s.setMinimumPlayers(999); assert(s.getMinimumPlayers()==16);
        s.leaveSession();
        assert(!s.canStartGame(true) && s.isResourceIntegrityAccepted());
    }
    {
        OnlineSessionManager s; assert(s.initialize());
        auto& otherGame=matchmaking.lobbies[88001];
        otherGame.data.clear();
        otherGame.data["parasite_game"]="parasite";
        otherGame.data["parasite_version"]="2";
        assert(s.joinSteamLobby(88001,""));
        const int joinsBefore=matchmaking.joins;
        dataReady(s,88001);
        assert(matchmaking.joins==joinsBefore && s.getLobbyId()==0 && s.getNetworkManager()==nullptr);
        assert(s.getState()==OnlineSessionState::ERROR_STATUS);
        assert(s.getLastError()==NetworkText::get(NetworkText::Id::OnlineIncompatibleLobby));
    }
    {
        OnlineSessionManager s; assert(s.initialize());
        assert(s.createSteamLobby("Two travelers",false,"",2));
        assert(s.m_pendingCapacity==2);
        finishNameCheck(s);
        complete(matchmaking.lastCall,LobbyCreated_t{k_EResultOK,88002});
        assert(matchmaking.lobbies[88002].data["endurance_game"]=="endurance");
        assert(matchmaking.lobbies[88002].data["endurance_version"]=="1");
        assert(matchmaking.lobbies[88002].data.count("parasite_game")==0);
    }
    std::cout<<"PASS: ENDURANCE-only namespace, idle defaults, configurable player threshold and two-player lobbies\n";
}

int main() {
    std::cout.setf(std::ios::unitbuf);
    testEnduranceDefaults();
    testLobbyLanguages();
    testSteamRecoveryLifecycle();
    testSteamTwoPlayerDeparture();
    testSteamMigrationOrderingAndAdmission();
    testDirectPlayerNames();
    testIntegrityStateMachine();
    testIntegritySessionIntegration();
    {
        OnlineSessionManager s; assert(s.initialize());
        assert(s.getPlayers().front().steamId == 0); // No Steam session yet.
        assert(s.createSteamLobby("Avatar pending", false, "", 8));
        auto players = s.getPlayers();
        assert(s.getLobbyId() == 0 && players.size() == 1);
        assert(players.front().steamId == 42 && players.front().local && players.front().host);
        assert(players.front().name == s.getLocalPlayerName());
        s.m_steamAvailable = false;
        assert(s.getPlayers().front().steamId == 0);
        s.m_steamAvailable = true;
        steamUserAvailable = false;
        assert(s.getPlayers().front().steamId == 0);
        steamUserAvailable = true;
        finishNameCheck(s);
        assert(s.getLobbyId() == 0 && s.getPlayers().front().steamId == 42);
        complete(matchmaking.lastCall, LobbyCreated_t{k_EResultOK, 1991});
        players = s.getPlayers();
        assert(players.size() == 3 && players[0].steamId == 42 && players[0].local);
        assert(players[1].steamId == 7 && !players[1].local);
        s.leaveSession();
        assert(s.joinSteamLobby(1992, ""));
        players = s.getPlayers();
        assert(s.getLobbyId() == 0 && players.size() == 1);
        assert(players.front().steamId == 42 && players.front().local && !players.front().host);
        s.leaveSession();
        assert(s.hostDirect(27015));
        assert(s.getPlayers().front().steamId == 0);
        s.leaveSession();
        assert(s.getPlayers().front().steamId == 0);
        std::cout << "PASS: pending Steam creation/join keeps local avatar identity; live roster, unavailable Steam and direct sessions remain correct\n";
    }
    {
        OnlineSessionManager s; assert(s.initialize());
        s.m_steamPlayerName="Phaze";
        assert(s.getDefaultSteamLobbyName()=="Phaze's Lobby");
        NetworkText::initializeFromSteamLanguage("english");
        assert(s.getDefaultSteamLobbyName()=="Phaze's Lobby");
        NetworkText::initializeFromSteamLanguage("french");
        s.m_steamPlayerName.clear();
        assert(s.getDefaultSteamLobbyName()==NetworkText::get(NetworkText::Id::MainDefaultLobbyName));
        s.m_steamPlayerName="  \t ";
        assert(s.getDefaultSteamLobbyName()==NetworkText::get(NetworkText::Id::MainDefaultLobbyName));
        s.m_steamPlayerName=std::string(100,'x');
        assert(s.getDefaultSteamLobbyName()==std::string(55,'x')+"'s Lobby");
        std::string unicode;
        for (int n=0;n<30;++n) unicode+=u8"樹";
        s.m_steamPlayerName=unicode;
        steamPersona=unicode;
        const auto expected=unicode.substr(0,54)+"'s Lobby";
        assert(s.getDefaultSteamLobbyName()==expected && expected.size()==62);
        auto creates=matchmaking.creates;
        assert(s.createSteamLobby(" \t\r\n",false,"",8));
        assert(s.m_pendingLobbyName==expected && matchmaking.creates==creates);
        s.leaveSession();
        assert(s.createSteamLobby(" \t"+unicode+"\r\n ",false,"",8));
        assert(s.m_pendingLobbyName==unicode.substr(0,63));
        s.leaveSession();
        assert(s.createSteamLobby(" \tCustom room \r\n",false,"",8));
        assert(s.m_pendingLobbyName=="Custom room");
        finishNameCheck(s);
        complete(matchmaking.lastCall,LobbyCreated_t{k_EResultOK,810});
        assert(matchmaking.lobbies[810].data["name"]=="Custom room");
        steamPersona="Tester";
        std::cout<<"PASS: shared Steam default, language/fallback, UTF-8 boundaries, trimming, custom name publication\n";
    }
    {
        OnlineSessionManager s; s.initialize();
        auto creates=matchmaking.creates;
        assert(s.createSteamLobby("Exact name",true,"password",8));
        const std::vector<std::pair<std::string,std::string>> expected{
            {"endurance_game","endurance"},{"name","Exact name"}};
        assert(matchmaking.lastFilters==expected);
        assert(matchmaking.lastDistance==k_ELobbyDistanceFilterWorldwide);
        assert(matchmaking.lastResultLimit==1 && matchmaking.filters.empty());
        assert(matchmaking.creates==creates && !s.consumeLaunchRequest());
        auto search=matchmaking.lastCall;
        auto searches=matchmaking.searches;
        assert(!s.requestSteamLobbyList());
        assert(matchmaking.searches==searches && matchmaking.lastCall==search);
        assert(s.getState()==OnlineSessionState::CREATING && s.getLastError().empty());
        s.update(); s.update();
        assert(matchmaking.searches==searches && matchmaking.creates==creates);
        finishNameCheck(s);
        assert(matchmaking.creates==creates+1 && s.getState()==OnlineSessionState::CREATING);
        assert(!s.requestSteamLobbyList() && matchmaking.searches==searches);
        complete(matchmaking.lastCall,LobbyCreated_t{k_EResultOK,811});
        assert(s.getState()==OnlineSessionState::IN_LOBBY);
        std::cout<<"PASS: fresh worldwide exact-name check includes private/other versions; creation waits; refresh cannot steal search\n";
    }
    {
        OnlineSessionManager s; s.initialize();
        auto creates=matchmaking.creates;
        assert(s.createSteamLobby("Existing",false,"",8));
        finishNameCheck(s,1);
        assert(matchmaking.creates==creates && !s.m_lobbyOperationActive && !s.consumeLaunchRequest());
        assert(s.getState()==OnlineSessionState::ERROR_STATUS);
        assert(s.getLastError()==NetworkText::get(NetworkText::Id::OnlineLobbyNameTaken));
        assert(s.createSteamLobby("Different",false,"",8));
        finishNameCheck(s);
        assert(matchmaking.creates==creates+1 && s.getLastError().empty());
        complete(matchmaking.lastCall,LobbyCreated_t{k_EResultOK,812});
        std::cout<<"PASS: duplicate never creates or launches; corrected name can be retried\n";
    }
    {
        OnlineSessionManager s; s.initialize();
        auto creates=matchmaking.creates;
        matchmaking.rejectNext=true;
        assert(!s.createSteamLobby("Check rejected",false,"",8));
        assert(s.getLastError()==NetworkText::get(NetworkText::Id::OnlineLobbyNameCheckFailed));
        assert(matchmaking.creates==creates && !s.m_lobbyOperationActive);
        assert(s.createSteamLobby("Check I/O",false,"",8));
        complete(matchmaking.lastCall,LobbyMatchList_t{0},true);
        assert(s.getLastError()==NetworkText::get(NetworkText::Id::OnlineLobbyNameCheckFailed));
        assert(matchmaking.creates==creates && !s.consumeLaunchRequest());
        assert(s.createSteamLobby("Check null",false,"",8));
        const auto nullCall=matchmaking.lastCall;
        auto callback=mockResults.at(nullCall); mockResults.erase(nullCall);
        callback(nullptr,false);
        assert(s.getLastError()==NetworkText::get(NetworkText::Id::OnlineLobbyNameCheckFailed) && matchmaking.creates==creates);
        assert(s.createSteamLobby("Create rejected",false,"",8));
        matchmaking.rejectNext=true;
        finishNameCheck(s);
        assert(s.getLastError()==NetworkText::get(NetworkText::Id::OnlineCreateRejected));
        assert(!s.m_lobbyOperationActive && matchmaking.creates==creates+1);
        std::cout<<"PASS: rejected/I/O/null name checks fail closed, actual create rejection releases mutation slot\n";
    }
    {
        OnlineSessionManager s; s.initialize();
        assert(s.requestSteamLobbyList()); const auto browser=matchmaking.lastCall;
        auto creates=matchmaking.creates;
        assert(s.createSteamLobby("From browser",false,"",8));
        assert(mockResults.count(browser)==0);
        const auto abandoned=matchmaking.lastCall;
        assert(s.createSteamLobby("Replacement check",false,"",8));
        assert(mockResults.count(abandoned)==0 && matchmaking.creates==creates);
        assert(s.m_pendingLobbyName=="Replacement check");
        finishNameCheck(s);
        complete(matchmaking.lastCall,LobbyCreated_t{k_EResultOK,813});
        assert(matchmaking.lobbies[813].data["name"]=="Replacement check");
        s.leaveSession();
        assert(s.createSteamLobby("Cancelled for direct",false,"",8));
        const auto directCheck=matchmaking.lastCall;
        assert(s.hostDirect(27015));
        assert(mockResults.count(directCheck)==0);
        LobbyMatchList_t stale{1}; s.onLobbyListReceived(&stale,false);
        assert(s.getKind()==OnlineSessionKind::DIRECT_IP && s.getState()==OnlineSessionState::IN_LOBBY);
        s.leaveSession();
        assert(s.createSteamLobby("Cancelled for invite",false,"",8));
        const auto inviteCheck=matchmaking.lastCall;
        s.requestLobbyInvite(900);
        assert(mockResults.count(inviteCheck)==0 && s.consumeLobbyInvite()==900);
        assert(!s.hasSession() && !s.consumeLaunchRequest());
        assert(s.createSteamLobby("Cancelled for shutdown",false,"",8));
        const auto shutdownCheck=matchmaking.lastCall;
        s.release(); assert(mockResults.count(shutdownCheck)==0);
        std::cout<<"PASS: obsolete browser/check cancellation, replacement, direct-IP/invite handoff, shutdown cleanup\n";
    }
    {
        OnlineSessionManager s; s.initialize();
        assert(s.requestSteamLobbyList()); const auto old=matchmaking.lastCall;
        assert(s.requestSteamLobbyList());
        assert(mockResults.count(old)==0 && matchmaking.lastResultLimit==50);
        assert(matchmaking.lastDistance==k_ELobbyDistanceFilterDefault);
        matchmaking.searchResults={CSteamID(810),CSteamID(811)};
        complete(matchmaking.lastCall,LobbyMatchList_t{2});
        assert(s.getState()==OnlineSessionState::IDLE && s.getLobbyList().size()==1);
        assert(s.getLobbyList().front().name=="Custom room");
        std::cout<<"PASS: browser replacement, default regional scope and private lobby filtering remain intact\n";
    }

    {
        OnlineSessionManager s; assert(s.initialize());
        assert(NetworkText::isFrench());
        assert(s.createSteamLobby("Private",true,"secret",8)); finishNameCheck(s);
        assert(matchmaking.createdType==k_ELobbyTypePublic);
        auto call=matchmaking.lastCall;
        complete(call,LobbyCreated_t{k_EResultOK,100});
        assert(s.m_network->validator(7,"secret"));
        assert(!s.m_network->validator(999,"secret"));
        assert(!s.m_network->validator(7,"wrong"));
        assert(s.m_network->capacity==8);
        assert(s.consumeLaunchRequest() && !s.consumeLaunchRequest());
        s.requestLobbyInvite(100); assert(s.hasSession()); // Same lobby is a no-op.
        s.requestLobbyInvite(42); assert(s.hasSession()); // A user ID is not a lobby.
        s.requestLobbyInvite(200);
        assert(!s.hasSession() && !s.m_network);
        assert(s.consumeReturnToMainMenuRequest());
        assert(s.consumeLobbyInvite()==200 && s.consumeLobbyInvite()==0);
        assert(s.requestSteamLobbyList());
        assert(std::find(matchmaking.lastFilters.begin(),matchmaking.lastFilters.end(),std::make_pair(std::string("private"),std::string("0")))!=matchmaking.lastFilters.end());
        std::cout<<"PASS: real session creation, password/member authorization, same/invalid invite, scene-to-menu handoff, private search filter\n";
    }
    {
        OnlineSessionManager s; s.initialize();
        assert(s.createSteamLobby("Abandoned",false,"",8,"fr")); finishNameCheck(s); auto first=matchmaking.lastCall;
        s.leaveSession();
        assert(mockResults.count(first)==1); // Cancelling must retain the callback.
        auto creates=matchmaking.creates;
        assert(s.createSteamLobby("Replacement",false,"",8,"de"));
        assert(matchmaking.creates==creates); // No overlapping regular-lobby mutation.
        complete(first,LobbyCreated_t{k_EResultOK,201});
        assert(matchmaking.leaves.back()==201 && !s.m_lobbyId.IsValid());
        s.update(); assert(matchmaking.creates==creates);
        finishNameCheck(s); assert(matchmaking.creates==creates+1);
        complete(matchmaking.lastCall,LobbyCreated_t{k_EResultOK,202});
        assert(s.getLobbyId()==202 && s.consumeLaunchRequest());
        assert(matchmaking.lobbies[202].data["endurance_language"]=="de");
        std::cout<<"PASS: cancelled create consumed and left before replacement create begins\n";
    }
    {
        OnlineSessionManager s; s.initialize();
        auto first=startJoin(s,301); auto joins=matchmaking.joins;
        s.leaveSession();
        assert(s.joinSteamLobby(301,"")); dataReady(s,301);
        assert(matchmaking.joins==joins);
        complete(first,LobbyEnter_t{301,k_EChatRoomEnterResponseSuccess});
        assert(matchmaking.leaves.back()==301);
        s.update(); assert(matchmaking.joins==joins+1);
        matchmaking.lobbies[301].owner=CSteamID(42);
        complete(matchmaking.lastCall,LobbyEnter_t{301,k_EChatRoomEnterResponseSuccess});
        assert(s.isHost() && s.isNetworkReady() && s.consumeLaunchRequest());
        s.update(); assert(!s.consumeLaunchRequest());
        std::cout<<"PASS: same-ID cancelled join cleaned before replacement; owner-at-entry launches exactly once\n";
    }
    {
        OnlineSessionManager s; s.initialize();
        matchmaking.rejectNext=true;
        assert(!s.createSteamLobby("Rejected",false,"",8));
        assert(!s.m_lobbyOperationActive && s.getState()==OnlineSessionState::ERROR_STATUS);
        auto call=startJoin(s,401);
        failTransportStart=true;
        complete(call,LobbyEnter_t{401,k_EChatRoomEnterResponseSuccess});
        failTransportStart=false;
        assert(s.getState()==OnlineSessionState::ERROR_STATUS);
        assert(!s.consumeLaunchRequest());
        std::cout<<"PASS: rejected Steam call releases operation slot; transport start error preserved\n";
    }
    {
        OnlineSessionManager s; s.initialize();
        matchmaking.lobbies[501].data["password_required"]="1";
        assert(s.joinSteamLobby(501,"")); auto joins=matchmaking.joins;
        dataReady(s,501);
        assert(!s.hasSession() && s.consumeLobbyPasswordRequest()==501);
        assert(s.consumeLobbyPasswordRequest()==0 && matchmaking.joins==joins);
        matchmaking.lobbies[502].data["endurance_version"]="old";
        assert(s.joinSteamLobby(502,"")); dataReady(s,502);
        assert(s.getState()==OnlineSessionState::ERROR_STATUS && matchmaking.joins==joins);
        auto call=startJoin(s,503);
        matchmaking.lobbies[503].data["endurance_version"]="old";
        complete(call,LobbyEnter_t{503,k_EChatRoomEnterResponseSuccess});
        assert(!s.hasSession() && matchmaking.leaves.back()==503);
        std::cout<<"PASS: password prompt without joining, version validation before and after Steam join\n";
    }
    {
        OnlineSessionManager s; s.initialize();
        auto call=startJoin(s,601);
        complete(call,LobbyEnter_t{601,k_EChatRoomEnterResponseSuccess});
        matchmaking.lobbies[601].data["state"]="starting";
        dataReady(s,601);
        assert(!s.consumeLaunchRequest()); // Metadata must not bypass transport authentication.
        s.m_network->rejected=true;
        s.update();
        assert(!s.hasSession() && !s.isMigratingHost() && s.getState()==OnlineSessionState::ERROR_STATUS);
        assert(!s.consumeReturnToMainMenuRequest()); // Still on initial menu.
        std::cout<<"PASS: scene launch waits for admission, rejection terminates rather than migrating\n";
    }
    {
        OnlineSessionManager s; s.initialize();
        auto call=startJoin(s,701);
        complete(call,LobbyEnter_t{701,k_EChatRoomEnterResponseSuccess});
        s.beginSteamHostMigration(CSteamID(7));
        matchmaking.lobbies[701].owner=CSteamID(42);
        s.updateSteamHostMigration();
        assert(s.isHost() && !s.isMigratingHost() && s.consumeLaunchRequest());
        s.beginSteamHostMigration(CSteamID(42));
        matchmaking.lobbies[701].owner=CSteamID(8);
        s.updateSteamHostMigration();
        s.m_network->connected=true;
        s.updateSteamHostMigration();
        assert(!s.isMigratingHost() && !s.consumeLaunchRequest());
        std::cout<<"PASS: migration during initial join launches; established scene migration never relaunches\n";
    }
    {
        OnlineSessionManager s; s.initialize();
        assert(s.createSteamLobby("Cancel error",false,"",8)); finishNameCheck(s); auto first=matchmaking.lastCall;
        s.leaveSession(); assert(s.hostDirect(27015));
        complete(first,LobbyCreated_t{2,0});
        assert(s.getKind()==OnlineSessionKind::DIRECT_IP && s.getState()==OnlineSessionState::IN_LOBBY);
        assert(s.consumeLaunchRequest());
        assert(s.joinDirect("127.0.0.1",27015) && s.consumeLaunchRequest());
        s.leaveSession();
        assert(s.createSteamLobby("Release",false,"",8)); finishNameCheck(s); auto last=matchmaking.lastCall;
        s.release(); assert(mockResults.count(last)==0);
        std::cout<<"PASS: stale failure cannot alter direct session; direct host/join preserved; shutdown cancels observers\n";
    }
    assert(mockResults.empty());
}
'''

with tempfile.TemporaryDirectory(prefix='endurance-sessions-') as directory:
    path = Path(directory)
    (path / 'session.cpp').write_text(stubs + hash_util + header + source + cases)
    # Keep the actual session implementation and declarations, replacing only
    # engine/transport boundaries; Steam types and methods come from the SDK.
    sdk_prelude = '#include <steam/steam_api.h>\n'
    sdk_prelude += stubs[stubs.index('#include <algorithm>'):stubs.index('using uint64 =')]
    sdk_prelude += stubs[stubs.index('namespace Ge { namespace Debug'):]
    (path / 'session_sdk.cpp').write_text(sdk_prelude + hash_util + header + source)
    if args.sdk_check:
        subprocess.run(compiler + [
            '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsyntax-only',
            '-I' + str(ROOT / 'ThirdParty/steamworks_sdk/public'),
            '-I' + str(ROOT / 'ANTAGONIZ/Header/Network'),
            str(path / 'session_sdk.cpp'),
        ], check=True)
        print('PASS: full session implementation compiles with the bundled Steam SDK')
    sanitizer_flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if args.sanitize else []
    subprocess.run(compiler + sanitizer_flags + [
        '-std=c++17', '-Wall', '-Wextra', '-Werror', '-g',
        '-I' + str(ROOT / 'ANTAGONIZ/Header/Network'),
        str(path / 'session.cpp'),
        str(ROOT / 'ANTAGONIZ/Source/Network/ResourceIntegritySession.cpp'),
        '-o', str(path / 'test'),
    ], check=True)
    subprocess.run([str(path / 'test')], check=True, env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'})
