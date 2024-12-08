#ifndef LINK_STATE_ROUTING_HELPER_H
#define LINK_STATE_ROUTING_HELPER_H

#include <map>
#include <vector>
#include <cstdint>
#include "ns3/object.h"

namespace ns3 {

// Structure to represent a Link State Advertisement (LSA)
struct LinkStateAdvertisement {
    uint32_t routerId;                     
    std::vector<uint32_t> neighbors; 
    std::vector<uint32_t> linkCosts;
    uint32_t sequenceNumber = 0;
};

// Structure to represent a Link State Packet (LSP)
struct LinkStatePacket {
    uint32_t sequenceNumber;
    LinkStateAdvertisement lsa;
};

// Class to manage the Link State Database (LSDB)
class LinkStateDatabase {
public:
    void AddOrUpdateLsa(const LinkStateAdvertisement& lsa);
    std::map<uint32_t, LinkStateAdvertisement> GetLsdb() const;
    bool IsNewerLsa(const LinkStateAdvertisement& lsa) const;

private:
    std::map<uint32_t, LinkStateAdvertisement> m_lsdb;  // Stores LSAs keyed by routerId
};

// Class to manage LSP flooding and LSDB updates
class LinkStateRouting : public Object{
public:
    LinkStateRouting(uint32_t routerId);
    void InitializeLsa(const LinkStateAdvertisement& lsa);
    void FloodLsp(const LinkStatePacket& lsp);
    void ProcessLsp(const LinkStatePacket& lsp);
    std::map<uint32_t, LinkStateAdvertisement> GetLsdb() const;
    std::map<uint32_t, uint32_t> ComputeRoutingTable();

private:
    uint32_t m_routerId;
    LinkStateDatabase m_lsdb;
    LinkStateAdvertisement m_lsa;
};

} 

#endif 
