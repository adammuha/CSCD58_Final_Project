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
        // Inherited methods from Ipv4RoutingProtocol

        /*Query routing cache for an existing route, for an outbound packet.
        This lookup is used by transport protocols. It does not cause any packet to be forwarded, and is synchronous. 
        Can be used for multicast or unicast. The Linux equivalent is ip_route_output()
        The header input parameter may have an uninitialized value for the source address, but the destination address should always 
        be properly set by the caller.*/
        //https://www.nsnam.org/docs/release/3.19/doxygen/classns3_1_1_ipv4_routing_protocol.html#a9c0e9b77772a4974c06ee4577fe60547
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

        /* Route an input packet (to be forwarded or locally delivered)
        This lookup is used in the forwarding process. The packet is handed over to the Ipv4RoutingProtocol, and 
        will get forwarded onward by one of the callbacks. The Linux equivalent is ip_route_input(). There are four 
        valid outcomes, and a matching callbacks to handle each.*/
        //https://www.nsnam.org/docs/release/3.19/doxygen/classns3_1_1_ipv4_routing_protocol.html#a67e815ff40ebb9f5f4eec4e22e23132e
        //bool RouteInput(Ptr<const Packet> p, const Ipv4Header &header, Ptr<const NetDevice> idev, UnicastForwardCallback ucb, MulticastForwardCallback mcb, LocalDeliverCallback lcb, ErrorCallback ecb) override
        // NOTE: TODO /POSSIBLE ISSUE: FOR SOME REASON IT DOESN'T COUNT IT AS OVERRIDE UNLESS THE ARGUMENTS HAVE CALLBACK& INSTEAD OF CALLBACK. THE DOCS ARE DIFFERENT THOUGH.
        bool RouteInput(ns3::Ptr<const ns3::Packet> p, const ns3::Ipv4Header& header, ns3::Ptr<const ns3::NetDevice> idev, const UnicastForwardCallback& ucb, const MulticastForwardCallback& mcb, const LocalDeliverCallback& lcb, const ErrorCallback& ecb) override
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

        void PrintRoutingTable(ns3::Ptr<ns3::OutputStreamWrapper>, ns3::Time::Unit) const override
        {

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
    : m_agentFactory (o.m_agentFactory)
{
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
    // Users may find it convenient to turn on explicit debugging
  // for selected modules; the below lines suggest how to do this
#if 0 
  LogComponentEnable ("SimpleGlobalRoutingExample", LOG_LEVEL_INFO);
#endif

  // Set up some default values for the simulation.  Use the 

  Config::SetDefault ("ns3::OnOffApplication::PacketSize", UintegerValue (210));
  Config::SetDefault ("ns3::OnOffApplication::DataRate", StringValue ("448kb/s"));

  //DefaultValue::Bind ("DropTailQueue::m_maxPackets", 30);

  // Allow the user to override any of the defaults and the above
  // DefaultValue::Bind ()s at run-time, via command-line arguments
  CommandLine cmd;
  cmd.Parse (argc, argv);

  // Here, we will explicitly create four nodes.  In more sophisticated
  // topologies, we could configure a node factory.
  NS_LOG_INFO ("Create nodes.");
  NodeContainer c;
  c.Create (5);
  NodeContainer n02 = NodeContainer (c.Get (0), c.Get (2));
  NodeContainer n12 = NodeContainer (c.Get (1), c.Get (2));
  NodeContainer n32 = NodeContainer (c.Get (3), c.Get (2));
  NodeContainer n34 = NodeContainer (c.Get (3), c.Get (4));

  // Enable OLSR
  NS_LOG_INFO ("Enabling OLSR Routing.");
  OlsrHelper olsr;

  Ipv4StaticRoutingHelper staticRouting;

  LinkStateRoutingHelper linkStateRouting;

  Ipv4ListRoutingHelper list;
  //list.Add (staticRouting, 0);
  list.Add (olsr, 0);
  //list.Add (linkStateRouting, 0);

  InternetStackHelper internet;
  internet.SetRoutingHelper (list); // has effect on the next Install ()
  internet.Install (c);

  // We create the channels first without any IP addressing information
  NS_LOG_INFO ("Create channels.");
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue ("5Mbps"));
  p2p.SetChannelAttribute ("Delay", StringValue ("2ms"));
  NetDeviceContainer nd02 = p2p.Install (n02);
  NetDeviceContainer nd12 = p2p.Install (n12);
  p2p.SetDeviceAttribute ("DataRate", StringValue ("1500kbps"));
  p2p.SetChannelAttribute ("Delay", StringValue ("10ms"));
  NetDeviceContainer nd32 = p2p.Install (n32);
  NetDeviceContainer nd34 = p2p.Install (n34);

  // Later, we add IP addresses.
  NS_LOG_INFO ("Assign IP Addresses.");
  Ipv4AddressHelper ipv4;
  ipv4.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer i02 = ipv4.Assign (nd02);

  ipv4.SetBase ("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer i12 = ipv4.Assign (nd12);

  ipv4.SetBase ("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer i32 = ipv4.Assign (nd32);

  ipv4.SetBase ("10.1.4.0", "255.255.255.0");
  Ipv4InterfaceContainer i34 = ipv4.Assign (nd34);

  // Create the OnOff application to send UDP datagrams of size
  // 210 bytes at a rate of 448 Kb/s from n0 to n4
  NS_LOG_INFO ("Create Applications.");
  uint16_t port = 9;   // Discard port (RFC 863)

  OnOffHelper onoff ("ns3::UdpSocketFactory", 
                     InetSocketAddress (i34.GetAddress (1), port));
  onoff.SetConstantRate (DataRate ("448kb/s"));

  ApplicationContainer apps = onoff.Install (c.Get (0));
  apps.Start (Seconds (1.0));
  apps.Stop (Seconds (10.0));

  // Create a packet sink to receive these packets
  PacketSinkHelper sink ("ns3::UdpSocketFactory",
                         InetSocketAddress (Ipv4Address::GetAny (), port));

  apps = sink.Install (c.Get (3));
  apps.Start (Seconds (1.0));
  apps.Stop (Seconds (10.0));

  // Create a similar flow from n3 to n1, starting at time 1.1 seconds
  onoff.SetAttribute ("Remote",
                      AddressValue (InetSocketAddress (i12.GetAddress (0), port)));
  apps = onoff.Install (c.Get (3));
  apps.Start (Seconds (1.1));
  apps.Stop (Seconds (10.0));

  // Create a packet sink to receive these packets
  apps = sink.Install (c.Get (1));
  apps.Start (Seconds (1.1));
  apps.Stop (Seconds (10.0));

  AsciiTraceHelper ascii;
  p2p.EnableAsciiAll (ascii.CreateFileStream ("simple-point-to-point-olsr.tr"));
  p2p.EnablePcapAll ("simple-point-to-point-olsr");

  Simulator::Stop (Seconds (30));

  NS_LOG_INFO ("Run Simulation.");
  Simulator::Run ();
  Simulator::Destroy ();
  NS_LOG_INFO ("Done.");

  return 0;
}
