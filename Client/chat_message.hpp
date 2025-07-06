#pragma once

#include <string>

class chat_message
{
  public:
    enum
    {
        header_length = 4
    };
    enum
    {
        max_body_length = 512
    };

    chat_message() : body_length_(0)
    {
    }

    chat_message(std::string const& msg) : body_length_(msg.size())
    {
        if (body_length_ > max_body_length)
        {
            body_length_ = max_body_length;
        }
        std::memcpy(data_ + header_length, msg.data(), body_length_);
        encode_header();
    }

    const char* data() const
    {
        return data_;
    }
    char* data()
    {
        return data_;
    }

    std::size_t length() const
    {
        return header_length + body_length_;
    }

    const char* body() const
    {
        return data_ + header_length;
    }
    char* body()
    {
        return data_ + header_length;
    }

    std::size_t body_length() const
    {
        return body_length_;
    }

    void body_length(std::size_t new_length)
    {
        body_length_ = new_length;
        if (body_length_ > max_body_length)
        {
            body_length_ = max_body_length;
        }
    }

    bool decode_header()
    {
        body_length_ = (data_[0] << 24) | (data_[1] << 16) | (data_[2] << 8) | data_[3];
        if (body_length_ > max_body_length)
        {
            body_length_ = 0;
            return false;
        }
        return true;
    }

    void encode_header()
    {
        data_[0] = (body_length_ >> 24) & 0xFF;
        data_[1] = (body_length_ >> 16) & 0xFF;
        data_[2] = (body_length_ >> 8) & 0xFF;
        data_[3] = body_length_ & 0xFF;
    }

  private:
    char data_[header_length + max_body_length];
    std::size_t body_length_;
};