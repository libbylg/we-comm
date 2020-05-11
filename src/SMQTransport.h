#ifndef WECOMM_H
#define WECOMM_H

#define BOOST_ASIO_NO_DEPRECATED
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
using namespace boost;

#include "MESSAGE.h"

enum EndpointType {
    TYPE_CLIENT = 0,  //
    TYPE_SERVER = 1,
};


template <typename DISPATCHER, typename ALLOCATOR>
class SMQTransport
{
private:
    enum : uint16_t {
        STATUS_CONN_MASK = 0xF000,          //  连接状态掩码: XXXX-****|****-****
        STATUS_CONN_IDLE = 0x0000,          //  已经断开
        STATUS_CONN_CONNECTING = 0x1000,    //  正在连接
        STATUS_CONN_CLEANING = 0x2000,      //  正在清理链接
        STATUS_CONN_CONNECTED = 0x3000,     //  连接成功
        STATUS_CONN_DISCONNECTED = 0x4000,  //  已经断开

        STATUS_PROTOCOL_MASK = 0x000F,      //  协议状态掩码:  ***-****|****-XXXX
        STATUS_PROTOCOL_IDLE = 0x0000,      //  等待认证
        STATUS_PROTOCOL_WAITAUTH = 0x0001,  //  等待认证
        STATUS_PROTOCOL_READY = 0x0002,     //  认证成功
        STATUS_PROTOCOL_ERROR = 0x000F,     //  协议异常
    };

    enum : int32_t {
        EVENT_CONN_STATUS_CHANGED,
    };

    struct chan_t;
    struct stream_t : public NODE {
        asio::ip::tcp::socket socket;
        chan_t* chan;     //  绑定到哪个通道
        MESSAGE* rcur;    //  当前还未收取完成的消息
        BUFFER* wcur;     //  当前正在发送的消息(当wcur为null时,表示需要重启)
        uint16_t target;  //  流的目的地址
        uint16_t status;  //  当前状态
        uint32_t wloss;   //  是否处于写丢失状态

        stream_t(asio::ip::tcp::socket sock) : socket(std::move(sock))
        {
            chan = nullptr;
            rcur = nullptr;
            wcur = nullptr;
            target = MESSAGE::ADDRESS_INVALID;
            status = STATUS_CONN_IDLE | STATUS_PROTOCOL_IDLE;
            wloss = true;
        }

        void update_status(uint16_t mask, uint16_t val)
        {
            status = (status & ~mask) | (val & mask);
        }

        void current_status(uint16_t mask)
        {
            return (status & mask);
        }
    };

    struct chan_t : public NODE {
        stream_t* stream;
        NODE qsend;    //  发送队列
        int32_t size;  //  发送队列长度

        chan_t()
        {
            stream = nullptr;
        }
    };


public:
    SMQTransport()
    {
        acceptor = nullptr;
    }

    int Init(uint16_t selfid, DISPATCHER* disp, ALLOCATOR* alloc, int maxConn)
    {
        Q_ASSERT(nullptr != disp);
        Q_ASSERT(nullptr != alloc);

        dispatcher = disp;
        allocator = alloc;
        source = selfid;

        chans.resize(maxConn);

        return 0;
    }

