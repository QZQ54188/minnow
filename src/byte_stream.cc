#include "byte_stream.hh"

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ) {}

void Writer::push( string data )
{
  if(close_ || error_)
  {
    return;
  }
  size_t len = data.length();
  len = std::min(len, capacity_ - buffer_.size());
  write_count += len;
  buffer_.append(data.substr(0, len));
}

void Writer::close()
{
  // Your code here.
  close_ = true;
}

bool Writer::is_closed() const
{
  return close_;
}

uint64_t Writer::available_capacity() const
{
  return capacity_ - buffer_.size();
}

uint64_t Writer::bytes_pushed() const
{
  return write_count;
}

string_view Reader::peek() const
{
  return buffer_;
}

void Reader::pop( uint64_t len )
{
  // (void)len; // Your code here.
  if(buffer_.size() != 0)
  {
    len = std::min(len, buffer_.length());
    buffer_.erase(0, len);
    read_count += len;
  }
}

bool Reader::is_finished() const
{
  return close_ && buffer_.size() == 0;
}

uint64_t Reader::bytes_buffered() const
{
  return buffer_.size();
}

uint64_t Reader::bytes_popped() const
{
  return read_count;
}

