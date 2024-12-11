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

#include <iostream>
#include <fstream>
#include <string>
#include <cassert>
//NOTE: Only for testing purposes
#include "ns3/olsr-helper.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/ipv4-list-routing-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("LinkStateRoutingProtocol"); 

// Structure to represent a Link State Advertisement (LSA)
struct Link {
    Ipv4Address neighbor;   // IPv4 address of the neighbor
    uint32_t cost;          // Link cost
    uint32_t next_hop_interface; // Index of the next-hop interface (or similar identifier)
};

struct LinkStateAdvertisement {
    Ipv4Address routerAddress;            // The IPv4 address of the router
    std::vector<Link> links;              // List of links with neighbors and costs
    uint32_t sequenceNumber = 0;          // Sequence number for versioning
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
    std::map<Ipv4Address, LinkStateAdvertisement> GetLsdb() const;
    bool IsNewerLsa(const LinkStateAdvertisement& lsa) const;

private:
    std::map<Ipv4Address, LinkStateAdvertisement> m_lsdb;  // Stores LSAs keyed by routerId
};

// Class to manage LSP flooding and LSDB updates
class LinkStateRouting : public Object{
public:
    LinkStateRouting(Ipv4Address routerAddress);
    void InitializeLsa(const LinkStateAdvertisement& lsa);
    void FloodLsp(const LinkStatePacket& lsp);
    void ProcessLsp(const LinkStatePacket& lsp);
    std::map<Ipv4Address, LinkStateAdvertisement> GetLsdb() const;
    std::map<Ipv4Address, Ipv4Address> ComputeRoutingTable();

private:
    Ipv4Address m_routerAddress;
    LinkStateDatabase m_lsdb;
    LinkStateAdvertisement m_lsa;
};

// LinkStateDatabase Implementation

// Add the new LSA or update the existing one
void LinkStateDatabase::AddOrUpdateLsa(const LinkStateAdvertisement& lsa) {
    auto it = m_lsdb.find(lsa.routerAddress);
    if (it == m_lsdb.end() || it->second.sequenceNumber < lsa.sequenceNumber) {
        m_lsdb[lsa.routerAddress] = lsa;
    }
}

// Returns the current lsdb
std::map<Ipv4Address, LinkStateAdvertisement> LinkStateDatabase::GetLsdb() const {
    return m_lsdb;
}

bool LinkStateDatabase::IsNewerLsa(const LinkStateAdvertisement& lsa) const {
    auto it = m_lsdb.find(lsa.routerAddress);
    return (it == m_lsdb.end() || it->second.sequenceNumber < lsa.sequenceNumber);
}

// LinkStateRouting Implementation 

// Initialize the router with its ipv4 address
LinkStateRouting::LinkStateRouting(Ipv4Address routerAddress) : m_routerAddress(routerAddress) {}

