//
// Simple program to perform SMA speedwire device discovery
// https://www.sma.de/fileadmin/content/global/Partner/Documents/sma_developer/SpeedwireDD-TI-de-10.pdf
//
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#ifdef _WIN32
#include <Winsock2.h>
#include <Ws2tcpip.h>
#define poll(a, b, c)  WSAPoll((a), (b), (c))
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#endif

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <LocalHost.hpp>
#include <AddressConversion.hpp>
#include <SpeedwireByteEncoding.hpp>
#include <SpeedwireHeader.hpp>
#include <SpeedwireEmeterProtocol.hpp>
#include <SpeedwireInverterProtocol.hpp>
#include <SpeedwireCommand.hpp>
#include <SpeedwireSocket.hpp>
#include <SpeedwireSocketFactory.hpp>
#include <SpeedwireDevice.hpp>
#include <SpeedwireDiscovery.hpp>
using namespace libspeedwire;


//! Multicast device discovery request packet, according to SMA documentation.
const unsigned char  SpeedwireDiscovery::multicast_request[] = {
    0x53, 0x4d, 0x41, 0x00, 0x00, 0x04, 0x02, 0xa0,     // sma signature, 0x0004 length, 0x02a0 tag0
    0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x20,     // 0xffffffff group, 0x0000 length, 0x0020 discovery tag
    0x00, 0x00, 0x00, 0x00                              // 0x0000 length, 0x0000 end-of-data tag
};

const unsigned char  SpeedwireDiscovery::multicast_response[] = {
    0x53, 0x4d, 0x41, 0x00, 0x00, 0x04, 0x02, 0xa0,     // sma signature, 0x0004 length, 0x02a0 tag0
    0x00, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00,     // 0x00000001 group, 0x0002 length, 0x0000 tag
    0x00, 0x01                                          // 0x00000001 payload
    // after this mandatory fixed part, more tags are typically following:
    // tag 0x0010 - data2
    // tag 0x0020 - discovery
    // tag 0x0030 - ip address
    // tag 0x0040 - ?
    // tag 0x0070 - ?
    // tag 0x0080 - ?
    // tag 0x0000 - end of data
};

#if 0
// response from SBS2.5
534d4100                        // sma signature            => required
0004 02a0 00000001              // tag0, group 0x00000001   => required
0002 0000 0001                  // ??                       => required
0004 0010 0001 0003             // data2 tag  protocolid=0x0001, long words=0, control=0x03
0004 0020 0000 0001             // discovery tag, 0x00000001 ??
0004 0030 c0a8b216              // ip tag, 192.168.178.22
0002 0070 ef0c                  // 0x0070 tag, 0xef0c
0001 0080 00                    // 0x0080 tag, 0x00
0000 0000                       // end of data tag
#endif
#if 0
// response from ST5.0
534d4100                        // sma signature            => required
0004 02a0 00000001              // tag0, group 0x00000001   => required
0002 0000 0001                  // ??                       => required
0004 0010 0001 0003             // data2 tag  protocolid=0x0001, long words=0, control=0x03
0004 0020 0000 0001             // discovery tag, 0x00000001 ??
0004 0030 c0a8b216              // ip tag, 192.168.178.22
0004 0040 00000000              // 0x0040 tag, 0x00000000
0002 0070 ef0c                  // 0x0070 tag, 0xef0c
0001 0080 00                    // 0x0080 tag, 0x00
0000 0000                       // end of data tag
#endif


//! Unicast device discovery request packet, according to SMA documentation
const unsigned char  SpeedwireDiscovery::unicast_request[] = {
    0x53, 0x4d, 0x41, 0x00, 0x00, 0x04, 0x02, 0xa0,     // sma signature, 0x0004 length, 0x02a0 tag0
    0x00, 0x00, 0x00, 0x01, 0x00, 0x26, 0x00, 0x10,     // 0x00000001 group, 0x0026 length, 0x0010 "SMA Net 2", Version 0
    0x60, 0x65, 0x09, 0xa0, 0xff, 0xff, 0xff, 0xff,     // 0x6065 protocol, 0x09 #long words, 0xa0 ctrl, 0xffff dst susyID any, 0xffffffff dst serial any
    0xff, 0xff, 0x00, 0x00, 0x7d, 0x00, 0x52, 0xbe,     // 0x0000 dst cntrl, 0x007d src susy id, 0x3a28be52 src serial
    0x28, 0x3a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     // 0x0000 src cntrl, 0x0000 error code, 0x0000 fragment ID
    0x01, 0x80, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,     // 0x8001 packet ID, 0x0200 command ID, 0x00000000 first register id
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     // 0x00000000 last register id, 0x00000000 trailer
    0x00, 0x00
};

