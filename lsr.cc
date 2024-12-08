#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "link-state-routing-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("LSRScript"); 

class LinkStateRoutingProtocol : public Ipv4RoutingProtocol {
    public:
        /* commented out due to compile issues with the rest unimplemented properly
        static TypeId GetTypeId(void)
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
                route->SetSource(); //TODO: get source for route and set it to that
                route->SetDestination(Ipv4Address(dest));
                route->SetGateway(Ipv4Address(next));
                route->SetOutputDevice(oif);
                
                return route;
            }

            //https://www.nsnam.org/docs/release/3.19/doxygen/classns3_1_1_socket.html#ada1328c5ae0c28cb2a982caf8f6d6ccaa0f8ecb5a4ddbce3bade35fa12c3d49e8
            sockerr = Socket::ERROR_NOROUTETOHOST;
            return NULL;
        }
        bool RouteInput(Ptr<const Packet> p, const Ipv4Header &header, Ptr<const NetDevice> idev, UnicastForwardCallback ucb, MulticastForwardCallback mcb, LocalDeliverCallback lcb, ErrorCallback ecb) override
        {
            uint32_t dest = header.GetDestination().Get();
            
            if (m_lsroutingTable.find(dest) != m_lsroutingTable.end()) {
                uint32_t next = m_lsroutingTable[dest];
                ucb(p, header, Ipv4Address(next));
                return true;
            }
            
            ecb(p, header);
            return false;
        }

        /* Protocols are expected to implement this method to be notified of the state change of an interface in a node. */
        void NotifyInterfaceUp(uint32_t interface) override
        {
            LinkStateAdvertisement newlsa;

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
    //m_lsrouting = Create<LinkStateRouting>(0); //how are we picking router ids?
    UpdateRoutingTable();
}

//Deconstructor
LinkStateRoutingProtocol::~LinkStateRoutingProtocol()
{
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
    //LinkStateRoutingHelper linkStateRouting;              commented due to throwing error since the header's not implemented

    Ipv4ListRoutingHelper list;
    ///list.Add(linkStateRouting, 0);               commented due to declaration commented

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
