#ifndef WECOMM_H
#define WECOMM_H

#define BOOST_ASIO_NO_DEPRECATED
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <map>
#include <string>
#include <vector>
using namespace boost;

#include "assert.h"

#ifndef ASSERT
#define ASSERT(expr) assert(expr)
#endif  // ASSERT

#include "WeMessage.h"

enum EndpointType {
    TYPE_CLIENT = 0,  //
    TYPE_SERVER = 1,
};

template <typename T>
void list_insert(T* n, T* prev, T* next)
{
    n->next = next;
    n->prev = prev;
    prev->next = next;
    next->prev = prev;
}

struct WeConn {
    WeConn* next;
    WeConn* prev;
    WeConn()
    {
        next = this;
        prev = this;
    }
};


template <typename CONTEXT, typename DISPATCHER, typename ALLOCATOR>
class WeComm
{
    struct WeStream : public WeConn {
        int index;
        WeComm* comm;
        WeMessage header;
        WeMessage* curr;
        asio::ip::tcp::socket socket;

        WeStream(asio::ip::tcp::socket&& sock) : socket(std::move(sock))
        {
            curr = nullptr;
        }

        //        void HandleConnect(const system::error_code& ec, const asio::ip::tcp::endpoint& ep)
        //        {
        //            comm->HandleConnect(ec, ep, this);
        //        }

        //        virtual void HandleReadHeader(system::error_code ec)
        //        {
        //            comm->HandleReadHeader(ec, this);
        //        }

        //        virtual void HandleReadBody(system::error_code ec)
        //        {
        //            comm->HandleReadBody(ec, this);
        //        }
    };

public:
    WeComm(CONTEXT& context, DISPATCHER& disp, ALLOCATOR& allocator)
        : context(context), dispatcher(disp), allocator(allocator)
    {
        acceptor = nullptr;
    }

    virtual int Setup(EndpointType et, int index, const std::string& saddr)
    {
        //        asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), 9090);
        asio::ip::tcp::resolver resolver(context);
        auto endpoints = resolver.resolve(std::string("localhost"), std::string("9090"));


        if (TYPE_CLIENT == et) {
            asio::ip::tcp::socket socket(context);
            WeStream* stream = new WeStream(std::move(socket));
            stream->index = index;
            stream->comm = this;
            asio::async_connect(stream->socket, endpoints,
                                [this, stream](const system::error_code& ec, asio::ip::tcp::endpoint ep) {
                                    HandleConnect(ec, ep, stream);
                                });
            return 0;
        } else if (TYPE_SERVER == et) {
            ASSERT(nullptr == acceptor);
            auto accept = new asio::ip::tcp::acceptor(context, *endpoints.begin());
            ASSERT(nullptr != accept);
            acceptor = accept;
            accept->async_accept([this, accept](const system::error_code& ec, asio::ip::tcp::socket sock) {
                HandleAccept(ec, std::move(sock), accept);
            });
            return 0;
        }

        return -1;
    };

    virtual void HandleConnect(const system::error_code& err, asio::ip::tcp::endpoint ep, WeStream* stream)
    {
        if (err) {
            printf("HandleConnect failed:%d\n", err.value());
            return;
        }
        printf("HandleConnect success\n");

        asio::async_read(stream->socket, asio::buffer(&(stream->header), sizeof(stream->header)),
                         [this, stream](const boost::system::error_code& ec, std::size_t length) {
                             HandleReadHeader(ec, length, stream);
                         });
    }

    virtual void HandleAccept(const system::error_code& err, asio::ip::tcp::socket sock, asio::ip::tcp::acceptor* a)
    {
        if (err) {
            printf("HandleAccept failed:%d\n", err.value());
            return;
        }
        printf("HandleAccept success\n");

        WeStream* stream = new WeStream(std::move(sock));
        stream->comm = this;
        list_insert((WeConn*)stream, padding.prev, &padding);

        asio::async_read(stream->socket, asio::buffer(&(stream->header), sizeof(stream->header)),
                         [this, stream](const boost::system::error_code& ec, std::size_t length) {
                             HandleReadHeader(ec, length, stream);
                         });
    }

    virtual void HandleReadHeader(const system::error_code& err, std::size_t length, WeStream* stream)
    {
        if (err) {
            printf("HandleReadHeader failed:%d\n", err.value());
            return;
        }
        printf("HandleReadHeader success\n");

        if (nullptr == stream->curr) {
            stream->curr = allocator.Alloc(stream->header.Length());
            WeMessage* curmsg = stream->curr;
            curmsg->Header(stream->header);
            asio::async_read(stream->socket, asio::buffer(stream->curr->Payload(), stream->curr->PayloadLength()),
                             [this, stream](const boost::system::error_code& ec, std::size_t length) {
                                 HandleReadBody(ec, length, stream);
                             });
        }
    }

    virtual void HandleReadBody(const system::error_code& err, std::size_t length, WeStream* stream)
    {
        if (err) {
            printf("HandleReadBody failed:%d\n", err.value());
            return;
        }
        printf("HandleReadBody success\n");
    }

    virtual void HandleWrite(const system::error_code& err)
    {
    }

public:
    CONTEXT& context;
    DISPATCHER& dispatcher;
    ALLOCATOR& allocator;
    asio::ip::tcp::acceptor* acceptor;
    std::map<int, WeStream*> streams;  //  已经通过认证的流对象
    WeConn padding;
};

#endif  // WECOMM_H
