#ifndef WEMESSAGE_H
#define WEMESSAGE_H

#include <inttypes.h>

#include <cstdlib>
#include <cstring>


#ifndef Q_ASSERT
#include "assert.h"
#define Q_ASSERT(expr) assert(expr)
#endif  // ASSERT


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
        insert(buf, prev, (NODE*)this);
    }

    inline NODE* pop_front()
    {
        if (empty()) {
            return nullptr;
        }

        NODE* front = next;
        remove(front->prev, front->next);
        return front;
    }

    static inline void insert(NODE* n, NODE* prev, NODE* next)
    {
        n->next = next;
        n->prev = prev;
        prev->next = n;
        next->prev = n;
    }

    static inline void remove(NODE* prev, NODE* next)
    {
        prev->next = next;
        next->prev = prev;
    }
};

struct BUFFER : public NODE {
    int32_t cap;
    uint16_t source;
    uint16_t target;
};
static_assert((sizeof(BUFFER) % sizeof(void*) == 0), "make size align");


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
    uint32_t vtfl;       //  version(2),type(2),flags(4),length(24)
    uint32_t session;    //  session id
    uint8_t payload[0];  //  payload header

    enum : uint32_t {
        VTFL_VERSION_MASK = 0xC0000000,
        VTFL_TYPE_MASK = 0x30000000,
        VTFL_FLAGS_MASK = 0x0F000000,
    };

    enum : uint8_t {
        VERSION = 0x00,
    };

    enum : uint8_t {
        FLAGS_BYTEORDER = 0,  //  Little-Endian
    };

    enum : uint16_t {
        ADDRESS_INVALID = 0xFFFF,
    };

    enum : uint32_t {
        TOTAL_LENGTH_DEF = 64,
        TOTAL_LENGTH_MAX = 0x00FFFFFF,
    };

    enum : uint8_t {
        TYPE_USER = 0x00,
        TYPE_CONN = 0x01,
    };

    inline uint8_t Version() const
    {
        return ((vtfl & VTFL_VERSION_MASK) >> 30);
    }

    inline void Version(uint8_t ver)
    {
        vtfl = (vtfl & ~VTFL_VERSION_MASK) | ((uint32_t(ver) << 30) & VTFL_VERSION_MASK);
    }

    inline uint8_t Type() const
    {
        return ((vtfl & VTFL_TYPE_MASK) >> 28);
    }

    inline void Type(uint8_t type)
    {
        vtfl = (vtfl & ~VTFL_TYPE_MASK) | ((uint32_t(type) << 28) & VTFL_TYPE_MASK);
    }

    inline uint8_t Flags() const
    {
        return ((vtfl & VTFL_FLAGS_MASK) >> 24);
    }

    inline void Flags(uint8_t flags)
    {
        vtfl = (vtfl & ~VTFL_FLAGS_MASK) | ((uint32_t(flags) << 24) & VTFL_FLAGS_MASK);
    }

    inline int32_t TotalLength() const
    {
        return (vtfl & 0x00FFFFFF);
    }

    inline void TotalLength(uint32_t len)
    {
        Q_ASSERT(len < MESSAGE::TOTAL_LENGTH_MAX);
        vtfl = (vtfl & 0xFF000000) | (len & 0x00FFFFFF);
    }

    inline int32_t PayloadLength() const
    {
        return TotalLength() - sizeof(MESSAGE);
    }

    inline void PayloadLength(uint32_t len)
    {
        TotalLength(len + sizeof(MESSAGE));
    }

    MESSAGE& FillHeader(const MESSAGE& header)
    {
        std::memcpy(this, &header, sizeof(header));
        return *this;
    }

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

    inline uint16_t Target() const
    {
        const BUFFER* buf = BufferOf(this);
        return buf->target;
    }

    inline void Target(uint16_t s)
    {
        BUFFER* buf = BufferOf(this);
        buf->target = s;
    }

    inline int32_t Cap() const
    {
        const BUFFER* buf = BufferOf(this);
        return buf->cap;
    }

    void Reset()
    {
        Version(MESSAGE::VERSION);
        Flags(MESSAGE::FLAGS_BYTEORDER);
    }
};

template <typename T>
inline T PayloadOf(MESSAGE* msg)
{
    return (T)(msg->payload);
}


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
