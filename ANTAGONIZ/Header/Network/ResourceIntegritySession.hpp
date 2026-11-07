#ifndef ENDURANCE_RESOURCE_INTEGRITY_SESSION_HPP
#define ENDURANCE_RESOURCE_INTEGRITY_SESSION_HPP

#include <cstdint>
#include <map>
#include <string>

// CPU/transport-independent admission state. It is owned by OnlineSessionManager,
// never constructed from a scene Behaviour or a user-editable scene field.
class ResourceIntegritySession
{
public:
    static constexpr int ProtocolVersion = 1;
    static constexpr std::uint64_t ResponseTimeoutMilliseconds = 120000;
    enum class LocalState { None, Loading, Ready, Failed };
    enum class Failure { None, Mismatch, Unavailable, Timeout, ProtocolMismatch };
    struct Peer
    {
        std::uint64_t epoch = 0;
        std::uint64_t deadline = 0;
        bool challengeSent = false;
        bool responseReceived = false;
        bool accepted = false;
        bool verdictDelivered = false;
        Failure failure = Failure::None;
        std::string digest;
    };
    struct Response
    {
        std::uint64_t epoch = 0;
        int version = ProtocolVersion;
        std::string digest;
    };

    void bindTransport(std::uint64_t token, bool host);
    void clearTransport(); // Keep the verified local snapshot during host migration.
    void beginLocalCheck();
    void completeLocalCheck(const std::string& digest, const std::string& error);
    void cancelLocalCheck();
    void addPeer(int clientId, std::uint64_t now);
    void removePeer(int clientId);
    void receiveChallenge(std::uint64_t epoch, int version);
    bool takeResponse(Response& response);
    void receiveResponse(int clientId, std::uint64_t epoch, int version,
        const std::string& digest, std::uint64_t now);
    void receiveAccepted(std::uint64_t epoch, int version);
    void update(std::uint64_t now);

    LocalState localState() const { return m_localState; }
    const std::string& localError() const { return m_localError; }
    const std::string& localDigest() const { return m_localDigest; }
    bool accepted() const { return m_localState == LocalState::Ready && (m_host || m_clientAccepted); }
    Failure clientFailure() const { return m_clientFailure; }
    std::map<int, Peer>& peers() { return m_peers; }
    const std::map<int, Peer>& peers() const { return m_peers; }
    static bool validDigest(const std::string& digest);

private:
    std::uint64_t nextEpoch();
    void evaluate(Peer& peer, std::uint64_t now);
    void resetClient();

    LocalState m_localState = LocalState::None;
    std::string m_localDigest;
    std::string m_localError;
    bool m_host = false;
    std::uint64_t m_transportToken = 0;
    std::uint64_t m_nextEpoch = 0;
    std::map<int, Peer> m_peers;
    std::uint64_t m_clientEpoch = 0;
    bool m_responseSent = false;
    bool m_clientAccepted = false;
    Failure m_clientFailure = Failure::None;
};

#endif
