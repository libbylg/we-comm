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

enum : uint16_t {
    STATUS_CONN_MASK = 0xF000,          //  连接状态掩码: XXXX-****|****-****
    STATUS_CONN_IDLE = 0x0000,          //  已经断开
    STATUS_CONN_CONNECTING = 0x1000,    //  正在连接
    STATUS_CONN_CLEANING = 0x2000,      //  正在清理链接
    STATUS_CONN_CONNECTED = 0x3000,     //  连接成功
    STATUS_CONN_DISCONNECTED = 0x4000,  //  已经断开
    STATUS_CONN_ERROR = 0x5000,         //  已经异常
};

enum : uint16_t {
    STATUS_PROTOCOL_MASK = 0x000F,      //  协议状态掩码:  ***-****|****-XXXX
    STATUS_PROTOCOL_IDLE = 0x0000,      //  刚初始化
    STATUS_PROTOCOL_WAITAUTH = 0x0001,  //  等待认证
    STATUS_PROTOCOL_READY = 0x0002,     //  认证成功
    STATUS_PROTOCOL_ERROR = 0x000F,     //  协议异常
};

enum : uint16_t {
    ATTR_STREAM_TYPE_MASK = 0x0001,      //  连接类型掩码
    ATTR_STREAM_TYPE_ACTIVATE = 0x0000,  //  主动发起的连接
    ATTR_STREAM_TYPE_PASSIVES = 0x0001,  //  被动建立的连接
};

enum : int32_t {
    EVENT_CONN_INITED,
    EVENT_STATUS_CHANGED,
};

enum : int32_t {
    ACTION_NONE,
    ACTION_DISCONNECT,  //  执行断链
    ACTION_RECONNECT,   //  执行重连
    ACTION_REFUSE,      //  不接受新连接
};


struct SMQStream {
    virtual void update_status(uint16_t mask, uint16_t val) = 0;
    virtual uint16_t current_status(uint16_t mask) const = 0;
    virtual uint16_t get_attr(uint16_t mask) const = 0;
    virtual uint16_t get_target() const = 0;
    //    virtual void async_write(MESSAGE* msg) = 0;
};

static inline asio::ip::tcp::endpoint addr_of(const std::string& str)
{
    std::string host;
    unsigned short port;
    int pos = str.find_last_of(":");
    if (pos < 0) {
        host = str;
        port = 9090;
    } else {
        host = str.substr(0, pos);
        port = atoi(str.substr(pos + 1).c_str());
    }

    return asio::ip::tcp::endpoint(asio::ip::make_address(host), port);
}

static inline std::string str_of(const asio::ip::tcp::endpoint& ep)
{
    char buf[10] = {0};
    snprintf(buf, sizeof(buf), "%d", ep.port());
    return ep.address().to_string().append(":").append(buf);
}

template <typename TRANSPORT, typename DISPATCHER, typename ALLOCATOR>
class SMQProtocol
{
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

    void PostAuth(void* s)
    {
        SMQStream* stream = (SMQStream*)s;
        //((TRANSPORT*)this)->debug(stream, "PostAuth");
        MESSAGE* msg = allocator->Alloc(sizeof(MESSAGE) + sizeof(CONNAUTHMsg));
        Q_ASSERT(nullptr != msg);
        CONNAUTHMsg* auth = PayloadOf<CONNAUTHMsg*>(msg);
        auth->code = CONNAUTH;
        auth->source = source;
        msg->TotalLength(sizeof(MESSAGE) + sizeof(CONNAUTHMsg));
        msg->Type(MESSAGE::TYPE_CONN);

        //        Q_ASSERT(nullptr == stream->wcur);
        //        stream->wcur = BufferOf(msg);

        //  启动异步发送
        ((TRANSPORT*)this)->async_write(stream, msg);
    }

    void PostAuthAck(void* s)
    {
        SMQStream* stream = (SMQStream*)s;
        //        ((TRANSPORT*)this)->debug(stream, "PostAuthAck");
        MESSAGE* msg = allocator->Alloc(sizeof(MESSAGE) + sizeof(CONNAUTHACKMsg));
        Q_ASSERT(nullptr != msg);
        CONNAUTHACKMsg* auth = PayloadOf<CONNAUTHACKMsg*>(msg);
        auth->code = CONNAUTHACK;
        auth->source = ((TRANSPORT*)this)->source;
        msg->TotalLength(sizeof(MESSAGE) + sizeof(CONNAUTHACKMsg));
        msg->Type(MESSAGE::TYPE_CONN);

        //  启动异步发送
        ((TRANSPORT*)this)->async_write(stream, msg);
    }

