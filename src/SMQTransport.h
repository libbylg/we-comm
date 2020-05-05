#ifndef WECOMM_H
#define WECOMM_H

#define BOOST_ASIO_NO_DEPRECATED
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <map>
#include <string>
#include <vector>
using namespace boost;

#include "MESSAGE.h"

enum EndpointType {
    TYPE_CLIENT = 0,  //
    TYPE_SERVER = 1,
};


template <typename CONTEXT, typename DISPATCHER, typename ALLOCATOR>
class SMQTransport
{
private:
    enum : uint16_t {
        STATUS_CONN_MASK = 0xC000,                //  连接状态掩码: XX**-****|****-****
        STATUS_CONN_DISCONNECTED = 0x0000 << 14,  //  已经断开
        STATUS_CONN_CONNECTING = 0x0001 << 14,    //  正在连接
        STATUS_CONN_CLEANING = 0x0002 << 14,      //  正在清理链接
        STATUS_CONN_CONNECTED = 0x0003 << 14,     //  连接成功

        STATUS_PROTOCOL_MASK = 0x000F,      //  协议状态掩码:  ***-****|****-XXXX
        STATUS_PROTOCOL_IDLE = 0x0000,      //  等待认证
        STATUS_PROTOCOL_WAITAUTH = 0x0001,  //  等待认证
        STATUS_PROTOCOL_READY = 0x0002,     //  认证成功
        STATUS_PROTOCOL_ERROR = 0x000F,     //  协议异常
    };

    struct chan_t;
    struct stream_t : public NODE {
        asio::ip::tcp::socket socket;
        chan_t* chan;     //  绑定到哪个通道
        MESSAGE* rcur;    //  当前还未收取完成的消息
        MESSAGE* wcur;    //  当前还未收取完成的消息(当wcur为null时,表示需要重启)
        uint16_t source;  //  流的发源地址
        uint16_t target;  //  流的目的地址
        uint16_t status;  //  当前状态

        stream_t(uint16_t s, asio::ip::tcp::socket sock) : socket(std::move(sock))
        {
            chan = nullptr;
            rcur = nullptr;
            wcur = nullptr;
            source = s;
            target = MESSAGE::ADDR_INVALID;
            status = STATUS_CONN_DISCONNECTED | STATUS_PROTOCOL_IDLE;
        }
    };

    struct chan_t : public NODE {
        stream_t* stream;
        NODE qsend;      //  发送队列
        int32_t size;    //  发送队列长度
        int16_t target;  //  目标端的id

        chan_t()
        {
            stream = nullptr;
        }
    };


public:
    SMQTransport(CONTEXT& context, int maxConn) : context(context)
    {
        acceptor = nullptr;
        chans.resize(maxConn);
    }

    int Init(DISPATCHER* disp, ALLOCATOR* alloc)
    {
        Q_ASSERT(nullptr != disp);
        Q_ASSERT(nullptr != alloc);

        dispatcher = disp;
        allocator = alloc;

        return 0;
    }

    int SetupConnect(const std::string& saddr)
    {
        asio::ip::tcp::resolver resolver(context);
        auto endpoints = resolver.resolve(std::string("localhost"), std::string("9090"));

        // asio::ip::tcp::socket;
        auto stm = new stream_t(0, std::move(asio::ip::tcp::socket(context)));
        padding.push_back(stm);
        asio::async_connect(
            stm->socket, endpoints,
            [this, stm](const system::error_code& ec, asio::ip::tcp::endpoint ep) { HandleConnect(stm, ec, ep); });
        return 0;
    }

    int SetupAcceptor(const std::string& saddr)
    {
        asio::ip::tcp::resolver resolver(context);
        auto endpoints = resolver.resolve(std::string("localhost"), std::string("9090"));

        Q_ASSERT(nullptr == acceptor);
        auto accept = new asio::ip::tcp::acceptor(context, *endpoints.begin());
        Q_ASSERT(nullptr != accept);
        acceptor = accept;
        accept->async_accept([this, accept](const system::error_code& ec, asio::ip::tcp::socket sock) {
            HandleAccept(accept, ec, std::move(sock));
        });
        return 0;
    }

    int Post(MESSAGE* msg)
    {
        Q_ASSERT(msg != nullptr);

        BUFFER* buf = BufferOf(msg);
        Q_ASSERT(buf->source < chans.size());
        Q_ASSERT(buf->target < chans.size());

        chan_t* chan = ChanOf(buf->target);
        if (nullptr == chan) {
            return -1;
        }

        chan->push_back(BufferOf(msg));

        if (nullptr != chan->stream) {
            auto stream = chan->stream;
            stream->wcur = (MESSAGE*)chan->qsend.pop_front();
            asio::async_write(
                stream->socket, asio::buffer(stream->wcur, stream->wcur->Length()),
                [this, stream](boost::system::error_code ec, std::size_t len) { HandleWrite(stream, ec, len); });
        }
        return 0;
    }


protected:
    void HandleConnect(stream_t* stream, const system::error_code& err, asio::ip::tcp::endpoint ep)
    {
        if (err) {
            printf("HandleConnect failed:%d\n", err.value());
            return;
        }
        printf("HandleConnect success\n");

        if (nullptr == stream->rcur) {
            stream->rcur = allocator->Alloc(MESSAGE::TOTAL_LENGTH_DEF);
            Q_ASSERT(nullptr != stream->rcur);
        }

        padding.push_back(stream);

        asio::async_read(stream->socket, asio::buffer(stream->rcur, sizeof(MESSAGE)),
                         [this, stream](const boost::system::error_code& ec, std::size_t length) {
                             HandleReadHeader(stream, ec, length);
                         });
    }

