#include "ResourceIntegritySession.hpp"

#include <utility>

bool ResourceIntegritySession::validDigest(const std::string& digest)
{
    if (digest.size() != 64) return false;
    for (const unsigned char byte : digest)
        if (!((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f'))) return false;
    return true;
}

void ResourceIntegritySession::resetClient()
{
    m_clientEpoch = 0;
    m_responseSent = false;
    m_clientAccepted = false;
    m_clientFailure = Failure::None;
}

void ResourceIntegritySession::bindTransport(std::uint64_t token, bool host)
{
    m_transportToken = token;
    m_host = host;
    m_peers.clear();
    resetClient();
}

void ResourceIntegritySession::clearTransport()
{
    m_transportToken = 0;
    m_host = false;
    m_peers.clear();
    resetClient();
}

std::uint64_t ResourceIntegritySession::nextEpoch()
{
    // Transport identities and the per-session counter never come from the
    // client. This token separates checks/reconnections; it is not a secret.
    ++m_nextEpoch;
    std::uint64_t epoch = (m_transportToken * 0x9e3779b97f4a7c15ULL) ^ m_nextEpoch;
    if (epoch == 0) epoch = ++m_nextEpoch;
    return epoch;
}

void ResourceIntegritySession::beginLocalCheck()
{
    m_localState = LocalState::Loading;
    m_localDigest.clear();
    m_localError.clear();
    // A challenge may already have arrived while the client loaded the project.
    m_responseSent = false;
    m_clientAccepted = false;
    for (auto& entry : m_peers)
    {
        entry.second = Peer{};
        entry.second.epoch = nextEpoch();
    }
}

void ResourceIntegritySession::completeLocalCheck(const std::string& digest, const std::string& error)
{
    if (m_localState != LocalState::Loading) return;
    m_localError = error;
    m_localDigest = digest;
    m_localState = error.empty() && validDigest(digest) ? LocalState::Ready : LocalState::Failed;
    if (m_localState == LocalState::Failed) m_localDigest.clear();
    m_responseSent = false;
    m_clientAccepted = false;
}

void ResourceIntegritySession::cancelLocalCheck()
{
    m_localState = LocalState::None;
    m_localDigest.clear();
    m_localError.clear();
    for (auto& entry : m_peers)
    {
        entry.second = Peer{};
        entry.second.epoch = nextEpoch();
    }
    resetClient();
}

void ResourceIntegritySession::addPeer(int clientId, std::uint64_t now)
{
    if (!m_host || clientId <= 0 || m_peers.count(clientId)) return;
    Peer peer;
    peer.epoch = nextEpoch();
    if (m_localState == LocalState::Ready)
        peer.deadline = now + ResponseTimeoutMilliseconds;
    m_peers.emplace(clientId, std::move(peer));
}

void ResourceIntegritySession::removePeer(int clientId)
{
    m_peers.erase(clientId);
}

void ResourceIntegritySession::receiveChallenge(std::uint64_t epoch, int version)
{
    if (m_host) return;
    if (epoch == 0 || version != ProtocolVersion)
    {
        m_clientFailure = Failure::ProtocolMismatch;
        m_clientAccepted = false;
        return;
    }
    if (epoch == m_clientEpoch) return; // Duplicate does not revoke an accepted check.
    m_clientEpoch = epoch;
    m_responseSent = false;
    m_clientAccepted = false;
    m_clientFailure = Failure::None;
}

bool ResourceIntegritySession::takeResponse(Response& response)
{
    if (m_host || m_clientEpoch == 0 || m_responseSent || m_clientFailure != Failure::None ||
        (m_localState != LocalState::Ready && m_localState != LocalState::Failed)) return false;
    response.epoch = m_clientEpoch;
    response.version = ProtocolVersion;
    response.digest = m_localState == LocalState::Ready ? m_localDigest : std::string();
    m_responseSent = true;
    return true;
}

void ResourceIntegritySession::evaluate(Peer& peer, std::uint64_t now)
{
    if (peer.accepted || peer.failure != Failure::None || m_localState != LocalState::Ready) return;
    if (peer.deadline == 0) peer.deadline = now + ResponseTimeoutMilliseconds;
    if (peer.responseReceived)
    {
        if (peer.digest == m_localDigest) peer.accepted = true;
        else peer.failure = Failure::Mismatch;
    }
    else if (now >= peer.deadline) peer.failure = Failure::Timeout;
}

void ResourceIntegritySession::receiveResponse(int clientId, std::uint64_t epoch,
    int version, const std::string& digest, std::uint64_t now)
{
    if (!m_host) return;
    const auto found = m_peers.find(clientId);
    if (found == m_peers.end()) return;
    Peer& peer = found->second;
    if (epoch != peer.epoch || peer.accepted || peer.failure != Failure::None || peer.responseReceived) return;
    // Enforce the deadline at the packet entry as well: update() may not have
    // run between expiry and receipt of this reliable response.
    evaluate(peer, now);
    if (peer.failure != Failure::None) return;
    if (version != ProtocolVersion) peer.failure = Failure::ProtocolMismatch;
    else if (digest.empty()) peer.failure = Failure::Unavailable;
    else if (!validDigest(digest)) peer.failure = Failure::ProtocolMismatch;
    else
    {
        peer.responseReceived = true;
        peer.digest = digest;
        evaluate(peer, now);
    }
}

void ResourceIntegritySession::receiveAccepted(std::uint64_t epoch, int version)
{
    if (m_host || epoch != m_clientEpoch || epoch == 0 || !m_responseSent ||
        m_localState != LocalState::Ready) return;
    if (version != ProtocolVersion)
    {
        m_clientFailure = Failure::ProtocolMismatch;
        m_clientAccepted = false;
    }
    else if (m_clientFailure == Failure::None) m_clientAccepted = true;
}

void ResourceIntegritySession::update(std::uint64_t now)
{
    if (!m_host) return;
    for (auto& entry : m_peers) evaluate(entry.second, now);
}
