#ifndef WEMESSAGE_H
#define WEMESSAGE_H

#include <inttypes.h>

#include <cstdlib>
#include <cstring>


#ifndef Q_ASSERT
#include "assert.h"
#define Q_ASSERT(expr) assert(expr)
#endif  // ASSERT


template <typename T>
static inline void list_insert(T* n, T* prev, T* next)
{
    n->next = next;
    n->prev = prev;
    prev->next = next;
    next->prev = prev;
}

template <typename T>
static inline void list_remove(T* prev, T* next)
{
    prev->next = next;
    next->prev = prev;
}

struct MESSAGE;
struct BUFFER;

struct NODE {
    NODE* next;
    NODE* prev;

    NODE()
    {
        next = this;
        prev = this;
    }

    inline bool empty() const
    {
        return (next == this);
    }

    inline void push_back(NODE* buf)
    {
        list_insert(buf, prev, (NODE*)this);
    }

    inline NODE* pop_front()
    {
        if (empty()) {
            return nullptr;
        }

        NODE* front = next;
        list_remove(front->prev, front->next);
        return front;
    }
};

struct BUFFER : public NODE {
    int32_t cap;
    int32_t padding;
    int16_t ref;
    int16_t style;
    uint16_t source;
    uint16_t target;
};


inline MESSAGE* MessageOf(BUFFER* buf)
{
    return (MESSAGE*)(((uint8_t*)buf) + sizeof(BUFFER));
}


inline const MESSAGE* MessageOf(const BUFFER* buf)
{
    return (const MESSAGE*)(((uint8_t*)buf) + sizeof(BUFFER));
}


inline BUFFER* BufferOf(MESSAGE* msg)
{
    return (BUFFER*)(((uint8_t*)msg) - sizeof(BUFFER));
}


inline const BUFFER* BufferOf(const MESSAGE* msg)
{
    return (const BUFFER*)(((uint8_t*)msg) - sizeof(BUFFER));
}


struct MESSAGE {
    uint32_t vfl;        //  version(3),flags(5),length(24)
    uint32_t session;    //  session id
    uint8_t payload[0];  //  payload header

    enum : uint8_t {
        VERSION = 0x00,
    };

    enum : uint8_t {
        FLAGS_BYTEORDER = 0,  //  Little-Endian
    };

    enum : uint16_t {
        ADDR_INVALID = 0xFFFF,
    };

    enum : uint32_t {
        TOTAL_LENGTH_DEF = 64,
        TOTAL_LENGTH_MAX = 0x00FFFFFF,
    };

    inline uint16_t Source() const
    {
        const BUFFER* buf = BufferOf(this);
        return buf->source;
    }

    inline void Source(uint16_t s)
    {
        BUFFER* buf = BufferOf(this);
        buf->source = s;
    }

    inline uint8_t Version() const
    {
        return (vfl >> 29);
    }

    inline void Version(uint8_t ver)
    {
        vfl = (vfl & 0x3FFFFFFF) | ((uint32_t(ver) << 29) & 0xE0000000);
    }

    inline uint8_t Flags() const
    {
        return ((vfl & 0x1F000000) >> 24);
    }

    inline void Flags(uint8_t flags)
    {
        vfl = (vfl & 0xEFFFFFFF) | ((uint32_t(flags) << 24) & 0x30000000);
    }

    inline int32_t Length() const
    {
        return (vfl & 0x00FFFFFF);
    }

    inline void Length(uint32_t len)
    {
        Q_ASSERT(len < MESSAGE::TOTAL_LENGTH_MAX);
        vfl = (vfl & 0xFF000000) | (len & 0x00FFFFFF);
    }

    MESSAGE& FillHeader(struct MESSAGE& header)
    {
        std::memcpy(this, &header, sizeof(header));
        return *this;
    }

    void Reset()
    {
        Version(MESSAGE::VERSION);
        Flags(MESSAGE::FLAGS_BYTEORDER);
    }
};

template <typename T>
inline T* PayloadOf(MESSAGE* msg)
{
    return (T*)(msg->payload);
}


struct WeConnAuth {
    uint8_t tl;       //  type & length
    char padding[3];  //  name of the command

    uint8_t Type() const
    {
        return (tl >> 5);
    }

    uint8_t CmdNameLen() const
    {
        return (tl & 0x1F);
    }
};


struct Interface {
    virtual ~Interface()
    {
    }
};

#define DEFINE_INTERFACE(name) struct name : public Interface


DEFINE_INTERFACE(Dispatch)
{
    virtual void HandleMessage(MESSAGE * msg) = 0;
};


DEFINE_INTERFACE(MessageAllocator)
{
    virtual MESSAGE* Alloc(int32_t size) = 0;
    virtual void Free(MESSAGE * msg) = 0;
};

class MessageAllocatorDefault : public MessageAllocator
{
public:
    virtual MESSAGE* Alloc(int32_t payloadSize)
    {
        int32_t cap = sizeof(MESSAGE) + payloadSize;

        BUFFER* buf = (BUFFER*)malloc(sizeof(BUFFER) + cap);
        Q_ASSERT(nullptr != buf);
        buf->cap = cap;

        MESSAGE* msg = MessageOf(buf);
        msg->Reset();

        return msg;
    }

    virtual void Free(MESSAGE* msg)
    {
        Q_ASSERT(nullptr != msg);
        BUFFER* buf = BufferOf(msg);
        free(buf);
    }
};


#endif  // WEMESSAGE_H