    void HandleAccept(asio::ip::tcp::acceptor* a, const system::error_code& err, asio::ip::tcp::socket sock)
    {
        if (err) {
            printf("HandleAccept failed:%d\n", err.value());
            return;
        }
        printf("HandleAccept success\n");

        auto stream = new stream_t(0, std::move(sock));

        padding.push_back(stream);

        if (nullptr == stream->rcur) {
            stream->rcur = allocator->Alloc(MESSAGE::TOTAL_LENGTH_DEF);
            Q_ASSERT(nullptr != stream->rcur);
        }

        asio::async_read(stream->socket, asio::buffer(stream->rcur, sizeof(MESSAGE)),
                         [this, stream](const boost::system::error_code& ec, std::size_t length) {
                             HandleReadHeader(stream, ec, length);
                         });
    }

    void HandleReadHeader(stream_t* stream, const system::error_code& err, std::size_t length)
    {
        if (err) {
            printf("HandleReadHeader failed:%d\n", err.value());
            return;
        }
        printf("HandleReadHeader success\n");

        //  确保缓冲区足够大,如果运气足够好,那么不需要额外搬移数据
        uint32_t rcurLen = stream->rcur->Length();
        if (rcurLen > BufferOf(stream->rcur)->cap) {
            auto oldrcur = stream->rcur;
            auto newrcur = allocator->Alloc(rcurLen);
            newrcur->FillHeader(*oldrcur);
            stream->rcur = newrcur;
            allocator->Free(stream->rcur);
        }

        asio::async_read(stream->socket,
                         asio::buffer(stream->rcur->payload, (stream->rcur->Length() - sizeof(MESSAGE))),
                         [this, stream](const boost::system::error_code& ec, std::size_t length) {
                             HandleReadBody(stream, ec, length);
                         });
    }

    void HandleReadBody(stream_t* stream, const system::error_code& err, std::size_t length)
    {
        if (err) {
            printf("HandleReadBody failed:%d\n", err.value());
            return;
        }
        printf("HandleReadBody success\n");

        dispatcher->HandleMessage(stream->rcur);

        auto oldrcur = stream->rcur;
        stream->rcur = allocator->Alloc(MESSAGE::TOTAL_LENGTH_DEF);
        Q_ASSERT(nullptr != stream->rcur);

        allocator->Free(oldrcur);
        asio::async_read(stream->socket, asio::buffer(stream->rcur, sizeof(MESSAGE)),
                         [this, stream](const boost::system::error_code& ec, std::size_t length) {
                             HandleReadHeader(stream, ec, length);
                         });
    }

    void HandleWrite(stream_t* stream, const system::error_code& err, std::size_t length)
    {
        if (err) {
            printf("HandleReadBody failed:%d\n", err.value());
            return;
        }
        printf("HandleReadBody success\n");

        //  确定流所绑定的索引
        if (stream->target == MESSAGE::ADDR_INVALID) {
            return;
        }

        //  找到传送通道
        auto chan = ChanOf(stream->target);
        Q_ASSERT(nullptr != chan);

        //  保存前一个消息
        auto oldwcur = stream->wcur;

        //  直接覆盖前一个消息的地址
        stream->wcur = (MESSAGE*)chan->qsend.pop_front();

        //  启动异步发送
        asio::async_write(
            stream->socket, asio::buffer(stream->wcur, stream->wcur->Length()),
            [this, stream](boost::system::error_code ec, std::size_t len) { HandleWrite(stream, ec, len); });

        //  释放前一个消息
        allocator->Free(oldwcur);
    }

    inline chan_t* ChanOf(int16_t id)
    {
        if ((id < 0) || (id > chans.size())) {
            return nullptr;
        }

        return &chans[id];
    }

protected:
    CONTEXT& context;                   //  网络IO上下文, asio::io_context
    DISPATCHER* dispatcher;             //  消息分发器
    ALLOCATOR* allocator;               //  消息对象分配器
    asio::ip::tcp::acceptor* acceptor;  //  连接器
    std::vector<chan_t> chans;          //  所有可能的流对象列表
    NODE padding;                       //  处于待命状态的连接
};

#endif  // WECOMM_H
