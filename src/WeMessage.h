#ifndef WEMESSAGE_H
#define WEMESSAGE_H

#include <inttypes.h>

#include <cstdlib>
#include <cstring>

struct MESSAGE;
struct WeMessage;
struct WePayload;

struct MESSAGE {
    MESSAGE* next;
    MESSAGE* prev;
    int32_t ref;
    int32_t style;
    uint8_t entry[0];

    operator WeMessage*()
    {
        return (WeMessage*)(entry);
    }
};

struct WeMessage {
    uint32_t vfl;        //  version(3),flags(5),length(24)
    uint16_t suorce;     //  source address
    uint16_t target;     //  target address
    uint32_t session;    //  session id
    uint8_t payload[0];  //  payload header
    inline uint8_t Version() const
    {
        return (vfl >> 29);
    }
    inline uint8_t Flags() const
    {
        return ((vfl & 0x1F000000) >> 24);
    }
    inline uint8_t Length() const
    {
        return (vfl & 0x00FFFFFF);
    }
    WeMessage& Header(struct WeMessage& header)
    {
        std::memcpy(this, &header, sizeof(header));
        return *this;
    }

    inline int PayloadLength() const
    {
        return Length() - sizeof(WeMessage);
    }

    WePayload* Payload()
    {
        return (WePayload*)(payload);
    }

    operator MESSAGE*()
    {
        return (MESSAGE*)((uint8_t*)(this) - sizeof(MESSAGE));
    }
};
static_assert(sizeof(WeMessage) == 12, "make sure the structure is packed");


struct WePayload {
    uint8_t tl;    //  type(3), length(5)
    char name[3];  //  name of the command
};

struct WeCmdAuth {
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
    virtual void Handle(MESSAGE * msg) = 0;
};


#endif  // WEMESSAGE_H