    void HandleConnMessage(void* s, MESSAGE* msg)
    {
        SMQStream* stream = (SMQStream*)s;
        Q_ASSERT(msg->TotalLength() >= (sizeof(MESSAGE) + sizeof(CONNHEAD)));
        CONNHEAD* head = PayloadOf<CONNHEAD*>(msg);
        switch (head->code) {
            case CONNAUTH: {
                //                ((TRANSPORT*)this)->debug(stream, "Receive Auth");
                Q_ASSERT(msg->TotalLength() >= (sizeof(MESSAGE) + sizeof(CONNAUTHMsg)));
                CONNAUTHMsg* req = PayloadOf<CONNAUTHMsg*>(msg);
                Q_ASSERT(MESSAGE::ADDRESS_INVALID != req->source);
                int ret = ((TRANSPORT*)this)->BindStreamChan(stream, req->source);
                if (0 != ret) {
                }
                PostAuthAck(stream);
                stream->update_status(STATUS_PROTOCOL_MASK, STATUS_PROTOCOL_READY);
            } break;
            case CONNAUTHACK: {
                //                ((TRANSPORT*)this)->debug(stream, "Receive AuthAck");
                Q_ASSERT(msg->TotalLength() >= (sizeof(MESSAGE) + sizeof(CONNAUTHACKMsg)));
                CONNAUTHACKMsg* ack = PayloadOf<CONNAUTHACKMsg*>(msg);
                Q_ASSERT(MESSAGE::ADDRESS_INVALID != ack->source);
                int ret = ((TRANSPORT*)this)->BindStreamChan(stream, ack->source);
                if (0 != ret) {
                }
                stream->update_status(STATUS_PROTOCOL_MASK, STATUS_PROTOCOL_READY);
            } break;
            default:
                Q_ASSERT(false);
        }
    }

    int Init(uint16_t selfid, DISPATCHER* disp, ALLOCATOR* alloc)
    {
        source = selfid;
        dispatcher = disp;
        allocator = alloc;
        return 0;
    }

    virtual void HandleEvent(void* s, uint32_t& action, uint16_t event, uintptr_t param1, uintptr_t param2)
    {
        std::printf("HandleEvent: event=%d, [%llu, %llu]\n", event, param1, param2);
        SMQStream* stream = (SMQStream*)s;
        if (EVENT_CONN_INITED == event) {
            stream->update_status(STATUS_PROTOCOL_MASK, STATUS_PROTOCOL_IDLE);
            return;
        }

        if (EVENT_STATUS_CHANGED == event) {
            uint16_t conn_status_old = param1 & STATUS_CONN_MASK;
            uint16_t conn_status_new = param2 & STATUS_CONN_MASK;

            if ((conn_status_old == STATUS_CONN_CONNECTING) && (conn_status_new == STATUS_CONN_CONNECTED)) {
                stream->update_status(STATUS_PROTOCOL_MASK, STATUS_PROTOCOL_WAITAUTH);
                PostAuth(stream);
                return;
            }

            if ((conn_status_old != STATUS_CONN_DISCONNECTED) && (conn_status_new == STATUS_CONN_DISCONNECTED)) {
                stream->update_status(STATUS_PROTOCOL_MASK, STATUS_PROTOCOL_IDLE);
                if (ATTR_STREAM_TYPE_ACTIVATE == stream->get_attr(ATTR_STREAM_TYPE_MASK)) {
                    action = ACTION_RECONNECT;
                    std::printf("Disconnected, try reconnect\n");
                } else {
                    action = ACTION_DISCONNECT;
                }
                return;
            }
        }
    }

    virtual void HandleMessage(void* s, MESSAGE* msg)
    {
        SMQStream* stream = (SMQStream*)(s);
        switch (msg->Type()) {
            case MESSAGE::TYPE_CONN:
                HandleConnMessage(stream, msg);
                break;
            case MESSAGE::TYPE_USER:
                msg->Source(stream->get_target());
                msg->Target(source);
                dispatcher->HandleMessage(stream, msg);
                break;
            default:
                Q_ASSERT(false);
                break;
        }
    }

protected:
    uint16_t source;
    DISPATCHER* dispatcher;
    ALLOCATOR* allocator;
};


