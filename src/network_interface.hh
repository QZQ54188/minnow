#pragma once

#include "address.hh"
#include "ethernet_frame.hh"
#include "ipv4_datagram.hh"
#include "arp_message.hh"

#include <algorithm>
#include <memory>
#include <optional>
#include <queue>
#include <unordered_map>
#include <vector>
#include <map>

// A "network interface" that connects IP (the internet layer, or network layer)
// with Ethernet (the network access layer, or link layer).

// This module is the lowest layer of a TCP/IP stack
// (connecting IP with the lower-layer network protocol,
// e.g. Ethernet). But the same module is also used repeatedly
// as part of a router: a router generally has many network
// interfaces, and the router's job is to route Internet datagrams
// between the different interfaces.

// The network interface translates datagrams (coming from the
// "customer," e.g. a TCP/IP stack or router) into Ethernet
// frames. To fill in the Ethernet destination address, it looks up
// the Ethernet address of the next IP hop of each datagram, making
// requests with the [Address Resolution Protocol](\ref rfc::rfc826).
// In the opposite direction, the network interface accepts Ethernet
// frames, checks if they are intended for it, and if so, processes
// the the payload depending on its type. If it's an IPv4 datagram,
// the network interface passes it up the stack. If it's an ARP
// request or reply, the network interface processes the frame
// and learns or replies as necessary.
class NetworkInterface
{
public:
  // An abstraction for the physical output port where the NetworkInterface sends Ethernet frames
  class OutputPort
  {
  public:
    virtual void transmit( const NetworkInterface& sender, const EthernetFrame& frame ) = 0;
    virtual ~OutputPort() = default;
  };

  // Construct a network interface with given Ethernet (network-access-layer) and IP (internet-layer)
  // addresses
  NetworkInterface( std::string_view name,
                    std::shared_ptr<OutputPort> port,
                    const EthernetAddress& ethernet_address,
                    const Address& ip_address );

  // Sends an Internet datagram, encapsulated in an Ethernet frame (if it knows the Ethernet destination
  // address). Will need to use [ARP](\ref rfc::rfc826) to look up the Ethernet destination address for the next
  // hop. Sending is accomplished by calling `transmit()` (a member variable) on the frame.
  void send_datagram( const InternetDatagram& dgram, const Address& next_hop );

  // Receives an Ethernet frame and responds appropriately.
  // If type is IPv4, pushes the datagram to the datagrams_in queue.
  // If type is ARP request, learn a mapping from the "sender" fields, and send an ARP reply.
  // If type is ARP reply, learn a mapping from the "sender" fields.
  void recv_frame( EthernetFrame frame );

  // Called periodically when time elapses
  void tick( size_t ms_since_last_tick );

  // Accessors
  const std::string& name() const { return name_; }
  const OutputPort& output() const { return *port_; }
  OutputPort& output() { return *port_; }
  std::queue<InternetDatagram>& datagrams_received() { return datagrams_received_; }

private:
  // 由网络层数据生成链路层帧
  EthernetFrame make_ethernet_frame( const uint16_t& type,
                                     std::vector<Ref<std::string>> payload,
                                     std::optional<EthernetAddress> dst = std::nullopt ) const;

  // arp请求或者响应报文，仅在响应报文时使用target_mac
  ARPMessage make_arp_message( const uint16_t& type,
                               const uint32_t& target_ip,
                               std::optional<EthernetAddress> target_mac = std::nullopt ) const;

  class address_mapping
  {
    EthernetAddress ether_addr_ {};
    size_t timer_ {};

  public:
    explicit address_mapping( EthernetAddress ether_addr ) : ether_addr_ { std::move( ether_addr ) }, timer_ {} {};
    EthernetAddress get_ether() const noexcept { return ether_addr_; };
    address_mapping& tick( const size_t ms_time_passed ) noexcept;
    address_mapping& operator+=( const size_t ms_time_passed ) noexcept { return tick( ms_time_passed ); };
    auto operator<=>( const size_t deadline ) const { return timer_ <=> deadline; };
  };
  // Human-readable name of the interface
  std::string name_;

  // The physical output port (+ a helper function `transmit` that uses it to send an Ethernet frame)
  std::shared_ptr<OutputPort> port_;
  void transmit( const EthernetFrame& frame ) const { port_->transmit( *this, frame ); }

  // Ethernet (known as hardware, network-access-layer, or link-layer) address of the interface
  EthernetAddress ethernet_address_;

  // IP (known as internet-layer or network-layer) address of the interface
  Address ip_address_;

  // Datagrams that have been received
  std::queue<InternetDatagram> datagrams_received_ {};

  // 为了在数据链路层传输，需要一个表单来存储ip地址到mac地址的映射，
  std::unordered_map<uint32_t, address_mapping> addr_mapping_ {};

  // 发送arp请求的数据报在获取到目标mac地址之前不可发送，将其缓存起来，key为目标ip地址
  // 因为可能一份ip对应多个数据报，所以使用multimap
  std::multimap<uint32_t, InternetDatagram> bufferd_ip_data_ {};

  // 将5s(lab5要求)内发送过arp请求的ip数据报加入表单中防止arp泛洪，value表示计时器
  std::unordered_map<uint32_t, size_t> arp_request_buffer_ {};
};
