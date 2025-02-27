#pragma once

#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

#include <functional>
#include <iostream>
#include <queue>
#include <string_view>

class ReTreansmitTimer
{
public:
  explicit ReTreansmitTimer( uint64_t init_rto_time ) : RTO_( init_rto_time ) {};
  bool is_expired() const { return is_open_ && allTime_passed_ >= RTO_; }
  bool is_open() const { return is_open_; }
  // 激活一个分组的计时器，返回引用可以支持链式调用
  ReTreansmitTimer& open();
  // 将超时重传时间变为两倍
  ReTreansmitTimer& timeout();
  // 重置当前计时器
  ReTreansmitTimer& reset();
  // 当前计时器经过了多少时间
  ReTreansmitTimer& tick( uint64_t ms_since_last_tick );

private:
  uint64_t RTO_ {};            // 超时重传时间
  uint64_t allTime_passed_ {}; // 计时器启动之后所经过的总时间
  bool is_open_ { false };     // 定时器是否打开
};

class TCPSender
{
public:
  /* Construct TCP sender with given default Retransmission Timeout and possible ISN */
  TCPSender( ByteStream&& input, Wrap32 isn, uint64_t initial_RTO_ms )
    : input_( std::move( input ) ), isn_( isn ), initial_RTO_ms_( initial_RTO_ms )
  {}

  /* Generate an empty TCPSenderMessage */
  TCPSenderMessage make_empty_message() const;

  /* Receive and process a TCPReceiverMessage from the peer's receiver */
  void receive( const TCPReceiverMessage& msg );

  /* Type of the `transmit` function that the push and tick methods can use to send messages */
  using TransmitFunction = std::function<void( const TCPSenderMessage& )>;

  /* Push bytes from the outbound stream */
  void push( const TransmitFunction& transmit );

  /* Time has passed by the given # of milliseconds since the last time the tick() method was called */
  void tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit );

  // Accessors
  uint64_t sequence_numbers_in_flight() const;  // For testing: how many sequence numbers are outstanding?
  uint64_t consecutive_retransmissions() const; // For testing: how many consecutive retransmissions have happened?
  const Writer& writer() const { return input_.writer(); }
  const Reader& reader() const { return input_.reader(); }
  Writer& writer() { return input_.writer(); }

private:
  Reader& reader() { return input_.reader(); }

  TCPSenderMessage make_message( uint64_t seq, bool syn, std::string payload, bool fin ) const;

  ByteStream input_;
  Wrap32 isn_;
  uint64_t initial_RTO_ms_;

  uint16_t window_size_ { 1 };
  bool zero_window_ {}; // 表示发送方拥塞窗口大小是不是为0，如果为0的话就不加倍超时重传时间
  uint64_t acked_seq_ { 1 };                // 表示发送方收到接收方想要的下一个数据的序列号
  uint64_t seq_num_in_flight_ {};           // 表示已经发送但是未被确认的数据的数目
  uint64_t consecutive_retransmissions_ {}; // 表示超时重发的次数
  uint64_t seq_num_ {}; // 表示发送方将要发送的下一个字符，也可使用queue.front()
  // 用于存储发送数据报的队列，利用先进先出的特性
  std::queue<TCPSenderMessage> outstanding_segment_ {};

  ReTreansmitTimer timer_ { initial_RTO_ms_ };
  /*设置四个标志位，前两个是表示在连接过程中已经确定的状态位，后两个表示是否发送过SYN_和FIN_
  用与应对特殊情况*/
  bool SYN_ { false }, FIN_ { false }, send_FIN_ { false }, send_SYN_ { false };
};
