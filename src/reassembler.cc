#include "reassembler.hh"
#include "debug.hh"
#include <algorithm>

using namespace std;

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  // 首先获取当前字节流的写权限
  Writer& writer = output_.writer();
  // 计算当前重组器可接受的下标范围，分类讨论
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

  // 记录是否存在表示结束的子串
  if (is_last_substring) {
    has_last_substring_ = true;
  }

  // 如果接受到的下标不是期待下标，就缓存，如果是期待下标，就
  if ( first_index > expected_index_ ) {
    // 在这个逻辑分支中先判断缓存是为了更方便处理重复分组
    cache_bytes( first_index, std::move( data ) );
  } else {
    push_bytes( first_index, std::move( data ), is_last_substring );
  }
  // 刷新缓冲区，如果可以将重组器缓存区组装好的字节推到字节buffer中就推入
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
    has_last_substring_ = false; // 重置标志，因为已经处理过了
  }
}

void Reassembler::cache_bytes( uint64_t first_index, string data )
{
  // 找到buffer链表中与所给区间可能有关系的左右节点，左节点的右边界一定大于first_index
  auto left = lower_bound( buffer_.begin(), buffer_.end(), first_index, []( auto&& elem, uint64_t index ) {
    return index > ( elem.first + elem.second.length() );
  } );
  // 右节点的下标大于end_index
  auto right = upper_bound( left, buffer_.end(), first_index + data.length(), []( uint64_t index, auto&& elem ) {
    return index < elem.first;
  } );

  // 当left==buffer_.end()的时候，直接插入到left位置即可
  if ( const uint64_t end_index = first_index + data.length(); left != buffer_.end() ) {
    auto& [left_index, left_data] = *left;
    const uint64_t right_index = left_data.length() + left_index;
    // 得出基本数据之后开始计算左边区间和当前区间的关系
    if ( first_index >= left_index && right_index >= end_index ) {
      // 当前数据已经被包含在左边区间中，重复数据直接返回，无需缓存
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
    auto& [left_index, left_data] = *prev( right );
    if ( const uint64_t right_index = left_index + left_data.length(); right_index > end_index ) {
      data.resize( data.length() - ( end_index - left_index ) );
      data.append( left_data );
    }
  }

  for ( ; left != right; left = buffer_.erase( left ) ) {
    bytes_pending_ -= left->second.length();
  }
  bytes_pending_ += data.length();
  buffer_.insert( left, { first_index, std::move( data ) } );
}

void Reassembler::flush_buffer()
{
  while ( !buffer_.empty() ) {
    auto& [index, data] = buffer_.front();
    if ( index > expected_index_ ) {
      break;
    }
    bytes_pending_ -= data.length();
    
    // 如果这是最后一个缓存数据且已经标记接收到最后子串，则传递结束标志
    bool is_last = has_last_substring_ && buffer_.size() == 1;
    
    push_bytes( index, std::move( data ), is_last );
    
    if ( !buffer_.empty() ) {
      buffer_.pop_front();
    }
  }
  
  // 处理边缘情况：如果缓冲区为空但收到了最后子串标记，需要关闭流
  if (buffer_.empty() && has_last_substring_) {
    output_.writer().close();
    has_last_substring_ = false;
  }
}