// Request: 534d4100000402a00000000100260010 606509a0 ffffffffffff0000 7d0052be283a0000 000000000180 00020000 00000000 00000000 00000000  => command = 0x00000200, first = 0x00000000; last = 0x00000000; trailer = 0x00000000
// Response 534d4100000402a000000001004e0010 606513a0 7d0052be283a00c0 7a01842a71b30000 000000000180 01020000 00000000 00000000 00030000 00ff0000 00000000 01007a01 842a71b3 00000a00 0c000000 00000000 00000000 01010000 00000000

// Response from inverter: id 0x00000300 (Discovery) conn 0x00 type 0x00 (Unsigned32) time 0x0000ff00  data   0x00000000  0x017a0001  0xb3712a84  0x000a0000  0x0000000c  0x00000000  0x00000000  0x00000101
// Response from battery:  id 0x00000300 (Discovery) conn 0x00 type 0x00 (Unsigned32) time 0x0000ff00  data   0x60024170  0x015a0001  0x714f5e45  0x000a0000  0x0000000c  0x00000000  0x00000003  0x00000101

/**
 *  Constructor.
 *  @param host A reference to the LocalHost instance of this machine.
 */
SpeedwireDiscovery::SpeedwireDiscovery(LocalHost& host) :
    localhost(host)
{
    // assemble multicast device discovery packet
    //unsigned char mreq[20];
    //SpeedwireProtocol mcast(mreq, sizeof(mreq));
    //mcast.setDefaultHeader(0xffffffff, 0, 0x0000);
    //mcast.setNetworkVersion(0x0020);
    //if (memcmp(mreq, multicast_request, sizeof(mreq) != 0)) {
    //    perror("diff");
    //}

    // assemble unicast device discovery packet
    //unsigned char ureq[58];
    //SpeedwireProtocol ucast(ureq, sizeof(ureq));
    //ucast.setDefaultHeader(1, 0x26, SpeedwireProtocol::sma_inverter_protocol_id);
    //ucast.setControl(0xa0);
    //SpeedwireInverter uinv(ucast);
    //uinv.setDstSusyID(0xffff);
    //uinv.setDstSerialNumber(0xffffffff);
    //uinv.setDstControl(0);
    //uinv.setSrcSusyID(0x007d);
    //uinv.setSrcSerialNumber(0x3a28be42);
    //uinv.setSrcControl(0);
    //uinv.setErrorCode(0);
    //uinv.setFragmentID(0);
    //uinv.setPacketID(0x8001);
    //uinv.setCommandID(0x00000200);
    //uinv.setFirstRegisterID(0x00000000);
    //uinv.setLastRegisterID(0x00000000);
    //uinv.setDataUint32(0, 0x00000000);  // set trailer
    //if (memcmp(ureq, unicast_request, sizeof(ureq) != 0)) {
    //    perror("diff");
    //}
}


/**
 *  Destructor - clear the device list.
 */
SpeedwireDiscovery::~SpeedwireDiscovery(void) {
    speedwireDevices.clear();
}


/**
 *  Pre-register a device IP address, i.e. just provide the ip address of the device.
 *  A pre-registered device is explicitly queried during the device discovery process. As this discovery 
 *  is based on unicast upd packets, the ip address can be on a different subnet or somewhere on the internet.
 *  @param peer_ip_address The IP address of the speedwire peer in dot (ipv4) or colon (ipv6) notation.
 */
bool SpeedwireDiscovery::preRegisterDevice(const std::string peer_ip_address) {
    SpeedwireDevice info;
    info.peer_ip_address = peer_ip_address;
    bool new_device = true;
    for (auto& device : speedwireDevices) {
        if (info.peer_ip_address == device.peer_ip_address) {
            new_device = false;
        }
    }
    if (new_device == true) {
        speedwireDevices.push_back(info);
    }
    return new_device;
}


/**
 *  Pre-register a required device by its serial number.
 *  A reqistered device will receive more discovery effort, in case it is not detected.
 *  is based on unicast upd packets, the ip address can be on a different subnet or somewhere on the internet.
 *  @param serial_number the serial number of the required device.
 */
