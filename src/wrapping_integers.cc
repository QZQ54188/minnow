#include "wrapping_integers.hh"
#include "debug.hh"

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  return Wrap32( static_cast<uint32_t>( n ) + zero_point.raw_value_ );
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  // up_bound = 2^32
  const uint64_t up_bound = static_cast<uint64_t>( UINT32_MAX ) + 1;
  // checkpoint_mod表示checkpoint以zeropoint为起始点在uint_32中的位置
  const uint32_t checkpoint_mod = Wrap32::wrap( checkpoint, zero_point ).raw_value_;
  uint32_t dis = this->raw_value_ - checkpoint_mod;
  if ( dis <= ( up_bound >> 1 ) || checkpoint + dis < up_bound ) {
    return checkpoint + dis;
  }
  return checkpoint + dis - up_bound;
}
