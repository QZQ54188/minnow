#pragma once

#include "exception.hh"
#include "network_interface.hh"

#include <map>
#include <memory>
#include <optional>
#include <compare>

// \brief A router that has multiple network interfaces and
// performs longest-prefix-match routing between them.
class Router
{
public:
  // Add an interface to the router
  // \param[in] interface an already-constructed network interface
  // \returns The index of the interface after it has been added to the router
  size_t add_interface( std::shared_ptr<NetworkInterface> interface )
  {
    interfaces_.push_back( notnull( "add_interface", std::move( interface ) ) );
    return interfaces_.size() - 1;
  }

  // Access an interface by index
  std::shared_ptr<NetworkInterface> interface( const size_t N ) { return interfaces_.at( N ); }

  // Add a route (a forwarding rule)
  void add_route( uint32_t route_prefix,
                  uint8_t prefix_length,
                  std::optional<Address> next_hop,
                  size_t interface_num );

  // Route packets between the interfaces
  void route();

private:
  struct subnet_mask{
    uint32_t mask_ {};    //子网掩码
    uint32_t net_ {};     //网络号
    explicit subnet_mask(const uint8_t prefix_length, const uint32_t route_prefix) 
    : mask_(~(UINT32_MAX >> prefix_length)), net_(route_prefix & mask_){};
    auto operator<=>(const subnet_mask& other){
      return mask_ != other.mask_ ?
      mask_ <=> other.mask_ : net_ <=> other.net_;
    }
  };

  // 路由表，key为子网掩码，value为接口号和下一跳地址组成的二元组
  using routerT = std::multimap<subnet_mask, std::pair<size_t, std::optional<Address>>, std::greater<subnet_mask>>;
  routerT router_map_ {};

  routerT::const_iterator match_max_prefix(const uint32_t target_ip) const;

  // The router's collection of network interfaces
  std::vector<std::shared_ptr<NetworkInterface>> interfaces_ {};
};
