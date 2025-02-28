#include "router.hh"
#include "debug.hh"

#include <iostream>
#include <ranges>

using namespace std;

// route_prefix: The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
// prefix_length: For this route to be applicable, how many high-order (most-significant) bits of
//    the route_prefix will need to match the corresponding bits of the datagram's destination address?
// next_hop: The IP address of the next hop. Will be empty if the network is directly attached to the router (in
//    which case, the next hop address should be the datagram's final destination).
// interface_num: The index of the interface to send the datagram out on.
void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  cerr << "DEBUG: adding route " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
       << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(direct)" )
       << " on interface " << interface_num << "\n";

  router_map_.emplace(subnet_mask(prefix_length, route_prefix), std::make_pair(interface_num, next_hop));
}

// Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
void Router::route()
{
  ranges::for_each(interfaces_, [this](auto& cur_interface) -> void{
    // received_data表示该网络接口接收到的ip数据报队列
    auto& received_data = cur_interface->datagrams_received();
    while(!received_data.empty()){
      auto&& dgram = received_data.front();
      const auto& item = match_max_prefix(dgram.header.dst);
      if(item == router_map_.cend() || dgram.header.ttl <= 1){
        received_data.pop();
        continue;
      }
      --dgram.header.ttl;
      dgram.header.compute_checksum();
      const auto& [interface_num, next_hop] = item->second;

      interface(interface_num) ->send_datagram(
        std::move(dgram),
        next_hop.has_value() ? *next_hop : Address::from_ipv4_numeric(dgram.header.dst)
      );
      received_data.pop();
    }
  });
}

Router::routerT::const_iterator Router::match_max_prefix(const uint32_t target_ip) const{
  return ranges::find_if(router_map_, [&target_ip](auto& iter) -> bool{
    return (target_ip & iter.first.mask_) == iter.first.net_;
  });
}