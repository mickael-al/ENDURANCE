#pragma once
#include <cstdint>
#include <vector>
#include <map>
#include <deque>
#include <functional>
#include <algorithm>
#include <cstring>
using HSteamNetConnection=std::uint32_t;
using HSteamListenSocket=std::uint32_t;
using HSteamNetPollGroup=std::uint32_t;
constexpr HSteamNetConnection k_HSteamNetConnection_Invalid=0;
constexpr HSteamListenSocket k_HSteamListenSocket_Invalid=0;
constexpr HSteamNetPollGroup k_HSteamNetPollGroup_Invalid=0;
constexpr int k_nSteamNetworkingSend_Reliable=8;
constexpr int k_nSteamNetworkingSend_UnreliableNoNagle=1;
enum EResult { k_EResultOK=1,k_EResultLimitExceeded=25,k_EResultIgnored=41,k_EResultInvalidState=11 };
enum ESteamNetworkingConnectionState { k_ESteamNetworkingConnectionState_None, k_ESteamNetworkingConnectionState_Connecting, k_ESteamNetworkingConnectionState_FindingRoute, k_ESteamNetworkingConnectionState_Connected, k_ESteamNetworkingConnectionState_ClosedByPeer, k_ESteamNetworkingConnectionState_ProblemDetectedLocally };
class CSteamID { std::uint64_t id=0; public: CSteamID()=default; explicit CSteamID(std::uint64_t v):id(v){} bool IsValid()const{return id!=0;} std::uint64_t ConvertToUint64()const{return id;} };
struct SteamNetworkingIdentity { CSteamID id; void Clear(){id=CSteamID();} void SetSteamID(CSteamID v){id=v;} std::uint64_t GetSteamID64()const{return id.ConvertToUint64();} };
struct SteamNetConnectionInfo_t { HSteamListenSocket m_hListenSocket=0; ESteamNetworkingConnectionState m_eState=k_ESteamNetworkingConnectionState_None; SteamNetworkingIdentity m_identityRemote; int m_eEndReason=0; char m_szEndDebug[128]={}; };
struct SteamNetConnectionRealTimeStatus_t {int m_nPing=1;};
struct SteamNetConnectionStatusChangedCallback_t { HSteamNetConnection m_hConn=0; SteamNetConnectionInfo_t m_info; };
struct SteamNetworkingMessage_t { HSteamNetConnection m_conn=0; void* m_pData=nullptr; int m_cbSize=0; std::vector<std::uint8_t> bytes; void Release(){delete this;} };
inline std::map<const void*,std::function<void(SteamNetConnectionStatusChangedCallback_t*)>> mockCallbacks;
template<class T,class E> class CCallbackManual { public:
 void Register(T* obj,void(T::*method)(E*)){mockCallbacks[this]=[obj,method](E* e){(obj->*method)(e);};}
 void Unregister(){mockCallbacks.erase(this);} ~CCallbackManual(){Unregister();}
};
struct MockSend {HSteamNetConnection conn; std::vector<std::uint8_t> bytes; int flags; EResult result;};
class ISteamNetworkingSockets { public:
 HSteamListenSocket listen=7; HSteamNetConnection outgoing=500;
 HSteamNetPollGroup nextPollGroup=9; bool failPollGroup=false,failListen=false,failConnect=false;
 std::vector<HSteamNetPollGroup> createdPollGroups,destroyedPollGroups;
 std::vector<HSteamListenSocket> closedListenSockets;
 std::map<HSteamNetConnection,SteamNetConnectionInfo_t> peers;
 std::vector<HSteamNetConnection> accepted,closed;
 std::map<HSteamNetConnection,int> closeReasons;
 std::vector<MockSend> sent;
 std::deque<SteamNetworkingMessage_t*> inbox;
 EResult nextSendResult=k_EResultOK;
 HSteamNetPollGroup CreatePollGroup(){if(failPollGroup)return 0;createdPollGroups.push_back(nextPollGroup);return nextPollGroup++;}
 HSteamListenSocket CreateListenSocketP2P(int,int,const void*){return failListen?0:listen;}
 HSteamNetConnection ConnectP2P(const SteamNetworkingIdentity& id,int,int,const void*){if(failConnect)return 0;peers[outgoing].m_identityRemote=id;return outgoing;}
 EResult AcceptConnection(HSteamNetConnection c){accepted.push_back(c);return k_EResultOK;}
 bool SetConnectionPollGroup(HSteamNetConnection,HSteamNetPollGroup){return true;}
 bool CloseConnection(HSteamNetConnection c,int reason,const char*,bool){closed.push_back(c);closeReasons[c]=reason;return true;}
 bool CloseListenSocket(HSteamListenSocket c){closedListenSockets.push_back(c);return true;}
 bool DestroyPollGroup(HSteamNetPollGroup c){destroyedPollGroups.push_back(c);return true;}
 bool GetConnectionInfo(HSteamNetConnection c,SteamNetConnectionInfo_t* out){auto i=peers.find(c);if(i==peers.end())return false;*out=i->second;return true;}
 EResult GetConnectionRealTimeStatus(HSteamNetConnection,SteamNetConnectionRealTimeStatus_t*,int,void*){return k_EResultOK;}
 EResult SendMessageToConnection(HSteamNetConnection c,const void* data,std::uint32_t n,int flags,std::int64_t*){auto p=static_cast<const std::uint8_t*>(data);sent.push_back({c,{p,p+n},flags,nextSendResult});auto r=nextSendResult;nextSendResult=k_EResultOK;return r;}
 int ReceiveMessagesOnPollGroup(HSteamNetPollGroup,SteamNetworkingMessage_t** out,int cap){int n=0;while(n<cap&&!inbox.empty()){out[n++]=inbox.front();inbox.pop_front();}return n;}
 int ReceiveMessagesOnConnection(HSteamNetConnection,SteamNetworkingMessage_t** out,int cap){return ReceiveMessagesOnPollGroup(9,out,cap);}
 void state(HSteamNetConnection c,ESteamNetworkingConnectionState state,int reason=0){SteamNetConnectionStatusChangedCallback_t e;e.m_hConn=c;e.m_info=peers[c];e.m_info.m_eState=state;e.m_info.m_eEndReason=reason;peers[c]=e.m_info;auto callbacks=mockCallbacks;for(auto& cb:callbacks)cb.second(&e);}
 void incoming(HSteamNetConnection c,std::uint64_t steamId){SteamNetConnectionStatusChangedCallback_t e;e.m_hConn=c;e.m_info.m_hListenSocket=listen;e.m_info.m_eState=k_ESteamNetworkingConnectionState_Connecting;e.m_info.m_identityRemote.SetSteamID(CSteamID(steamId));peers[c]=e.m_info;auto callbacks=mockCallbacks;for(auto& cb:callbacks)cb.second(&e);}
 void inject(HSteamNetConnection c,const std::vector<std::uint8_t>& bytes){auto* m=new SteamNetworkingMessage_t;m->m_conn=c;m->bytes=bytes;m->m_pData=m->bytes.data();m->m_cbSize=m->bytes.size();inbox.push_back(m);}
};
inline ISteamNetworkingSockets mockSteam;
inline ISteamNetworkingSockets* SteamNetworkingSockets(){return &mockSteam;}