bool SpeedwireDiscovery::requireDevice(const uint32_t serial_number) {
    SpeedwireDevice info;
    info.serialNumber = serial_number;
    bool new_device = true;
    for (auto& device : speedwireDevices) {
        if (info.serialNumber == device.serialNumber) {
            new_device = false;
        }
    }
    if (new_device == true) {
        speedwireDevices.push_back(info);
    }
    return new_device;
}


/**
 *  Fully register a device, i.e. provide a full information data set of the device.
 */
bool SpeedwireDiscovery::registerDevice(const SpeedwireDevice& info) {
    bool new_device = true;
    bool updated_device = false;
    for (auto& device : speedwireDevices) {
        if (device.hasIPAddressOnly() && info.peer_ip_address == device.peer_ip_address) {
            device = info;
            new_device = false;
            updated_device = true;
        }
        else if (device.hasSerialNumberOnly() && info.serialNumber == device.serialNumber) {
            device = info;
            new_device = false;
            updated_device = true;
        }
        else if (device.isComplete() && info == device) {
            device = info;
            new_device = false;
            //updated_device = true;
        }
    }
    if (new_device) {
        speedwireDevices.push_back(info);
        updated_device = true;
    }
    // remove duplicate device entries, this can happen if the same device was pre-registered by IP and by serial number
    if (new_device == false) {
        for (std::vector<SpeedwireDevice>::iterator it1 = speedwireDevices.begin(); it1 != speedwireDevices.end(); it1++) {
            for (std::vector<SpeedwireDevice>::iterator it2 = it1 + 1; it2 != speedwireDevices.end(); ) {
                if (*it1 == *it2) {
                    it2 = speedwireDevices.erase(it2);
                }
                else {
                    it2++;
                }
            }
        }
    }
    return updated_device;
}


/**
 *  Unregister a device, i.e. delete it from the device list.
 */
void SpeedwireDiscovery::unregisterDevice(const SpeedwireDevice& info) {
    for (std::vector<SpeedwireDevice>::iterator it = speedwireDevices.begin(); it != speedwireDevices.end(); ) {
        if (*it == info) {
            it = speedwireDevices.erase(it);
        } else {
            it++;
        }
    }
}


/**
 *  Get a vector of all known devices.
 */
const std::vector<SpeedwireDevice>& SpeedwireDiscovery::getDevices(void) const {
    return speedwireDevices;
}


/**
 *  Get the number of all pre-registered devices, where just the ip address is known.
 */
unsigned long SpeedwireDiscovery::getNumberOfPreRegisteredIPDevices(void) const {
    unsigned long count = 0;
    for (const auto& device : speedwireDevices) {
        count += (device.hasIPAddressOnly() ? 1 : 0);
    }
    return count;
}


/**
 *  Get the number of all pre-registered required devices, but they are still not yet discovered.
 */
unsigned long SpeedwireDiscovery::getNumberOfMissingDevices(void) const {
    unsigned long count = 0;
    for (const auto& device : speedwireDevices) {
        count += (device.hasSerialNumberOnly() ? 1 : 0);
    }
    return count;
}


/**
 *  Get the number of all fully registered devices.
 */
unsigned long SpeedwireDiscovery::getNumberOfFullyRegisteredDevices(void) const {
    unsigned long count = 0;
    for (const auto& device : speedwireDevices) {
        count += (device.isComplete() ? 1 : 0);
    }
    return count;
}


/**
 *  Get the number of all known devices.
 */
unsigned long SpeedwireDiscovery::getNumberOfDevices(void) const {
    return (unsigned long)speedwireDevices.size();
}


/**
 *  Try to find SMA devices on the networks connected to this host.
 *  @return the number of discovered devices
 */