    int SetupConnect(const std::string& saddr)
    {
        asio::ip::tcp::resolver resolver(context);
        auto endpoints = resolver.resolve(std::string("localhost"), std::string("9090"));

        // asio::ip::tcp::socket;
        auto stream = new stream_t(std::move(asio::ip::tcp::socket(context)));
        padding.push_back(stream);


        UpdateStatus(stream, STATUS_CONN_MASK, STATUS_CONN_CONNECTING);
        // stream->update_status(STATUS_CONN_MASK, STATUS_CONN_CONNECTING);

        asio::async_connect(stream->socket, endpoints,
                            [this, stream](const system::error_code& ec, asio::ip::tcp::endpoint ep) {
                                HandleConnectResult(stream, ec, ep);
                            });
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
            HandleAcceptResult(accept, ec, std::move(sock));
        });
        return 0;
    }

    int Post(MESSAGE* msg)
    {
        std::printf("Post external message\n");

        Q_ASSERT(msg != nullptr);

        BUFFER* buf = BufferOf(msg);
        //        Q_ASSERT(buf->source < chans.size());
        Q_ASSERT(buf->target < chans.size());

        chan_t* chan = ChanOf(buf->target);
        if (nullptr == chan) {
            return -1;
        }

        chan->qsend.push_back(BufferOf(msg));

        if (nullptr != chan->stream) {
            auto stream = chan->stream;

            //  如果当前处于写丢失状态,那么重启写操作
            if (stream->wloss) {
                stream->wcur = (BUFFER*)(chan->qsend.pop_front());
                if (nullptr != stream->wcur) {
                    async_write(stream);
                }
            }
        }
        return 0;
    }

    void Loop()
    {
        context.run();
    }