template <typename DISPATCHER, typename ALLOCATOR>
class SMQTransport : public SMQProtocol<SMQTransport<DISPATCHER, ALLOCATOR>, DISPATCHER, ALLOCATOR>
{
private:
    struct chan_t;
    struct stream_t : public NODE, public SMQStream {
        asio::ip::tcp::socket socket;
        chan_t* chan;     //  绑定到哪个通道
        BUFFER* wcur;     //  当前正在发送的消息(当wcur为null时,表示需要重启)
        MESSAGE* rcur;    //  当前还未收取完成的消息
        MESSAGE rbuf;     //  消息头缓冲区
        int8_t rhead;     //  是否正在读取消息头
        uint8_t wloss;    //  是否处于写丢失状态
        uint16_t attr;    //  属性
        uint16_t target;  //  流的目的地址
        uint16_t status;  //  当前状态
        std::string targetAddr;
        asio::deadline_timer* timer;

        stream_t(asio::ip::tcp::socket sock, const std::string& addr, uint16_t attr = 0)
            : socket(std::move(sock)), attr(attr)
        {
            chan = nullptr;
            rcur = nullptr;
            wcur = nullptr;
            target = MESSAGE::ADDRESS_INVALID;
            status = STATUS_CONN_IDLE;
            rhead = true;
            wloss = true;
            targetAddr = addr;
            timer = nullptr;
        }

        virtual void update_status(uint16_t mask, uint16_t val) override
        {
            status = (status & ~mask) | (val & mask);
        }

        virtual uint16_t current_status(uint16_t mask) const override
        {
            return (status & mask);
        }

        virtual uint16_t get_attr(uint16_t mask) const override
        {
            return (attr & mask);
        }

        virtual uint16_t get_target() const
        {
            return target;
        }
    };

    struct chan_t : public NODE {
        stream_t* stream;
        NODE qsend;    //  发送队列
        int32_t size;  //  发送队列长度

        chan_t()
        {
            stream = nullptr;
            size = 0;
        }
    };

    typedef SMQProtocol<SMQTransport<DISPATCHER, ALLOCATOR>, DISPATCHER, ALLOCATOR> PARENT;


public:
    SMQTransport()
    {
        acceptor = nullptr;
    }

    int Init(uint16_t selfid, DISPATCHER* disp, ALLOCATOR* alloc, int maxConn)
    {
        Q_ASSERT(nullptr != disp);
        Q_ASSERT(nullptr != alloc);

        int ret = PARENT::Init(selfid, disp, alloc);
        if (0 != ret) {
            return -1;
        }

        //        dispatcher = disp;
        allocator = alloc;
        //        source = selfid;

        chans.resize(maxConn);

        return 0;
    }

    int SetupConnect(const std::string& saddr)
    {
        // asio::ip::tcp::socket;
        auto stream = new stream_t(std::move(asio::ip::tcp::socket(context)), saddr, ATTR_STREAM_TYPE_ACTIVATE);

        asio::ip::tcp::resolver resolver(context);
        auto endpoints = resolver.resolve(std::string("localhost"), std::string("9090"));
        padding.push_back(stream);
        uint32_t action = ACTION_NONE;
        this->HandleEvent(stream, action, EVENT_CONN_INITED, 0, 0);

        UpdateStatus(stream, STATUS_CONN_MASK, STATUS_CONN_CONNECTING);

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

        //        auto endpoints = addr_of(saddr);

        Q_ASSERT(nullptr == acceptor);
        asio::ip::tcp::acceptor* accept = nullptr;
        try {
            accept = new asio::ip::tcp::acceptor(context, *endpoints.begin());
        } catch (system::system_error err) {
            std::printf("Listen port '%s' failed: %s\n", saddr.c_str(), err.what());
            return -1;
        }

        acceptor = accept;

        accept->async_accept([this, accept](const system::error_code& ec, asio::ip::tcp::socket sock) {
            HandleAcceptResult(accept, ec, std::move(sock));
        });
        return 0;
    }