int SpeedwireDiscovery::discoverDevices(const bool full_scan) {

    // get a list of all local ipv4 interface addresses
    const std::vector<std::string>& localIPs = localhost.getLocalIPv4Addresses();

    // allocate a pollfd structure for each local ip address and populate it with the socket fds
    std::vector<SpeedwireSocket> sockets = SpeedwireSocketFactory::getInstance(localhost)->getRecvSockets(SpeedwireSocketFactory::SocketType::ANYCAST, localIPs);
    std::vector<struct pollfd> fds;

    for (auto& socket : sockets) {
        struct pollfd pfd;
        pfd.fd = socket.getSocketFd();
        fds.push_back(pfd);
    }

    // configure state machine for discovery requests
    const uint64_t maxWaitTimeInMillis = 2000u;
    uint64_t startTimeInMillis = localhost.getTickCountInMs();
    size_t broadcast_counter = 0;
    size_t wakeup_counter = 0;
    size_t prereg_counter = 0;
    size_t subnet_counter = (full_scan ? 1 : 0xffffffff);
    size_t socket_counter = 0;

    while ((localhost.getTickCountInMs() - startTimeInMillis) < maxWaitTimeInMillis) {

        // prepare pollfd structure
        for (auto& pfd : fds) {
            pfd.events = POLLIN;
            pfd.revents = 0;
        }

        // send discovery request packet and update counters
        for (int i = 0; i < 10; ++i) {
            if (sendNextDiscoveryPacket(broadcast_counter, wakeup_counter, prereg_counter, subnet_counter, socket_counter) == false) {
                break;  // done with sending all discovery packets
            }
            startTimeInMillis = localhost.getTickCountInMs();
        }

        // wait for inbound packets on any of the configured sockets
        //fprintf(stdout, "poll() ...\n");
        if (poll(fds.data(), (uint32_t)fds.size(), 10) < 0) {     // timeout 10 ms
            perror("poll failed");
            break;
        }
        //fprintf(stdout, "... done\n");

        // determine the socket that received a packet
        for (int j = 0; j < fds.size(); ++j) {
            if ((fds[j].revents & POLLIN) != 0) {

                // read packet data, analyze it and create a device information record
                recvDiscoveryPackets(sockets[j]);
            }
        }
    }

    for (const auto& device : speedwireDevices) {
#if 1
        if (!device.hasSerialNumberOnly()) {
            // try to get further information about the device by querying device type information from the peer
            SpeedwireCommand command(localhost, speedwireDevices);
            SpeedwireDevice updatedDevice = command.queryDeviceType(device);
            if (updatedDevice.isComplete()) {
                registerDevice(updatedDevice);
                printf("%s\n", updatedDevice.toString().c_str());
            }
        }
#else
        printf("%s\n", device.toString().c_str());
#endif
    }

    // return the number of discovered and fully registered devices
    return getNumberOfFullyRegisteredDevices();
}


/**
 *  Send discovery packets. For now this is only for ipv4 peers.
 *  State machine implementing the following sequence of packets:
 *  - multicast speedwire discovery requests to all interfaces
 *  - unicast speedwire discovery requests to pre-registered hosts
 *  - unicast speedwire discovery requests to all hosts on the network (only if the network prefix is < /16)
 */
