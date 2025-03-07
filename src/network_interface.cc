#include <iostream>

#include "arp_message.hh"
#include "debug.hh"
#include "ethernet_frame.hh"
#include "exception.hh"
#include "helpers.hh"
#include "network_interface.hh"

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( string_view name,
                                    shared_ptr<OutputPort> port,
                                    const EthernetAddress& ethernet_address,
                                    const Address& ip_address )
  : name_( name )
  , port_( notnull( "OutputPort", move( port ) ) )
  , ethernet_address_( ethernet_address )
  , ip_address_( ip_address )
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address_ ) << " and IP address "
       << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but
//! may also be another host if directly connected to the same network as the destination) Note: the Address type
//! can be converted to a uint32_t (raw 32-bit IP address) by using the Address::ipv4_numeric() method.
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  // 在ip数据报中，我们发送的都是uint32位的ip地址
  uint32_t target_ip = next_hop.ipv4_numeric();
  auto iter = addr_mapping_.find( target_ip );
  if ( iter == addr_mapping_.end() ) {
    // 之前的映射表中没有这个地址，那么发送arp广播请求获取这个mac地址
    // auto[first, end] = bufferd_ip_data_.equal_range(target_ip);
    // bool exist = false;
    // for(auto it = first; it != end; it++){
    //   if(it->second.header.dst == dgram.header.dst){
    //     exist = true;
    //     break;
    //   }
    // }
    // if(!exist)
    bufferd_ip_data_.emplace( target_ip, dgram );
    if ( arp_request_buffer_.find( target_ip ) == arp_request_buffer_.end() ) {
      transmit( make_ethernet_frame( EthernetHeader::TYPE_ARP,
                                     serialize( make_arp_message( ARPMessage::OPCODE_REQUEST, target_ip ) ) ) );
      arp_request_buffer_.emplace( target_ip, 0 );
    }
  } else {
    EthernetAddress ether_addr = iter->second.get_ether();
    transmit( make_ethernet_frame( EthernetHeader::TYPE_IPv4, serialize( dgram ), ether_addr ) );
  }
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame( EthernetFrame frame )
{
  // 如果当前帧的地址不是广播地址而且目的mac不是接收接口的mac，不做处理
  if ( frame.header.dst != ETHERNET_BROADCAST && frame.header.dst != ethernet_address_ ) {
    return;
  }

  if ( frame.header.type == EthernetHeader::TYPE_IPv4 ) {
    InternetDatagram ip_data;
    if ( !parse( ip_data, frame.payload ) ) {
      return;
    }
    datagrams_received_.emplace( ip_data );
  } else if ( frame.header.type == EthernetHeader::TYPE_ARP ) {
    ARPMessage arp_msg;
    if ( !parse( arp_msg, frame.payload ) ) {
      return;
    }
    auto send_bufferd_data = [&arp_msg, this]() ->void{
      // 遍历队列发出旧数据帧
        auto [head, tail] = bufferd_ip_data_.equal_range( arp_msg.sender_ip_address );
        // size_t cnt = 0;
        for_each( head, tail, [this, &arp_msg]( auto&& iter ) -> void {
          // std::cout<<++cnt<<std::endl;
          transmit( make_ethernet_frame(
            EthernetHeader::TYPE_IPv4, serialize( iter.second ), arp_msg.sender_ethernet_address ) );
        } );
        if ( head != tail )
          bufferd_ip_data_.erase( head, tail );
    };
    
    addr_mapping_.insert_or_assign( arp_msg.sender_ip_address, address_mapping( arp_msg.sender_ethernet_address ) );
    if ( arp_msg.opcode == ARPMessage::OPCODE_REQUEST ) {
      if ( arp_msg.target_ip_address == ip_address_.ipv4_numeric() ) {
        transmit( make_ethernet_frame( EthernetHeader::TYPE_ARP,
                                       serialize( make_arp_message( ARPMessage::OPCODE_REPLY,
                                                                    arp_msg.sender_ip_address,
                                                                    arp_msg.sender_ethernet_address ) ),
                                       arp_msg.sender_ethernet_address ) );
      }
      if(bufferd_ip_data_.contains(arp_msg.sender_ip_address)){
        send_bufferd_data();
      }
    } else if ( arp_msg.opcode == ARPMessage::OPCODE_REPLY ) {
      send_bufferd_data();
    }
  }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  constexpr size_t ms_mapping_ttl = 30'000, ms_arp_resend = 5'000;
  auto flush_timer = [&ms_since_last_tick, this]( auto& table, size_t deadline ) -> void {
    for ( auto iter = table.begin(); iter != table.end(); ) {
      if ( ( iter->second += ms_since_last_tick ) > deadline ) {
        if(deadline == 5'000){
          // 如果这个arp请求超时了，那么丢弃之前放在待处理的缓存中的表项
          bufferd_ip_data_.erase(iter->first);
        }
        iter = table.erase( iter );
      } else {
        iter++;
      }
    }
  };
  flush_timer( addr_mapping_, ms_mapping_ttl );
  flush_timer( arp_request_buffer_, ms_arp_resend );
}

EthernetFrame NetworkInterface::make_ethernet_frame( const uint16_t& type,
                                                     std::vector<Ref<std::string>> payload,
                                                     std::optional<EthernetAddress> dst ) const
{
  return { .header { .dst = dst.has_value() ? std::move( *dst ) : ETHERNET_BROADCAST,
                     .src = ethernet_address_,
                     .type = type },
           .payload = std::move( payload ) };
}

ARPMessage NetworkInterface::make_arp_message( const uint16_t& type,
                                               const uint32_t& target_ip,
                                               std::optional<EthernetAddress> target_mac ) const
{
  return { .opcode = type,
           .sender_ethernet_address = ethernet_address_,
           .sender_ip_address = ip_address_.ipv4_numeric(),
           .target_ethernet_address = target_mac.has_value() ? std::move( *target_mac ) : EthernetAddress {},
           .target_ip_address = target_ip };
}

NetworkInterface::address_mapping& NetworkInterface::address_mapping::tick( const size_t ms_time_passed ) noexcept
{
  timer_ += ms_time_passed;
  return *this;
}