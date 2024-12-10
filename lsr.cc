#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/boolean.h"

#include <map>
#include <vector>
#include <cstdint>
#include "ns3/object.h"

#include <iostream>
#include <set>
#include <limits> 
#include <algorithm>

#include "ns3/node-list.h"
#include "ns3/names.h"
#include "ns3/ptr.h"
#include "ns3/ipv4-list-routing.h"

#include "ns3/object-factory.h"
#include "ns3/node.h"
#include "ns3/node-container.h"
#include "ns3/ipv4-routing-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("LSRScript"); 

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

class LinkStateRoutingProtocol : public Ipv4RoutingProtocol {
    public:
        // commented out due to compile issues with the rest unimplemented properly. Currently due to RouteInput giving an error concerning override
        /*static TypeId GetTypeId(void)
        {
            static TypeId tid = TypeId("ns3::LinkStateRoutingProtocol")
                .SetParent<Ipv4RoutingProtocol>()
                .AddConstructor<LinkStateRoutingProtocol>()
                ;
            return tid;
        }*/
        LinkStateRoutingProtocol();
        virtual ~LinkStateRoutingProtocol();
        // Inherited methods from Ipv4RoutingProtocol
        Ptr<Ipv4Route> RouteOutput(Ptr<Packet> p, const Ipv4Header &header, Ptr<NetDevice> oif, Socket::SocketErrno &sockerr) override
        {
            uint32_t dest = header.GetDestination().Get();

            if (m_lsroutingTable.find(dest) != m_lsroutingTable.end()) {
                uint32_t next = m_lsroutingTable[dest];
                Ptr<Ipv4Route> route = Create<Ipv4Route>();
                //route->SetSource(); //TODO: get source for route and set it to that
                route->SetDestination(Ipv4Address(dest));
                route->SetGateway(Ipv4Address(next));
                route->SetOutputDevice(oif);
                
                return route;
            }

            //https://www.nsnam.org/docs/release/3.19/doxygen/classns3_1_1_socket.html#ada1328c5ae0c28cb2a982caf8f6d6ccaa0f8ecb5a4ddbce3bade35fa12c3d49e8
            sockerr = Socket::ERROR_NOROUTETOHOST;
            return NULL;
        }
        bool RouteInput(Ptr<const Packet> p, const Ipv4Header &header, Ptr<const NetDevice> idev, UnicastForwardCallback ucb, MulticastForwardCallback mcb, LocalDeliverCallback lcb, ErrorCallback ecb)
        {
            uint32_t dest = header.GetDestination().Get();
            
            if (m_lsroutingTable.find(dest) != m_lsroutingTable.end()) {
                uint32_t next = m_lsroutingTable[dest];

                Ipv4Address nextAdd = Ipv4Address(next);  
                Ptr<Ipv4Route> route = Create<Ipv4Route>();
                route->SetDestination(Ipv4Address(dest)); 
                route->SetGateway(nextAdd);           
                uint32_t interfaceO = m_ipv4->GetInterfaceForAddress(nextAdd);
                route->SetOutputDevice(m_ipv4->GetNetDevice (interfaceO));        

                ucb(route, p, header);
                return true;
            }

            return false;
        }

        /* Protocols are expected to implement this method to be notified of the state change of an interface in a node. */
        void NotifyInterfaceUp(uint32_t interface) override
        {
            LinkStateAdvertisement newlsa;
            newlsa.routerId = 1; // TODO: How are we assigning router IDs?

            m_lsrouting->InitializeLsa(newlsa);

            //notify other routers and then update the routing table
            SendLinkStateAdvertisement();

            UpdateRoutingTable();
        }

        /* Protocols are expected to implement this method to be notified of the state change of an interface in a node. */
        void NotifyInterfaceDown(uint32_t interface) override
        {   
            //no function for remove lsa?

            //notify other routers and then update the routing table
            SendLinkStateAdvertisement();

            UpdateRoutingTable();
        }
        void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address) override {
            /* Protocols are expected to implement this method to be notified whenever a new address is added to an interface. 
            Typically used to add a 'network route' on an interface. Can be invoked on an up or down interface. */

        }
        void NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address) override {
            /* Protocols are expected to implement this method to be notified whenever a new address is removed from an interface. 
            Typically used to remove the 'network route' of an interface. Can be invoked on an up or down interface.
            */
        }
        void SetIpv4(Ptr<Ipv4> ipv4) override
        {
            m_ipv4 = ipv4;
        }
        

    private:
        void SendLinkStateAdvertisement();
        void ReceiveLinkStateAdvertisement(Ptr<Socket> socket);
        void UpdateRoutingTable();
        // Data structures for routing table and LSA management
        // ...
        Ptr<Ipv4> m_ipv4;
        std::map<Ipv4Address, Ptr<Socket>> m_socketMap;
        //store a link state routing class and routing table?
        Ptr<LinkStateRouting> m_lsrouting;
        std::map<uint32_t, uint32_t> m_lsroutingTable;
        // ...
};