bool SpeedwireDiscovery::sendNextDiscoveryPacket(size_t& broadcast_counter, size_t& wakeup_counter, size_t& prereg_counter, size_t& subnet_counter, size_t& socket_counter) {

    // sequentially first send multicast speedwire discovery requests
    const std::vector<std::string>& localIPs = localhost.getLocalIPv4Addresses();
    if (broadcast_counter < localIPs.size()) {
        const std::string& addr = localIPs[broadcast_counter];
        SpeedwireSocket socket = SpeedwireSocketFactory::getInstance(localhost)->getSendSocket(SpeedwireSocketFactory::SocketType::UNICAST, addr);  // must be UNICAST socket to find the correct interface(!)
        //fprintf(stdout, "send broadcast discovery request to %s (via interface %s)\n", AddressConversion::toString(socket.getSpeedwireMulticastIn4Address()).c_str(), socket.getLocalInterfaceAddress().c_str());
        int nbytes = socket.send(multicast_request, sizeof(multicast_request));
        broadcast_counter++;
        return true;
    }
    // followed by a wakeup sequence for all preregistered devices
    static const int num_retries = 1;
    if (wakeup_counter < num_retries * localIPs.size()) {
        // then send the same multicast speedwire discovery requests as a unicast request to all pre-registered devices
        for (auto& device : speedwireDevices) {
            if (device.hasIPAddressOnly()) {
                const std::string& addr = localIPs[wakeup_counter / num_retries];
                struct in_addr if_addr  = AddressConversion::toInAddress(addr);
                struct in_addr dev_addr = AddressConversion::toInAddress(device.peer_ip_address);
                if (AddressConversion::resideOnSameSubnet(if_addr, dev_addr, 24)) {
                    SpeedwireSocket socket = SpeedwireSocketFactory::getInstance(localhost)->getSendSocket(SpeedwireSocketFactory::SocketType::UNICAST, addr);
                    sockaddr_in sockaddr;
                    sockaddr.sin_family = AF_INET;
                    sockaddr.sin_addr = dev_addr;
                    sockaddr.sin_port = htons(9522);
                    //fprintf(stdout, "send multicast discovery request to %s (via interface %s)\n", device.peer_ip_address.c_str(), socket.getLocalInterfaceAddress().c_str());
                    int nbytes = socket.sendto(multicast_request, sizeof(multicast_request), sockaddr);
                    if (num_retries > 1) {
                        printf("wait for %s to wake up ...\n", device.peer_ip_address.c_str());
                        LocalHost::sleep(1000);
                    }
                }
            }
        }
        wakeup_counter++;
        return true;
    }
    // followed by pre-registered ip addresses
    if (prereg_counter < localIPs.size()) {
        for (auto& device : speedwireDevices) {
            if (device.hasIPAddressOnly()) {
                SpeedwireSocket socket = SpeedwireSocketFactory::getInstance(localhost)->getSendSocket(SpeedwireSocketFactory::SocketType::UNICAST, localIPs[prereg_counter]);
                sockaddr_in sockaddr;
                sockaddr.sin_family = AF_INET;
                sockaddr.sin_addr = AddressConversion::toInAddress(device.peer_ip_address);
                sockaddr.sin_port = htons(9522);
                //fprintf(stdout, "send unicast discovery request to %s (via interface %s)\n", device.peer_ip_address.c_str(), socket.getLocalInterfaceAddress().c_str());
                int nbytes = socket.sendto(unicast_request, sizeof(unicast_request), sockaddr);
            }
        }
        prereg_counter++;
        return true;
    }
    // followed by unicast speedwire discovery requests
    if (socket_counter < localIPs.size()) {
        // determine address range of local subnet
        const std::string& addr = localIPs[socket_counter];
        uint32_t subnet_length = 32 - localhost.getInterfacePrefixLength(addr);
        uint32_t max_subnet_counter = ((uint32_t)1 << subnet_length) - 1;
        // skip full scan if the local network prefix is < /16
        if (max_subnet_counter > 0xffff) {
            subnet_counter = max_subnet_counter;
        }
        if (subnet_counter < max_subnet_counter) {
            // assemble address of the recipient
            struct in_addr inaddr = AddressConversion::toInAddress(addr);
            uint32_t saddr = ntohl(inaddr.s_addr);      // ip address of the interface
            saddr = saddr & (~max_subnet_counter);      // mask subnet addresses, such that the subnet part is 0
            saddr = saddr + (uint32_t)subnet_counter;   // add subnet address
            sockaddr_in sockaddr;
            sockaddr.sin_family = AF_INET;
            sockaddr.sin_addr.s_addr = htonl(saddr);
            sockaddr.sin_port = htons(9522);
            // send to socket
            SpeedwireSocket socket = SpeedwireSocketFactory::getInstance(localhost)->getSendSocket(SpeedwireSocketFactory::SocketType::UNICAST, addr);
            //fprintf(stdout, "send unicast discovery request to %s (via interface %s)\n", localhost.toString(sockaddr).c_str(), socket.getLocalInterfaceAddress().c_str());
            int nbytes = socket.sendto(unicast_request, sizeof(unicast_request), sockaddr);
            ++subnet_counter;
            return true;
        }
        // proceed with the next local interface
        if (subnet_counter >= max_subnet_counter && socket_counter < localIPs.size()) {
            subnet_counter = 1;
            ++socket_counter;
            return true;
        }
    }
    return false;
}


/**
 *  Receive a discovery packet, analyze it and create a device information record.
 */
