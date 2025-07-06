#include <deque>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>

using boost::asio::ip::tcp;

class chat_participant
{
  public:
    virtual ~chat_participant() = default;
    virtual void deliver(const std::string& msg) = 0;
    virtual std::string const& nickname() const = 0;
};

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
        // 初始化 header
        std::fill(data_, data_ + header_length, 0);
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

using chat_participant_ptr = std::shared_ptr<chat_participant>;
using chat_message_queue = std::deque<chat_message>;

class chat_room
{
  public:
    void set_room_name(std::string const& name)
    {
        room_name_ = name;
    }

    std::string const& room_name() const
    {
        return room_name_;
    }

    void join(chat_participant_ptr participant)
    {
        // 添加新成员到聊天室
        participants_.insert(participant);
        // 向新成员发送最近的消息
        for (const auto& msg : recent_msgs_)
        {
            participant->deliver(msg);
        }
        if (!recent_msgs_.empty())
        {
            participant->deliver("----------以上是历史聊天记录----------");
        }
        // 向其他成员发送新成员加入的消息
        std::stringstream ss;
        ss << participant->nickname() << "加入了聊天室——";
        system_prompt(ss.str(), participant);
    }

    void leave(chat_participant_ptr participant)
    {
        participants_.erase(participant);
    }

    void deliver(const std::string& msg, chat_participant_ptr sender)
    {
        std::stringstream ss;
        ss << sender->nickname() << " says: " << msg;
        recent_msgs_.push_back(ss.str());

        while (recent_msgs_.size() > max_recent_msgs)
        {
            recent_msgs_.pop_front();
        }

        for (const auto& participant : participants_)
        {
            if (sender != participant)
            {
                participant->deliver(ss.str());
            }
        }
    }

    void system_prompt(const std::string& msg, chat_participant_ptr blocked_user)
    {
        std::stringstream ss;
        ss << "system prompt: " << msg;
        for (const auto& participant : participants_)
        {
            if (blocked_user != participant)
            {
                participant->deliver(ss.str());
            }
        }
    }

  private:
    std::string room_name_;
    std::set<chat_participant_ptr> participants_;
    enum
    {
        max_recent_msgs = 100
    };
    std::deque<std::string> recent_msgs_;
};

class chat_session : public chat_participant, public std::enable_shared_from_this<chat_session>
{
  public:
    enum auth_state
    {
        NOT_AUTHED,
        AUTHED,
        FAILED
    }; // 验证状态

  public:
    chat_session(tcp::socket socket, chat_room& room) : socket_(std::move(socket)), room_(room), state_(NOT_AUTHED)
    {
    }

    void start()
    {
        std::stringstream ss;
        ss << "欢迎来到聊天室[" << room_.room_name() << "]，请输入你的用户名：";
        deliver(ss.str());
        do_read_header();
    }

    void deliver(const std::string& msg) override
    {
        bool write_in_progress = !write_msgs_.empty();
        write_msgs_.push_back(msg);
        if (!write_in_progress)
        {
            do_write();
        }
    }

    std::string const& nickname() const override
    {
        return nickname_;
    }

  private:
    void do_read_header()
    {
        auto self(shared_from_this());
        boost::asio::async_read(socket_, boost::asio::buffer(read_msg_.data(), chat_message::header_length),
                                [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                                    if (!ec && read_msg_.decode_header())
                                    {
                                        do_read_body();
                                    }
                                    else
                                    {
                                        room_.leave(shared_from_this());
                                    }
                                });
    }

    void do_read_body()
    {
        auto self(shared_from_this());
        boost::asio::async_read(socket_, boost::asio::buffer(read_msg_.body(), read_msg_.body_length()),
                                [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                                    if (!ec)
                                    {
                                        if (NOT_AUTHED == state_)
                                        { // 首次建立连接时，先验证用户身份
                                            nickname_ = std::string(read_msg_.body(), read_msg_.body_length());
                                            // 这里可以添加验证逻辑 如验证用户名是否已存在等
                                            // if(...)
                                            state_ = AUTHED;
                                            deliver("----------通过验证，开始聊天----------\n\n");
                                            room_.join(shared_from_this());
                                        }
                                        else if (AUTHED == state_)
                                        {
                                            room_.deliver(std::string(read_msg_.body(), read_msg_.body_length()),
                                                          shared_from_this());
                                        }

                                        do_read_header();
                                    }
                                    else
                                    {
                                        room_.leave(shared_from_this());
                                    }
                                });
    }

    void do_write()
    {
        auto self(shared_from_this());
        boost::asio::async_write(socket_, boost::asio::buffer(write_msgs_.front().data(), write_msgs_.front().length()),
                                 [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                                     if (!ec)
                                     {
                                         write_msgs_.pop_front();
                                         if (!write_msgs_.empty())
                                         {
                                             do_write();
                                         }
                                     }
                                     else
                                     {
                                         room_.leave(shared_from_this());
                                     }
                                 });
    }

    tcp::socket socket_;
    chat_room& room_;
    auth_state state_;
    std::string nickname_;
    chat_message read_msg_;
    chat_message_queue write_msgs_;
};

class chat_server
{
  public:
    chat_server(boost::asio::io_context& io_context, const tcp::endpoint& endpoint) : acceptor_(io_context, endpoint)
    {
        room_.set_room_name("10001");
        do_accept();
    }

  private:
    void do_accept()
    {
        acceptor_.async_accept([this](boost::system::error_code ec, tcp::socket socket) {
            if (!ec)
            {
                std::make_shared<chat_session>(std::move(socket), room_)->start();
            }

            do_accept();
        });
    }

    tcp::acceptor acceptor_;
    chat_room room_;
};

int main()
{
    try
    {
        boost::asio::io_context io_context;

        tcp::endpoint endpoint(tcp::v4(), 12345);
        chat_server server(io_context, endpoint);

        io_context.run();
    }
    catch (std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}