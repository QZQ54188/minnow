#include "reassembler.hh"
#include "debug.hh"
#include <algorithm>

using namespace std;

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  Writer& writer = output_.writer();
  const uint64_t unacceptable_index = expected_index_ + writer.available_capacity();
  if ( first_index >= unacceptable_index || writer.is_closed() || writer.available_capacity() == 0 ) {
    // 当前下标超出范围，写道被关闭或者剩余容量为0直接返回
    return;
  } else if ( first_index + data.length() > unacceptable_index ) {
    // 当前数据位置超出capacity，重新调整数据大小
    data.resize( unacceptable_index - first_index );
    // 数据被截断之后不视为最后一个分组（测试数据而来）
    is_last_substring = false;
  }

  if ( first_index > expected_index_ ) {
    cache_bytes( first_index, std::move( data ), is_last_substring );
  } else {
    push_bytes( first_index, std::move( data ), is_last_substring );
  }
  flush_buffer();
}

// How many bytes are stored in the Reassembler itself?
// This function is for testing only; don't add extra state to support it.
uint64_t Reassembler::count_bytes_pending() const
{
  return bytes_pending_;
}

void Reassembler::push_bytes( uint64_t first_index, string data, bool is_last_substring )
{
  // 根据first_index的取值讨论，可能有重复分组，也可能是正好期待的下一个分组
  if ( first_index < expected_index_ ) {
    // 有部分重复分组
    data.erase( 0, expected_index_ - first_index );
  }

  expected_index_ += data.length();
  output_.writer().push( std::move( data ) );

  // 如果是最后一个string的话就关闭buffer，不再支持写入
  if ( is_last_substring ) {
    output_.writer().close();
    buffer_.clear();
    bytes_pending_ = 0;
  }
}

void Reassembler::cache_bytes( uint64_t first_index, string data, bool is_last_substring )
{
  // 找到buffer链表中与所给区间可能有关系的左右节点，左节点的右边界一定大于first_index
  auto left = lower_bound( buffer_.begin(), buffer_.end(), first_index, []( auto&& elem, uint64_t index ) {
    return index > ( get<0>( elem ) + get<1>( elem ).length() );
  } );
  // 右节点的下标大于end_index
  auto right = upper_bound( left, buffer_.end(), first_index + data.length(), []( uint64_t index, auto&& elem ) {
    return index < get<0>( elem );
  } );

  // 当left==buffer_.end()的时候，直接插入到left位置即可
  if ( const uint64_t end_index = first_index + data.length(); left != buffer_.end() ) {
    auto& [left_index, left_data, last] = *left;
    const uint64_t right_index = left_data.length() + left_index;
    // 得出基本数据之后开始计算左边区间和当前区间的关系
    if ( first_index >= left_index && right_index >= end_index ) {
      // 当前数据已经被包含在左边区间中
      return;
    } else if ( left_index > end_index ) {
      // 两个区间没有重叠的地方
      right = left;
    } else if ( !( left_index >= first_index && right_index <= end_index ) ) {
      // 部分重叠
      if ( first_index >= left_index ) {
        data.insert( 0, string_view( left_data.c_str(), left_data.length() - ( right_index - first_index ) ) );
      } else {
        data.resize( data.length() - ( end_index - left_index ) );
        data.append( left_data );
      }
      first_index = min( first_index, left_index );
    }
  }

  if ( const uint64_t end_index = first_index + data.length(); right != left && !buffer_.empty() ) {
    // right的prev才可能和当前数据有关系
    auto& [left_index, left_data, last] = *prev( right );
    if ( const uint64_t right_index = left_index + left_data.length(); right_index > end_index ) {
      data.resize( data.length() - ( end_index - left_index ) );
      data.append( left_data );
    }
  }

  for ( ; left != right; left = buffer_.erase( left ) ) {
    bytes_pending_ -= get<1>( *left ).length();
    is_last_substring |= get<2>( *left );
  }
  bytes_pending_ += data.length();
  buffer_.insert( left, { first_index, std::move( data ), is_last_substring } );
}

void Reassembler::flush_buffer()
{
  while ( !buffer_.empty() ) {
    auto& [index, data, last] = buffer_.front();
    if ( index > expected_index_ ) {
      break;
    }
    bytes_pending_ -= data.length();
    push_bytes( index, std::move( data ), last );
    if ( !buffer_.empty() ) {
      buffer_.pop_front();
    }
  }
}