protected:
    void HandleConnectResult(stream_t* stream, const system::error_code& err, asio::ip::tcp::endpoint ep)
    {
        if (err) {
            debug(stream, "HandleConnect failed:%d", err.value());
            return;
        }
        debug(stream, "HandleConnect success");

        if (nullptr == stream->rcur) {
            stream->rcur = allocator->Alloc(MESSAGE::TOTAL_LENGTH_DEF);
            Q_ASSERT(nullptr != stream->rcur);
        }

        UpdateStatus(stream, STATUS_CONN_MASK, STATUS_CONN_CONNECTED);
        // stream->update_status(STATUS_CONN_MASK, STATUS_CONN_CONNECTED);

        asio::async_read(stream->socket, asio::buffer(stream->rcur, sizeof(MESSAGE)),
                         [this, stream](const boost::system::error_code& ec, std::size_t length) {
                             HandleReadHeader(stream, ec, length);
                         });

        padding.push_back(stream);
    }

    void HandleAcceptResult(asio::ip::tcp::acceptor* a, const system::error_code& err, asio::ip::tcp::socket sock)
    {
        if (err) {
            std::printf("HandleAccept failed:%d\n", err.value());
            return;
        }
        std::printf("HandleAccept success\n");

        auto stream = new stream_t(std::move(sock));

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

    void HandleReceiveResult(stream_t* stream, const system::error_code& err, std::size_t length)
    {
    }

    void HandleReadHeader(stream_t* stream, const system::error_code& err, std::size_t length)
    {
        if (err) {
            debug(stream, "HandleReadHeader failed:%d", err.value());
            return;
        }
        debug(stream, "HandleReadHeader success");

        //  确保缓冲区足够大,如果运气足够好,那么不需要额外搬移数据
        uint32_t rcurLen = stream->rcur->TotalLength();
        if (rcurLen > BufferOf(stream->rcur)->cap) {
            auto oldrcur = stream->rcur;
            auto newrcur = allocator->Alloc(rcurLen);
            newrcur->FillHeader(*oldrcur);
            stream->rcur = newrcur;
            allocator->Free(stream->rcur);
        }

        asio::async_read(stream->socket,
                         asio::buffer(stream->rcur->payload, (stream->rcur->TotalLength() - sizeof(MESSAGE))),
                         [this, stream](const boost::system::error_code& ec, std::size_t length) {
                             HandleReadBody(stream, ec, length);
                         });
    }

    void HandleReadBody(stream_t* stream, const system::error_code& err, std::size_t length)
    {
        if (err) {
            debug(stream, "HandleReadBody failed:%d", err.value());
            return;
        }
        debug(stream, "HandleReadBody success");

        HandleMessage(stream, stream->rcur);

        auto oldrcur = stream->rcur;
        stream->rcur = allocator->Alloc(MESSAGE::TOTAL_LENGTH_DEF);
        Q_ASSERT(nullptr != stream->rcur);

        allocator->Free(oldrcur);
        asio::async_read(stream->socket, asio::buffer(stream->rcur, sizeof(MESSAGE)),
                         [this, stream](const boost::system::error_code& ec, std::size_t length) {
                             HandleReadHeader(stream, ec, length);
                         });
    }

    void HandleWriteResult(stream_t* stream, const system::error_code& err, std::size_t length)
    {
        if (err) {
            debug(stream, "HandleWriteResult failed:%d", err.value());
            stream->wloss = true;
            return;
        }
        debug(stream, "HandleWriteResult success");

        //  确定流所绑定的索引
        if (stream->target == MESSAGE::ADDRESS_INVALID) {
            stream->wloss = true;
            return;
        }

        //  找到传送通道
        auto chan = ChanOf(stream->target);
        Q_ASSERT(nullptr != chan);

        //  保存前一个消息
        auto oldwcur = stream->wcur;

        //  直接覆盖前一个消息的地址
        stream->wcur = (BUFFER*)(chan->qsend.pop_front());
        if (nullptr == stream->wcur) {
            stream->wloss = true;
            return;
        }

        //  启动异步发送
        async_write(stream);

        //  释放前一个消息
        allocator->Free(MessageOf(oldwcur));
    }

    //  启动异步发送
    void async_write(stream_t* stream)
    {
        stream->wloss = false;
        Q_ASSERT(nullptr != stream->wcur);
        MESSAGE* msg = MessageOf(stream->wcur);
        asio::async_write(
            stream->socket, asio::buffer(msg, msg->TotalLength()),
            [this, stream](boost::system::error_code ec, std::size_t len) { HandleWriteResult(stream, ec, len); });
    }

    //  启动异步发送
    void async_receive(stream_t* stream)
    {
        asio::async_read(stream->socket, asio::buffer(stream->rcur, sizeof(MESSAGE)),
                         [this, stream](const boost::system::error_code& ec, std::size_t length) {
                             HandleReadHeader(stream, ec, length);
                         });
    }

    int debug(stream_t* stream, const char* format, ...)
    {
        std::printf("[%p][%d-%d]", stream, source, stream->target);
        va_list valist;
        va_start(valist, format);
        std::vprintf(format, valist);
        va_end(valist);
        std::printf("\n");

        return 0;
    }

    inline chan_t* ChanOf(int16_t id)
    {
        if ((id < 0) || (id > chans.size())) {
            return nullptr;
        }

        return &chans[id];
    }

protected:
    enum {
        CONNAUTH = 1,
        CONNAUTHACK = 2,
    };
    struct CONNHEAD {
        uint16_t code;  //  type & length
    };
    struct CONNAUTHMsg : public CONNHEAD {
        uint16_t source;
    };

    struct CONNAUTHACKMsg : public CONNHEAD {
        uint16_t source;
    };

    void PostAuth(stream_t* stream)
    {
        debug(stream, "PostAuth");
        MESSAGE* msg = allocator->Alloc(sizeof(MESSAGE) + sizeof(CONNAUTHMsg));
        Q_ASSERT(nullptr != msg);
        CONNAUTHMsg* auth = PayloadOf<CONNAUTHMsg*>(msg);
        auth->code = CONNAUTH;
        auth->source = source;
        msg->TotalLength(sizeof(MESSAGE) + sizeof(CONNAUTHMsg));
        msg->Type(MESSAGE::TYPE_CONN);

        Q_ASSERT(nullptr == stream->wcur);
        stream->wcur = BufferOf(msg);

        //  启动异步发送
        async_write(stream);
    }

    void PostAuthAck(stream_t* stream)
    {
        debug(stream, "PostAuthAck");
        MESSAGE* msg = allocator->Alloc(sizeof(MESSAGE) + sizeof(CONNAUTHACKMsg));
        Q_ASSERT(nullptr != msg);
        CONNAUTHACKMsg* auth = PayloadOf<CONNAUTHACKMsg*>(msg);
        auth->code = CONNAUTHACK;
        auth->source = source;
        msg->TotalLength(sizeof(MESSAGE) + sizeof(CONNAUTHACKMsg));
        msg->Type(MESSAGE::TYPE_CONN);

        debug(stream, "assert pos");
        Q_ASSERT(nullptr == stream->wcur);
        stream->wcur = BufferOf(msg);

        //  启动异步发送
        async_write(stream);
    }

    void HandleConnMessage(stream_t* stream, MESSAGE* msg)
    {
        Q_ASSERT(msg->TotalLength() >= (sizeof(MESSAGE) + sizeof(CONNHEAD)));
        CONNHEAD* head = PayloadOf<CONNHEAD*>(msg);
        switch (head->code) {
            case CONNAUTH: {
                debug(stream, "Receive Auth");
                Q_ASSERT(msg->TotalLength() >= (sizeof(MESSAGE) + sizeof(CONNAUTHMsg)));
                CONNAUTHMsg* req = PayloadOf<CONNAUTHMsg*>(msg);
                Q_ASSERT(MESSAGE::ADDRESS_INVALID != req->source);
                stream->target = req->source;
                BindStreamChan(stream, ChanOf(stream->target));
                PostAuthAck(stream);
                stream->update_status(STATUS_PROTOCOL_MASK, STATUS_PROTOCOL_READY);
            } break;
            case CONNAUTHACK: {
                debug(stream, "Receive AuthAck");
                Q_ASSERT(msg->TotalLength() >= (sizeof(MESSAGE) + sizeof(CONNAUTHACKMsg)));
                CONNAUTHACKMsg* ack = PayloadOf<CONNAUTHACKMsg*>(msg);
                Q_ASSERT(MESSAGE::ADDRESS_INVALID != ack->source);
                stream->target = ack->source;
                BindStreamChan(stream, ChanOf(stream->target));
                stream->update_status(STATUS_PROTOCOL_MASK, STATUS_PROTOCOL_READY);
            } break;
            default:
                Q_ASSERT(false);
        }
    }

    void BindStreamChan(stream_t* stream, chan_t* chan)
    {
        stream->chan = chan;
        chan->stream = stream;
    }

    void UpdateStatus(stream_t* stream, uint16_t mask, uint16_t val)
    {
        uint16_t oldstatus = (stream->status & mask);
        stream->update_status(mask, val);
        uint16_t newstatus = (stream->status & mask);

        if (oldstatus != newstatus) {
            HandleEvent(stream, EVENT_CONN_STATUS_CHANGED, oldstatus, newstatus);
        }
    }

    void HandleEvent(stream_t* stream, uint16_t event, uintptr_t param1, uintptr_t param2)
    {
        if (EVENT_CONN_STATUS_CHANGED == event) {
            if ((param1 == STATUS_CONN_CONNECTING) && (param2 == STATUS_CONN_CONNECTED)) {
                stream->update_status(STATUS_PROTOCOL_MASK, STATUS_PROTOCOL_WAITAUTH);
                PostAuth(stream);
            }
        }
    }

    void HandleMessage(stream_t* stream, MESSAGE* msg)
    {
        switch (msg->Type()) {
            case MESSAGE::TYPE_CONN:
                HandleConnMessage(stream, msg);
                break;
            case MESSAGE::TYPE_USER:
                dispatcher->HandleMessage(msg);
                break;
            default:
                Q_ASSERT(false);
                break;
        }
    }


protected:
    asio::io_context context;           //  网络IO上下文, asio::io_context
    DISPATCHER* dispatcher;             //  消息分发器
    ALLOCATOR* allocator;               //  消息对象分配器
    asio::ip::tcp::acceptor* acceptor;  //  连接器
    std::vector<chan_t> chans;          //  所有可能的流对象列表
    NODE padding;                       //  处于待命状态的连接
    uint16_t source;                    //  源地址
};

#endif  // WECOMM_H
