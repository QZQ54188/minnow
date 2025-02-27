#include "byte_stream.hh"

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ) {}

bool Writer::is_closed() const
{
  return is_closed_;
}

void Writer::push( string data )
{
  // 如果当前的字节流已经被关闭，那么不接受任何字节进入
  if ( is_closed() )
    return;
  // 数据大小大于可用容量，对数据进行截断处理
  if ( data.size() > available_capacity() )
    data.resize( available_capacity() );
  if ( !data.empty() ) {
    // 没事不要塞空字节字符串进去
    num_bytes_pushed_ += data.size();
    num_bytes_buffered_ += data.size();
    bytes_.emplace( move( data ) );
  }
  // 确定当前首部string视图
  if ( view_wnd_.empty() && !bytes_.empty() )
    view_wnd_ = bytes_.front();
}

void Writer::close()
{
  if ( !is_closed_ ) {
    is_closed_ = true;
    // 防止重复关闭，然后不断塞入 EOF
    bytes_.emplace( string( 1, EOF ) );
  }
}

uint64_t Writer::available_capacity() const
{
  return capacity_ - num_bytes_buffered_;
}

uint64_t Writer::bytes_pushed() const
{
  return num_bytes_pushed_;
}

bool Reader::is_finished() const
{
  // 当且仅当写者关闭、存在队列中未 pop 的字节数为 0
  return is_closed_ && bytes_buffered() == 0;
}

uint64_t Reader::bytes_popped() const
{
  return num_bytes_popped_;
}

string_view Reader::peek() const
{
  return view_wnd_;
}

void Reader::pop( uint64_t len )
{
  // 对弹出字节数目进行判断，如果大于view_wnd_就不断从队列中循环弹出view_wnd_
  auto remainder = len;
  while ( remainder >= view_wnd_.size() && remainder != 0 ) {
    // 不断清掉能从队列中 pop 出去的字节
    remainder -= view_wnd_.size();
    bytes_.pop();
    view_wnd_ = bytes_.empty() ? ""sv : bytes_.front();
  }
  if ( !view_wnd_.empty() )
    view_wnd_.remove_prefix( remainder );

  num_bytes_buffered_ -= len;
  num_bytes_popped_ += len;
}

uint64_t Reader::bytes_buffered() const
{
  return num_bytes_buffered_;
}
