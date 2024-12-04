#include "link-state-routing-helper.h"
#include <iostream>
#include <set>
#include <limits> 

namespace ns3 {

// LinkStateDatabase Implementation

// Add the new LSA or update the existing one
void LinkStateDatabase::AddOrUpdateLsa(const LinkStateAdvertisement& lsa) {
    auto it = m_lsdb.find(lsa.routerId);
    if (it == m_lsdb.end() || it->second.sequenceNumber < lsa.sequenceNumber) {
        m_lsdb[lsa.routerId] = lsa;
    }
}

// Returns the current lsdb
std::map<uint32_t, LinkStateAdvertisement> LinkStateDatabase::GetLsdb() const {
    return m_lsdb;
}

bool LinkStateDatabase::IsNewerLsa(const LinkStateAdvertisement& lsa) const {
    auto it = m_lsdb.find(lsa.routerId);
    return (it == m_lsdb.end() || it->second.sequenceNumber < lsa.sequenceNumber);
}

// LinkStateRouting Implementation 

// Initialize the router with its unique ID
LinkStateRouting::LinkStateRouting(uint32_t routerId) : m_routerId(routerId) {
}

void LinkStateRouting::InitializeLsa(const LinkStateAdvertisement& lsa) {
    m_lsa = lsa;
    m_lsdb.AddOrUpdateLsa(m_lsa);
}

void LinkStateRouting::FloodLsp(const LinkStatePacket& lsp) {
    // Check if the LSP is new
    if (m_lsdb.IsNewerLsa(lsp.lsa)) {
        // Update the LSDB with the LSA from the LSP
        m_lsdb.AddOrUpdateLsa(lsp.lsa);

        std::cout << "Flooding LSP with sequence number: " << lsp.sequenceNumber << std::endl;
    }
}

void LinkStateRouting::ProcessLsp(const LinkStatePacket& lsp) {
    // Handle an incoming LSP
    if (m_lsdb.IsNewerLsa(lsp.lsa)) {
        // Update the LSDB if the LSP is new
        m_lsdb.AddOrUpdateLsa(lsp.lsa);

        std::cout << "Processing LSP with sequence number: " << lsp.sequenceNumber << std::endl;
    }
}

std::map<uint32_t, LinkStateAdvertisement> LinkStateRouting::GetLsdb() const {
    // Return the LSDB for debugging or route computation
    return m_lsdb.GetLsdb();
}

std::map<uint32_t, uint32_t> LinkStateRouting::ComputeRoutingTable() {
    const auto& lsdb = m_lsdb.GetLsdb();  // Get the current LSDB
    std::map<uint32_t, uint32_t> distances;  // Distance to each node
    std::map<uint32_t, uint32_t> previous;   // Previous node on the shortest path
    std::set<uint32_t> unvisited;            // Nodes yet to be visited

    // Step 1: Initialize distances and unvisited set
    for (const auto& [nodeId, _] : lsdb) {
        distances[nodeId] = std::numeric_limits<uint32_t>::max();  // Set initial distance to infinity
        unvisited.insert(nodeId);  // Add to unvisited set
    }
    distances[m_routerId] = 0;  // Distance to self is 0

    // Step 2: Dijkstra's algorithm
    while (!unvisited.empty()) {
        // Find the unvisited node with the smallest distance
        uint32_t current = *std::min_element(unvisited.begin(), unvisited.end(),
                                             [&distances](uint32_t a, uint32_t b) {
                                                 return distances[a] < distances[b];
                                             });

        unvisited.erase(current);  // Mark current node as visited

        // Update distances for neighbors of the current node
        const auto& lsa = lsdb.at(current);
        for (size_t i = 0; i < lsa.neighbors.size(); ++i) {
            uint32_t neighbor = lsa.neighbors[i];
            uint32_t linkCost = lsa.linkCosts[i];
            if (unvisited.count(neighbor)) {  // If the neighbor is unvisited
                uint32_t newDistance = distances[current] + linkCost;
                if (newDistance < distances[neighbor]) {
                    distances[neighbor] = newDistance;
                    previous[neighbor] = current;
                }
            }
        }
    }

    // Step 3: Return the routing table
    std::map<uint32_t, uint32_t> routingTable;  // Destination -> Next Hop
    for (const auto& [node, distance] : distances) {
        if (node != m_routerId && distance != std::numeric_limits<uint32_t>::max()) {
            uint32_t nextHop = node;
            while (previous[nextHop] != m_routerId) {
                nextHop = previous[nextHop];
            }
            routingTable[node] = nextHop;
        }
    }

    return routingTable;  // Destination -> Next Hop mapping
}

} 
