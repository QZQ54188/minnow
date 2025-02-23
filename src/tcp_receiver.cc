#include "tcp_receiver.hh"
#include "debug.hh"

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  if ( message.RST ) {
    // 如果发送方RST为ture，为接收方的读端口设置错误
    reassembler_.reader().set_error();
    return;
  } else if ( message.SYN ) {
    // 接受到SYN之后正式开始字节的传输，设置seq_为ISN
    SYN_ = true;
    seq_ = message.seqno;
  } else if ( message.seqno == seq_ ) {
    // 如果收到的序列号与ISN相同，由于SYN需要占一个字节，所以无法在当前序列号插入数据
    return;
  }
  // checkpoint表示已经传输到重组器中的字节总数
  const uint64_t checkpoint = reassembler_.writer().bytes_pushed() + SYN_;
  uint64_t absolute_seqnum = message.seqno.unwrap( seq_, checkpoint );
  // 由于字节流序号中没有ISN占位，所以计算出绝对序列号之后还需要进行处理
  absolute_seqnum = absolute_seqnum == 0 ? absolute_seqnum : absolute_seqnum - 1;
  reassembler_.insert( absolute_seqnum, std::move( message.payload ), message.FIN );
  // printf("====%ld\n", reassembler_.writer().bytes_pushed());
}

TCPReceiverMessage TCPReceiver::send() const
{
  // 剩余的容量为重组器writer的剩余空间
  uint64_t capacity = reassembler_.writer().available_capacity();
  // 要返回出去告诉发送方的窗口大小
  uint16_t window_size = capacity > UINT16_MAX ? UINT16_MAX : capacity;
  uint64_t expected_index = reassembler_.writer().bytes_pushed() + SYN_;
  // printf("====%ld\n", expected_index);
  if ( !SYN_ ) {
    return { {}, window_size, reassembler_.writer().has_error() };
  }
  // 如果FIN到达接收方，接收方的重组器关闭，而且FIN还需要一个占位符(可以使用reassembler_.writer().is_closed()表示)
  return { Wrap32::wrap( expected_index + reassembler_.writer().is_closed(), seq_ ),
           window_size,
           reassembler_.writer().has_error() };
}