void LinkStateRouting::InitializeLsa(const LinkStateAdvertisement& lsa) {
    m_lsa = lsa;
    m_lsdb.AddOrUpdateLsa(m_lsa);
    std::cout << "Initialized LSA for router address: " << lsa.routerAddress << std::endl;
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

std::map<Ipv4Address, LinkStateAdvertisement> LinkStateRouting::GetLsdb() const {
    // Return the LSDB for debugging or route computation
    return m_lsdb.GetLsdb();
}

std::map<Ipv4Address, Ipv4Address> LinkStateRouting::ComputeRoutingTable() {
    const auto& lsdb = m_lsdb.GetLsdb();
    std::map<Ipv4Address, uint32_t> distances;
    std::map<Ipv4Address, Ipv4Address> previous;
    std::set<Ipv4Address> unvisited;

    for (const auto& [address, lsa] : lsdb) {
        distances[address] = std::numeric_limits<uint32_t>::max();
        unvisited.insert(address);
    }
    distances[m_routerAddress] = 0;

    while (!unvisited.empty()) {
        Ipv4Address current = *std::min_element(unvisited.begin(), unvisited.end(),
            [&distances](Ipv4Address a, Ipv4Address b) {
                return distances[a] < distances[b];
            });
        unvisited.erase(current);

        const auto& links = lsdb.at(current).links;
        for (const auto& link : links) {
            if (unvisited.count(link.neighbor)) {
                uint32_t newDistance = distances[current] + link.cost;
                if (newDistance < distances[link.neighbor]) {
                    distances[link.neighbor] = newDistance;
                    previous[link.neighbor] = current;
                }
            }
        }
    }

    std::map<Ipv4Address, Ipv4Address> routingTable;
    for (const auto& [address, _] : distances) {
        if (address != m_routerAddress && distances[address] != std::numeric_limits<uint32_t>::max()) {
            Ipv4Address nextHop = address;
            while (previous[nextHop] != m_routerAddress) {
                nextHop = previous[nextHop];
            }
            routingTable[address] = nextHop;
        }
    }
    return routingTable;
}


/*Ipv4RoutingProtocol defined at https://www.nsnam.org/doxygen/d1/d9f/classns3_1_1_ipv4_routing_protocol.html
A custom routing protocol for link state routing which derives from Ipv4RoutingProtocol*/
class LinkStateRoutingProtocol : public Ipv4RoutingProtocol {
    public:
        static TypeId GetTypeId(void)
        {
            static TypeId tid = TypeId("LinkStateRoutingProtocol")
                .SetParent<Ipv4RoutingProtocol>()
                .AddConstructor<LinkStateRoutingProtocol>()
                ;
            return tid;
        }
        LinkStateRoutingProtocol();
        virtual ~LinkStateRoutingProtocol();
        // Inherited methods from Ipv4RoutingProtocol. Note many of the descriptions are from documentation on nsnam.org

        /*Query routing cache for an existing route, for an outbound packet.
        This lookup is used by transport protocols. It does not cause any packet to be forwarded, and is synchronous. 
        Can be used for multicast or unicast. The Linux equivalent is ip_route_output()
        The header input parameter may have an uninitialized value for the source address, but the destination address should always 
        be properly set by the caller.*/
        //https://www.nsnam.org/docs/release/3.19/doxygen/classns3_1_1_ipv4_routing_protocol.html#a9c0e9b77772a4974c06ee4577fe60547
        Ptr<Ipv4Route> RouteOutput(Ptr<Packet> p, const Ipv4Header &header, Ptr<NetDevice> oif, Socket::SocketErrno &sockerr) {
        Ipv4Address dest = header.GetDestination();
        if (m_lsroutingTable.find(dest) != m_lsroutingTable.end()) {
            Ipv4Address nextHop = m_lsroutingTable[dest];
            Ptr<Ipv4Route> route = Create<Ipv4Route>();
            route->SetDestination(dest);
            route->SetGateway(nextHop);
            uint32_t interfaceIndex = m_ipv4->GetInterfaceForAddress(nextHop);
            route->SetOutputDevice(m_ipv4->GetNetDevice(interfaceIndex));
            return route;
    }

            //https://www.nsnam.org/docs/release/3.19/doxygen/classns3_1_1_socket.html#ada1328c5ae0c28cb2a982caf8f6d6ccaa0f8ecb5a4ddbce3bade35fa12c3d49e8
            sockerr = Socket::ERROR_NOROUTETOHOST;
            return NULL;
        }

        /* Route an input packet (to be forwarded or locally delivered)
        This lookup is used in the forwarding process. The packet is handed over to the Ipv4RoutingProtocol, and 
        will get forwarded onward by one of the callbacks. The Linux equivalent is ip_route_input(). There are four 
        valid outcomes, and a matching callbacks to handle each.*/
        //https://www.nsnam.org/docs/release/3.19/doxygen/classns3_1_1_ipv4_routing_protocol.html#a67e815ff40ebb9f5f4eec4e22e23132e
        bool RouteInput(ns3::Ptr<const ns3::Packet> p, const ns3::Ipv4Header& header, ns3::Ptr<const ns3::NetDevice> idev, const UnicastForwardCallback& ucb, const MulticastForwardCallback& mcb, const LocalDeliverCallback& lcb, const ErrorCallback& ecb) override
        {
            Ipv4Address dest = header.GetDestination();
            if (m_lsroutingTable.find(dest) != m_lsroutingTable.end()) {
                Ipv4Address nextHop = m_lsroutingTable[dest];
                Ptr<Ipv4Route> route = Create<Ipv4Route>();
                route->SetDestination(dest);
                route->SetGateway(nextHop);
                uint32_t interfaceIndex = m_ipv4->GetInterfaceForAddress(nextHop);
                route->SetOutputDevice(m_ipv4->GetNetDevice(interfaceIndex));

                ucb(route, p, header);
                return true;
            }

            return false;
        }

        /*Print the Routing Table entries. */
        void PrintRoutingTable(ns3::Ptr<ns3::OutputStreamWrapper>, ns3::Time::Unit) const override
        {
        }

        /* Protocols are expected to implement this method to be notified of the state change of an interface in a node. */
        void NotifyInterfaceUp(uint32_t interface) override
        {
            UpdateRoutingTable();
        }

        /* Protocols are expected to implement this method to be notified of the state change of an interface in a node. */
        void NotifyInterfaceDown(uint32_t interface) override
        {   
            UpdateRoutingTable();
        }

        /* Protocols are expected to implement this method to be notified whenever a new address is added to an interface. 
            Typically used to add a 'network route' on an interface. Can be invoked on an up or down interface. */
        void NotifyAddAddress(uint32_t interface, Ipv4InterfaceAddress address) override {
            UpdateRoutingTable();
        }

        /* Protocols are expected to implement this method to be notified whenever a new address is removed from an interface. 
            Typically used to remove the 'network route' of an interface. Can be invoked on an up or down interface.
            */
        void NotifyRemoveAddress(uint32_t interface, Ipv4InterfaceAddress address) override {
           UpdateRoutingTable();
        }

        void SetIpv4(Ptr<Ipv4> ipv4) override
        {
            m_ipv4 = ipv4;
        }
        

    private:
        void SendLinkStateAdvertisement();
        void ReceiveLinkStateAdvertisement(Ptr<Socket> socket);
        void UpdateRoutingTable();
        Ptr<Ipv4> m_ipv4;
        std::map<Ipv4Address, Ptr<Socket>> m_socketMap;
        Ptr<LinkStateRouting> m_lsrouting;
        std::map<Ipv4Address, Ipv4Address> m_lsroutingTable;
};

// Create and send LSA packets
void LinkStateRoutingProtocol::SendLinkStateAdvertisement() {
    // Create an LSP for the router's current LSA
    LinkStatePacket lsp;
    lsp.sequenceNumber = m_lsrouting->GetLsdb().at(m_ipv4->GetAddress(0, 0).GetLocal()).sequenceNumber;
    lsp.lsa = m_lsrouting->GetLsdb().at(m_ipv4->GetAddress(0, 0).GetLocal());

    std::cout << "Router " << m_ipv4->GetAddress(0, 0).GetLocal()
              << " sending LSA with sequence number: " << lsp.sequenceNumber << std::endl;

    // Flood LSP to all neighbors
    for (auto& [neighborAddress, socket] : m_socketMap) {
        Ptr<Packet> packet = Create<Packet>((uint8_t*)&lsp, sizeof(LinkStatePacket));
        socket->Send(packet);
        std::cout << "LSP sent to neighbor: " << neighborAddress << std::endl;
    }
}

// Process received LSA packets and update routing table
void LinkStateRoutingProtocol::ReceiveLinkStateAdvertisement(Ptr<Socket> socket) {
    Ptr<Packet> packet = socket->Recv();
    LinkStatePacket receivedLsp;

    packet->CopyData((uint8_t*)&receivedLsp, sizeof(LinkStatePacket));
    std::cout << "Router " << m_ipv4->GetAddress(0, 0).GetLocal()
              << " received LSA from router: " << receivedLsp.lsa.routerAddress
              << " with sequence number: " << receivedLsp.sequenceNumber << std::endl;

    // Process the received LSP
    if (m_lsrouting->GetLsdb().at(receivedLsp.lsa.routerAddress).sequenceNumber < receivedLsp.sequenceNumber) {
        m_lsrouting->ProcessLsp(receivedLsp);
    }
}

// Compute shortest paths using Dijkstra’s algorithm
void LinkStateRoutingProtocol::UpdateRoutingTable() {
    m_lsroutingTable = m_lsrouting->ComputeRoutingTable();
}

//Constructor
LinkStateRoutingProtocol::LinkStateRoutingProtocol() 
    : m_ipv4(0)
{
    m_lsrouting = Create<LinkStateRouting>(Ipv4Address("0.0.0.0")); //how are we picking router ids?
    UpdateRoutingTable();
}

//Deconstructor
LinkStateRoutingProtocol::~LinkStateRoutingProtocol()
{
}

NS_OBJECT_ENSURE_REGISTERED(LinkStateRoutingProtocol);


/*note: inspiration from https://www.nsnam.org/docs/release/3.19/doxygen/aodv-helper_8cc_source.html#l00043
and https://www.nsnam.org/docs/release/3.19/doxygen/ipv4-nix-vector-helper_8cc_source.html#l00037 
Ipv4RoutingHelper defined at https://www.nsnam.org/docs/release/3.19/doxygen/classns3_1_1_ipv4_routing_helper.html
Lots of inspiration from cross referencing different implementations of derived classes in the docs of ns3 actually*/
class LinkStateRoutingHelper : public Ipv4RoutingHelper
{
public:
    LinkStateRoutingHelper();
    LinkStateRoutingHelper(const LinkStateRoutingHelper &);
    LinkStateRoutingHelper* Copy(void) const;
    virtual Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const;
    void Set(std::string name, const AttributeValue &value);

private:
    ObjectFactory m_agentFactory;
};

LinkStateRoutingHelper::LinkStateRoutingHelper()
{
    m_agentFactory.SetTypeId("LinkStateRoutingProtocol");
}

LinkStateRoutingHelper::LinkStateRoutingHelper(const LinkStateRoutingHelper &o)
    : m_agentFactory(o.m_agentFactory)
{
}

//virtual constructor mainly for internal use by the other helpers
LinkStateRoutingHelper* LinkStateRoutingHelper::Copy(void) const 
{
    return new LinkStateRoutingHelper(*this); 
}

//Creates a new routing protocol within node
Ptr<Ipv4RoutingProtocol> LinkStateRoutingHelper::Create(Ptr<Node> node) const
{
    Ptr<LinkStateRoutingProtocol> agent = m_agentFactory.Create<LinkStateRoutingProtocol>();
    node->AggregateObject(agent);
    return agent;
}

//Control the attributes of LinkStateRoutingProtocol
void LinkStateRoutingHelper::Set(std::string name, const AttributeValue &value)
{
    m_agentFactory.Set(name, value);
}

// Function to parse the topology file and return an adjacency list with costs
std::vector<std::vector<std::pair<uint32_t, double>>> ParseTopology(const std::string &filename, uint32_t &numNodes) {
    std::vector<std::vector<std::pair<uint32_t, double>>> adjacencyList;

    std::ifstream file(filename);
    if (!file.is_open()) {
        NS_FATAL_ERROR("Unable to open topology file: " << filename);
    }

    std::string line;
    uint32_t maxNodeId = 0;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        uint32_t src, dest;
        double cost;

        if (!(iss >> src >> dest >> cost)) {
            NS_FATAL_ERROR("Invalid topology file format.");
        }

        maxNodeId = std::max(maxNodeId, std::max(src, dest));
        if (adjacencyList.size() <= maxNodeId) {
            adjacencyList.resize(maxNodeId + 1);
        }

        adjacencyList[src].emplace_back(dest, cost);
        adjacencyList[dest].emplace_back(src, cost);
    }

    file.close();
    numNodes = maxNodeId + 1;
    return adjacencyList;
}