    uint16_t get_attr(void* s, uint16_t mask)
    {
        stream_t* stream = (stream_t*)s;
        return (stream->attr & mask);
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
                async_write(stream, MessageOf(stream->wcur));
            }
        }
        return 0;
    }

    void Loop()
    {
        context.run();
    }

public:
    void HandleConnectResult(stream_t* stream, const system::error_code& err, asio::ip::tcp::endpoint ep)
    {
        if (err) {
            debug(stream, "%p:HandleConnect failed:%d: %s", stream, err.value(),
                  err.message().c_str());  // TODO 错误码是啥
            if (nullptr == stream->timer) {
                // stream->timer = new asio::deadline_timer(context, posix_time::seconds(5));
                stream->timer = new asio::deadline_timer(context);
            }

            stream->timer->expires_from_now(posix_time::seconds(5));
            stream->timer->async_wait([this, stream](system::error_code err) {
                std::printf("timeout - try reconnnect\n");
                stream->timer->cancel();
                this->async_connect(stream);
            });
            return;
        }
        debug(stream, "HandleConnect success: %s", ep.address().to_string().c_str());

        if (nullptr != stream->timer) {
            asio::deadline_timer* timer = stream->timer;
            // stream->timer = nullptr;
            timer->cancel();
            // delete timer;
        }

        UpdateStatus(stream, STATUS_CONN_MASK, STATUS_CONN_CONNECTED);

        stream->rhead = true;
        async_read(stream);
        padding.push_back(stream);
    }

    void HandleAcceptResult(asio::ip::tcp::acceptor* a, const system::error_code& err, asio::ip::tcp::socket sock)
    {
        if (err) {
            debug(nullptr, "HandleAcceptResult failed:%d: %s", err.value(), err.message().c_str());  // TODO 错误码是啥
            return;
        }
        std::printf("HandleAccept success\n");

        std::string saddr;
        system::error_code ec;
        auto endpoint = sock.remote_endpoint(ec);
        if (ec) {
            std::printf("HandleAccept : can not get the remote endpoint:%d:  %s\n", ec.value(), ec.message().c_str());
            saddr = str_of(endpoint);
        }

        auto stream = new stream_t(std::move(sock), saddr, ATTR_STREAM_TYPE_PASSIVES);

        padding.push_back(stream);
        uint32_t action = ACTION_NONE;
        this->HandleEvent(stream, action, EVENT_CONN_INITED, 0, 0);

        stream->rhead = true;
        async_read(stream);

        //  accept others connections
        acceptor->async_accept([this](const system::error_code& ec, asio::ip::tcp::socket sock) {
            HandleAcceptResult(this->acceptor, ec, std::move(sock));
        });
    }

    void HandleReadResult(stream_t* stream, const system::error_code& err, std::size_t length)
    {
        if ((asio::error::eof == err) || (asio::error::connection_reset == err)) {
            debug(stream, "HandleReadResult failed:%d: %s", err.value(), err.message().c_str());  // TODO 错误码是啥
            UpdateStatus(stream, STATUS_CONN_MASK, STATUS_CONN_DISCONNECTED);
            return;
        }

        if (err) {
            debug(stream, "HandleReadResult failed:%d: %s", err.value(), err.message().c_str());  // TODO 错误码是啥
            UpdateStatus(stream, STATUS_CONN_MASK, STATUS_CONN_DISCONNECTED);
            return;
        }
        debug(stream, "HandleReadResult success");

        if (stream->rhead) {
            uint32_t rcurLen = stream->rbuf.TotalLength();
            auto oldrcur = stream->rcur;
            auto newrcur = allocator->Alloc(rcurLen);
            newrcur->FillHeader(stream->rbuf);
            stream->rcur = newrcur;

            stream->rhead = false;
            async_read(stream);

        } else {
            this->HandleMessage((void*)stream, stream->rcur);
            stream->rcur = nullptr;

            stream->rhead = true;
            async_read(stream);
        }
    }

    void HandleWriteResult(stream_t* stream, const system::error_code& err, std::size_t length)
    {
        if ((asio::error::eof == err) || (asio::error::connection_reset == err)) {
            debug(stream, "HandleWriteResult failed:%d: %s", err.value(), err.message().c_str());  // TODO 错误码是啥
            UpdateStatus(stream, STATUS_CONN_MASK, STATUS_CONN_DISCONNECTED);
            return;
        }

        if (err) {
            debug(stream, "HandleWriteResult failed:%d: %s", err.value(), err.message().c_str());  // TODO 错误码是啥
            stream->socket.close();
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
        async_write(stream, MessageOf(stream->wcur));

        //  释放前一个消息
        allocator->Free(MessageOf(oldwcur));
    }

    //  启动异步发送
    void async_write(void* s, MESSAGE* newmsg)
    {
        stream_t* stream = (stream_t*)s;
        if (nullptr != newmsg) {
            stream->wcur = BufferOf(newmsg);
        }

        if (stream->wcur) {
            stream->wloss = true;
        }

        stream->wloss = false;
        Q_ASSERT(nullptr != stream->wcur);
        MESSAGE* msg = MessageOf(stream->wcur);
        asio::async_write(
            stream->socket, asio::buffer(msg, msg->TotalLength()),
            [this, stream](system::error_code ec, std::size_t len) { HandleWriteResult(stream, ec, len); });
    }

    //  启动异步发送
    void async_read(stream_t* stream)
    {
        if (true == stream->rhead) {
            asio::async_read(stream->socket, asio::buffer(&(stream->rbuf), sizeof(stream->rbuf)),
                             [this, stream](const system::error_code& ec, std::size_t length) {
                                 HandleReadResult(stream, ec, length);
                             });
        } else {
            asio::async_read(stream->socket,
                             asio::buffer(stream->rcur->payload, (stream->rcur->TotalLength() - sizeof(MESSAGE))),
                             [this, stream](const system::error_code& ec, std::size_t length) {
                                 HandleReadResult(stream, ec, length);
                             });
        }
    }

    void async_connect(stream_t* stream)
    {
        asio::ip::tcp::resolver resolver(context);
        //        asio::ip::tcp::endpoint ep = addr_of(stream->targetAddr);

        auto endpoints = resolver.resolve(std::string("localhost"), std::string("9090"));

        padding.push_back(stream);
        uint32_t action = ACTION_NONE;
        this->HandleEvent(stream, action, EVENT_CONN_INITED, 0, 0);

        UpdateStatus(stream, STATUS_CONN_MASK, STATUS_CONN_CONNECTING);

        asio::async_connect(stream->socket, endpoints,
                            [this, stream](const system::error_code& ec, asio::ip::tcp::endpoint ep) {
                                HandleConnectResult(stream, ec, ep);
                            });
    }

    int debug(stream_t* stream, const char* format, ...)
    {
        // std::printf("[%p][%d-%d]", stream, source, stream->target);
        if (nullptr != stream) {
            std::printf("[%p][??-%d]", stream, stream->target);
        }
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

public:
    int BindStreamChan(void* s, uint16_t target)
    {
        stream_t* stream = (stream_t*)s;
        chan_t* chan = ChanOf(target);
        if (nullptr != chan->stream) {
            return -1;
        }

        stream->target = target;

        stream->chan = chan;
        chan->stream = stream;
        return 0;
    }

    virtual void UpdateStatus(void* s, uint16_t mask, uint16_t val)
    {
        stream_t* stream = (stream_t*)s;
        uint16_t oldstatus = stream->status;
        stream->update_status(mask, val);
        uint16_t newstatus = stream->status;

        if (oldstatus != newstatus) {
            uint32_t action = ACTION_NONE;
            this->HandleEvent(stream, action, EVENT_STATUS_CHANGED, oldstatus, newstatus);
            if (action == ACTION_DISCONNECT) {
                stream->socket.close();
            }
            if (action == ACTION_RECONNECT) {
                stream->socket.close();
                async_connect(stream);
            }
        }
    }


protected:
    asio::io_context context;           //  网络IO上下文, asio::io_context
    ALLOCATOR* allocator;               //  消息对象分配器
    asio::ip::tcp::acceptor* acceptor;  //  连接器
    std::vector<chan_t> chans;          //  所有可能的流对象列表
    NODE padding;                       //  处于待命状态的连接

    //    DISPATCHER* dispatcher;             //  消息分发器
    //    uint16_t source;                    //  源地址
};

#endif  // WECOMM_H
