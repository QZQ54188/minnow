#include "tcp_sender.hh"
#include "debug.hh"
#include "tcp_config.hh"

using namespace std;

ReTreansmitTimer& ReTreansmitTimer::timeout()
{
  RTO_ <<= 1;
  return *this;
}

ReTreansmitTimer& ReTreansmitTimer::open()
{
  is_open_ = true;
  return *this;
}

ReTreansmitTimer& ReTreansmitTimer::reset()
{
  allTime_passed_ = 0;
  return *this;
}

ReTreansmitTimer& ReTreansmitTimer::tick( uint64_t ms_since_last_tick )
{
  allTime_passed_ += is_open_ ? ms_since_last_tick : 0;
  return *this;
}

// This function is for testing only; don't add extra state to support it.
uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return seq_num_in_flight_;
}

// This function is for testing only; don't add extra state to support it.
uint64_t TCPSender::consecutive_retransmissions() const
{
  return consecutive_retransmissions_;
}

void TCPSender::push( const TransmitFunction& transmit )
{
  Reader& read_bytes = input_.reader();
  FIN_ = read_bytes.is_finished();
  if ( send_FIN_ ) {
    return;
  }

  // 只要有数据可以发送就发送，发送过FIN之后无法再发送数据
  while ( seq_num_in_flight_ < window_size_ && !send_FIN_ ) {
    string_view byte_to_trans = read_bytes.peek();
    // cout<<byte_to_trans<<endl;
    if ( ( SYN_ && byte_to_trans.empty() && !FIN_ ) || send_FIN_ ) {
      // 传输空字符串直接break，传输过了FIN_也直接break
      break;
    }

    // 发送字符串，payload表示目前还可以发送的字符串
    std::string payload {};
    while ( payload.length() + seq_num_in_flight_ + !send_SYN_ < window_size_
            && payload.length() < TCPConfig::MAX_PAYLOAD_SIZE ) {
      // 组装payload
      if (byte_to_trans.empty() || FIN_) {
        // 没有数据需要发送了或者需要发送FIN_
        // cout<<"====" << byte_to_trans<<"===="<<endl;
        break;
      }
      // 如果当前读取的数据长度大于window_size_，那么对数据进行截取
      const uint64_t available_size
        = min( TCPConfig::MAX_PAYLOAD_SIZE - payload.length(),
               window_size_ - payload.length() - seq_num_in_flight_ - FIN_ - !send_SYN_ );
      if ( byte_to_trans.length() > available_size ) {
        // cout<<"====" << byte_to_trans.length() <<"===="<<endl;
        // cout<<payload.length()<<endl;
        byte_to_trans.remove_suffix( byte_to_trans.length() - available_size );
      }

      payload.append( byte_to_trans );
      read_bytes.pop( byte_to_trans.length() );
      FIN_ |= read_bytes.is_finished();
      byte_to_trans = read_bytes.peek();
    }

    if ( !send_FIN_ ) {
      size_t len = payload.length();
      auto& msg = outstanding_segment_.emplace( make_message( seq_num_, !send_SYN_, std::move( payload ), FIN_ ) );
      SYN_ = true;
      // 当窗口足够时，可以同时将数据和FIN_发送出去
      if ( FIN_ && len < window_size_ ) {
        send_FIN_ = true;
        len += 1;
      } else {
        msg.FIN = false;
      }
      seq_num_in_flight_ += len + !send_SYN_;
      seq_num_ += len + !send_SYN_;
      send_SYN_ = true;
      transmit( msg );
      timer_.open();
    } else {
      // 如果已经发送过了FIN的话，不可以发送任何其他数据，直接break
      break;
    }
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  return make_message( seq_num_, false, {}, false );
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  window_size_ = msg.window_size == 0 ? 1 : msg.window_size;
  zero_window_ = msg.window_size == 0;
  if ( !msg.ackno.has_value()) {
    if ( msg.RST ) {
      input_.set_error();
    }
    return;
  }

  // 表示接收方期待收到的下一个序号位置的数据
  const uint64_t expected_seq = msg.ackno->unwrap( isn_, seq_num_ );
  // 当前消息还没有被发送或者当前消息已经被确认的话直接返回
  if ( expected_seq > seq_num_ || acked_seq_ > expected_seq ) {
    return;
  }

  bool is_acked = false;
  while ( !outstanding_segment_.empty() ) {
    // 表示已发送但是未被确认的最早的消息
    auto& first_msg = outstanding_segment_.front();
    // printf("======%s====\n", first_msg.payload.c_str());
    const uint64_t end_seq = acked_seq_ + first_msg.sequence_length() - 1;
    if ( end_seq > expected_seq ) {
      break;
    }
    is_acked = true;
    seq_num_in_flight_ -= first_msg.sequence_length();
    acked_seq_ += first_msg.sequence_length();
    outstanding_segment_.pop();
  }

  if ( is_acked ) {
    if ( outstanding_segment_.empty() ) {
      // 所有的分组全部被确认
      timer_ = ReTreansmitTimer( initial_RTO_ms_ );
    } else {
      // 重启计时器
      timer_ = std::move( ReTreansmitTimer( initial_RTO_ms_ ).open() );
    }
    // 重置计时器，将连续传输的分组设置为0
    consecutive_retransmissions_ = 0;
  }
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  if ( timer_.tick( ms_since_last_tick ).is_expired() ) {
    // 重传队首的数据
    transmit( outstanding_segment_.front() );
    if ( zero_window_ ) {
      timer_.reset();
    } else {
      timer_.timeout().reset();
    }
    consecutive_retransmissions_++;
  }
}

TCPSenderMessage TCPSender::make_message( uint64_t seq, bool syn, std::string payload, bool fin ) const
{
  return { .seqno = Wrap32::wrap( seq, isn_ ),
           .SYN = syn,
           .payload = std::move( payload ),
           .FIN = fin,
           .RST = input_.reader().has_error() };
}