void LinkStateRoutingProtocol::SendLinkStateAdvertisement() {
    // Create and send LSA packets

}

void LinkStateRoutingProtocol::ReceiveLinkStateAdvertisement(Ptr<Socket> socket) {
    // Process received LSA packets and update routing table

}

void LinkStateRoutingProtocol::UpdateRoutingTable() {
    // Compute shortest paths using Dijkstra’s algorithm

    m_lsroutingTable = m_lsrouting->ComputeRoutingTable();
}

//Constructor
LinkStateRoutingProtocol::LinkStateRoutingProtocol() 
    : m_ipv4(0)
{
    m_lsrouting = Create<LinkStateRouting>(0); //how are we picking router ids?
    UpdateRoutingTable();
}

//Deconstructor
LinkStateRoutingProtocol::~LinkStateRoutingProtocol()
{
}

NS_OBJECT_ENSURE_REGISTERED(LinkStateRoutingProtocol);


//note: inspiration from https://www.nsnam.org/docs/release/3.19/doxygen/aodv-helper_8cc_source.html#l00043
class LinkStateRoutingHelper : public Ipv4RoutingHelper
{
public:
    LinkStateRoutingHelper();
    LinkStateRoutingHelper* Copy(void) const;
    virtual Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const;
    void Set(std::string name, const AttributeValue &value);

private:
    ObjectFactory m_agentFactory;
};

LinkStateRoutingHelper::LinkStateRoutingHelper()
{
    m_agentFactory.SetTypeId("ns3::LinkStateRoutingProtocol");
}

LinkStateRoutingHelper* 
LinkStateRoutingHelper::Copy (void) const 
{
    return new LinkStateRoutingHelper(*this); 
}

Ptr<Ipv4RoutingProtocol> 
LinkStateRoutingHelper::Create (Ptr<Node> node) const
{
    Ptr<LinkStateRoutingProtocol> agent = m_agentFactory.Create<LinkStateRoutingProtocol>();
    node->AggregateObject(agent);
    return agent;
}

void 
LinkStateRoutingHelper::Set (std::string name, const AttributeValue &value)
{
    m_agentFactory.Set(name, value);
}

int
main(int argc, char* argv[])
{
    //Define Network Topology
    CommandLine cmd;
    cmd.Parse(argc, argv);

    NodeContainer nodes;
    nodes.Create(4);

    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("2ms"));

    NetDeviceContainer devices;
    devices = pointToPoint.Install(nodes.Get(0), nodes.Get(1));
    devices = pointToPoint.Install(nodes.Get(1), nodes.Get(2));
    devices = pointToPoint.Install(nodes.Get(2), nodes.Get(3));

    InternetStackHelper stack;
    stack.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");

    Ipv4InterfaceContainer interfaces;
    interfaces = address.Assign(devices);

    // Configure Link State Routing:
    // Assuming there is a link state routing implementation
    LinkStateRoutingHelper linkStateRouting;

    Ipv4ListRoutingHelper list;
    list.Add(linkStateRouting, 0);               //commented due to declaration commented

    InternetStackHelper stack2;
    stack2.SetRoutingHelper(list);
    stack2.Install(nodes);

    //Set Up Applications:
    uint16_t port = 9;
    UdpEchoServerHelper server(port);

    ApplicationContainer apps = server.Install(nodes.Get(3));
    apps.Start(Seconds(1.0));
    apps.Stop(Seconds(10.0));

    UdpEchoClientHelper client(interfaces.GetAddress(3), port);
    client.SetAttribute("MaxPackets", UintegerValue(1));
    client.SetAttribute("Interval", TimeValue(Seconds(1.0)));
    client.SetAttribute("PacketSize", UintegerValue(1024));

    apps = client.Install(nodes.Get(0));
    apps.Start(Seconds(2.0));
    apps.Stop(Seconds(10.0));

    Simulator::Run();
    Simulator::Destroy();
    return 0;
}