int main(int argc, char* argv[])
{
    //Use multi-line comment to easily switch between the two main tests
    CommandLine cmd;
    cmd.Parse(argc, argv);

    // Create nodes and install stack
    NodeContainer nodes;
    nodes.Create(5);

    PointToPointHelper p2p;
    p2p.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    p2p.SetChannelAttribute("Delay", StringValue("2ms"));

    Ipv4StaticRoutingHelper staticRouting;
    LinkStateRoutingHelper linkStateRouting;
    //As noted in header, this is here purely for testing purposes, to compare our solution against optimized link state routing on ns3
    OlsrHelper olsr;

    Ipv4ListRoutingHelper list;

    list.Add(linkStateRouting, 0);

    InternetStackHelper stack;
    stack.SetRoutingHelper(list);
    stack.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.0.0.0", "255.255.255.0");

    // Connect nodes and assign addresses
    for (uint32_t i = 0; i < nodes.GetN() - 1; ++i) {
        NetDeviceContainer devices = p2p.Install(nodes.Get(i), nodes.Get(i + 1));
        address.Assign(devices);
        address.NewNetwork();
    }

    // Print IP addresses
    std::cout << "IP Addresses of Nodes:" << std::endl;
    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        Ptr<Ipv4> ipv4 = nodes.Get(i)->GetObject<Ipv4>();
        Ipv4Address addr = ipv4->GetAddress(1, 0).GetLocal();
        std::cout << "Node " << i << " IP Address: " << addr << std::endl;
    }

    // Create and initialize LSAs for all nodes
    std::vector<Ptr<LinkStateRouting>> lsrs;
    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        Ptr<Ipv4> ipv4 = nodes.Get(i)->GetObject<Ipv4>();
        Ptr<LinkStateRouting> lsr = Create<LinkStateRouting>(ipv4->GetAddress(1, 0).GetLocal());
        lsrs.push_back(lsr);

        LinkStateAdvertisement lsa;
        lsa.routerAddress = ipv4->GetAddress(1, 0).GetLocal();
        lsa.sequenceNumber = 1;

        if (i > 0) {
            Link linkToPrev;
            linkToPrev.neighbor = nodes.Get(i - 1)->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();
            linkToPrev.cost = 1;
            linkToPrev.next_hop_interface = 0;
            lsa.links.push_back(linkToPrev);
        }
        if (i < nodes.GetN() - 1) {
            Link linkToNext;
            linkToNext.neighbor = nodes.Get(i + 1)->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();
            linkToNext.cost = 1;
            linkToNext.next_hop_interface = 1;
            lsa.links.push_back(linkToNext);
        }

        lsr->InitializeLsa(lsa);
    }

    // Simulate LSP flooding
    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        LinkStatePacket lsp;
        for (const auto& [router, lsa] : lsrs[i]->GetLsdb()) {
            lsp.sequenceNumber = lsa.sequenceNumber;
            lsp.lsa = lsa;  // Send the entire LSA
            for (uint32_t j = 0; j < nodes.GetN(); ++j) {
                if (j != i) {
                    lsrs[j]->ProcessLsp(lsp);
                }
            }
        }
    }

    // Print the LSDB for all nodes
    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        auto lsdb = lsrs[i]->GetLsdb();
        std::cout << "LSDB size for Node " << i << ": " << lsdb.size() << '\n';
        std::cout << "\nLink State Database for Node " << i << ":\n";
        for (const auto& [router, lsa] : lsdb) {
            std::cout << "Router: " << router << ", Sequence: " << lsa.sequenceNumber << ", Links:\n";
            for (const auto& link : lsa.links) {
                std::cout << "  Neighbor: " << link.neighbor
                          << ", Cost: " << link.cost
                          << ", Interface: " << link.next_hop_interface << '\n';
            }
        }
    }

    Simulator::Run();
    Simulator::Destroy();

    return 0;
    /*
    // Define Network Topology
    CommandLine cmd;
    cmd.Parse(argc, argv);

    LogComponentEnable("UdpEchoClientApplication", LOG_LEVEL_INFO);
    LogComponentEnable("UdpEchoServerApplication", LOG_LEVEL_INFO);

    // Parse topology from file
    std::string topologyFile = "router_network.txt";
    uint32_t numNodes = 0; // Variable to hold the number of nodes
    std::vector<std::vector<std::pair<uint32_t, double>>> topology = ParseTopology(topologyFile, numNodes);

    // Build the topology
    NodeContainer nodes;
    nodes.Create(numNodes);

    PointToPointHelper pointToPoint;
    pointToPoint.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
    pointToPoint.SetChannelAttribute("Delay", StringValue("2ms"));

    Ipv4StaticRoutingHelper staticRouting;
    LinkStateRoutingHelper linkStateRouting;
    //As noted in header, this is here purely for testing purposes, to compare our solution against optimized link state routing on ns3
    OlsrHelper olsr;

    Ipv4ListRoutingHelper list;

    list.Add(linkStateRouting, 0);

    InternetStackHelper stack;
    stack.SetRoutingHelper(list);
    stack.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.0.0.0", "255.255.255.0");

    // Connect nodes as per topology
    for (uint32_t i = 0; i < topology.size(); ++i) {
        for (const auto& link : topology[i]) {
            uint32_t toNode = link.first;
            double cost = link.second;

            NetDeviceContainer devices = pointToPoint.Install(nodes.Get(i), nodes.Get(toNode));
            std::ostringstream delay;
            delay << cost << "ms";
            pointToPoint.SetChannelAttribute("Delay", StringValue(delay.str()));

            // Assign IP addresses to the devices
            address.Assign(devices);
            address.NewNetwork();
        }
    }

    // Print the topology with costs
    std::cout << "Network Topology (with Costs):" << std::endl;
    for (uint32_t i = 0; i < topology.size(); ++i) {
        for (const auto& link : topology[i]) {
            std::cout << "Node " << i << " <--> Node " << link.first
                      << " (Delay: " << link.second << "ms, Cost: " << link.second << ")" << std::endl;
        }
    }

    // Print the IP addresses of all nodes
    std::cout << "\nIP Addresses of Nodes:" << std::endl;
    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        Ptr<Ipv4> ipv4 = nodes.Get(i)->GetObject<Ipv4>();
        Ipv4Address address = ipv4->GetAddress(1, 0).GetLocal();
        std::cout << "Node " << i << " IP Address: " << address << std::endl;
    }

    // Echo Application Setup
    uint16_t port = 9;

    // Install Echo Server on Node 2
    UdpEchoServerHelper echoServer(port);
    ApplicationContainer serverApps = echoServer.Install(nodes.Get(2)); // Node 2
    serverApps.Start(Seconds(1.0));
    serverApps.Stop(Seconds(10.0));

    // Install Echo Client on Node 0
    UdpEchoClientHelper echoClient(Ipv4Address("10.0.1.2"), port); // Node 2's IP
    echoClient.SetAttribute("MaxPackets", UintegerValue(3));
    echoClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
    echoClient.SetAttribute("PacketSize", UintegerValue(1024));

    ApplicationContainer clientApps = echoClient.Install(nodes.Get(0)); // Node 0
    clientApps.Start(Seconds(2.0));
    clientApps.Stop(Seconds(10.0));

    Simulator::Run();
    Simulator::Destroy();

    return 0;
    */
}