bool SpeedwireDiscovery::recvDiscoveryPackets(const SpeedwireSocket& socket) {
    bool result = false;

    std::string peer_ip_address;
    char udp_packet[1600];
    int nbytes = 0;
    if (socket.isIpv4()) {
        struct sockaddr_in addr;
        nbytes = socket.recvfrom(udp_packet, sizeof(udp_packet), addr);
        addr.sin_port = 0;
        peer_ip_address = AddressConversion::toString(addr);
    }
    else if (socket.isIpv6()) {
        struct sockaddr_in6 addr;
        nbytes = socket.recvfrom(udp_packet, sizeof(udp_packet), addr);
        addr.sin6_port = 0;
        peer_ip_address = AddressConversion::toString(addr);
    }
    if (nbytes > 0) {
        SpeedwireHeader protocol(udp_packet, nbytes);

        // check for speedwire device discovery responses
        if (protocol.isValidDiscoveryPacket()) {

            // find ip address tag packet
            const void* ipaddr_ptr = protocol.findTagPacket(SpeedwireTagHeader::sma_tag_ip_address);
            if (ipaddr_ptr != NULL) {

                // check size of ip address packet, it must be >= 4 to hold at least an ipv4 address
                uint16_t ipaddr_size = SpeedwireTagHeader::getTagLength(ipaddr_ptr);
                if (ipaddr_size >= 4) {

                    // extract ip address and pre-register it as a new device
                    struct in_addr in;
                    in.S_un.S_addr = SpeedwireByteEncoding::getUint32LittleEndian((uint8_t*)ipaddr_ptr + SpeedwireTagHeader::TAG_HEADER_LENGTH);
                    std:: string ip = AddressConversion::toString(in);
                    printf("received speedwire discovery response packet from %s - ipaddr tag %s\n", peer_ip_address.c_str(), ip.c_str());
                    preRegisterDevice(ip);
                }
            }
        }
        else if (protocol.isValidData2Packet()) {

            SpeedwireData2Packet data2_packet(protocol);
            uint16_t length = data2_packet.getTagLength();
            uint16_t protocolID = data2_packet.getProtocolID();

            // check for emeter protocol
            if (SpeedwireData2Packet::isEmeterProtocolID(protocolID) || SpeedwireData2Packet::isExtendedEmeterProtocolID(protocolID)) {
                //LocalHost::hexdump(udp_packet, nbytes);
                SpeedwireEmeterProtocol emeter(protocol);
                SpeedwireDevice device;
                device.susyID = emeter.getSusyID();
                device.serialNumber = emeter.getSerialNumber();
                const SpeedwireDeviceType &device_type = SpeedwireDeviceType::fromSusyID(device.susyID);
                if (device_type.deviceClass != SpeedwireDeviceClass::UNKNOWN) {
                    device.deviceClass = toString(device_type.deviceClass);
                    device.deviceModel = device_type.name;
                }
                else {
                    device.deviceClass = "Emeter";
                    device.deviceModel = "Emeter";
                }
                device.peer_ip_address = peer_ip_address;
                device.interface_ip_address = localhost.getMatchingLocalIPAddress(peer_ip_address);
                if (device.interface_ip_address == "" && socket.isIpAny() == false) {
                    device.interface_ip_address = socket.getLocalInterfaceAddress();
                }
                if (registerDevice(device)) {
                    printf("found susyid %u serial %lu ip %s\n", device.susyID, device.serialNumber, device.peer_ip_address.c_str());
                    result = true;
                }
            }
            // check for inverter protocol and ignore loopback packets
            else if (SpeedwireData2Packet::isInverterProtocolID(protocolID) &&
                (nbytes != sizeof(unicast_request) || memcmp(udp_packet, unicast_request, sizeof(unicast_request)) != 0)) {
                SpeedwireInverterProtocol inverter_packet(protocol);
                //LocalHost::hexdump(udp_packet, nbytes);
                //printf("%s\n", inverter_packet.toString().c_str());
                SpeedwireDevice device;
                device.susyID = inverter_packet.getSrcSusyID();
                device.serialNumber = inverter_packet.getSrcSerialNumber();
                device.deviceClass = "Inverter";
                device.deviceModel = "Inverter";
                device.peer_ip_address = peer_ip_address;
                device.interface_ip_address = localhost.getMatchingLocalIPAddress(peer_ip_address);
                if (device.interface_ip_address == "" && socket.isIpAny() == false) {
                    device.interface_ip_address = socket.getLocalInterfaceAddress();
                }
                // try to get further information about the device by examining the susy id; this is not accurate
                const SpeedwireDeviceType& device_type = SpeedwireDeviceType::fromSusyID(device.susyID);
                if (device_type.deviceClass != SpeedwireDeviceClass::UNKNOWN) {
                    device.deviceClass = toString(device_type.deviceClass);
                    device.deviceModel = device_type.name;
                }
                if (registerDevice(device)) {
                    printf("found susyid %u serial %lu ip %s\n", device.susyID, device.serialNumber, device.peer_ip_address.c_str());
                    result = true;
                }
#if 0
                // dump reply information; this is just the src susyid and serialnumber together with some unknown bits
                printf("%s\n", inverter_packet.toString().c_str());
                std::vector<SpeedwireRawData> raw_data = inverter_packet.getRawDataElements();
                for (auto& rd : raw_data) {
                    printf("%s\n", rd.toString().c_str());
                }
#endif
            }
            else if (!SpeedwireData2Packet::isInverterProtocolID(protocolID)) {
                printf("received unknown response packet 0x%04x\n", protocolID);
                perror("unexpected response");
            }
        }
    }
    return result;
}
