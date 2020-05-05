#include "WeComm.h"

class WeDispatch
{
    // Dispatch interface
public:
    void Handle(MESSAGE* msg)
    {
        printf("WeDispatch::Handle\n");
    }
};

class Allocator
{
public:
    WeMessage* Alloc(int size)
    {
        return (WeMessage*)((MESSAGE*)std::malloc(sizeof(MESSAGE) + sizeof(size)));
    }

    void Free(WeMessage* msg)
    {
        MESSAGE* m = (MESSAGE*)msg;
        return std::free(m);
    }
};

int main(int argc, char* argv[])
{
    asio::io_context context;
    WeDispatch dispatch;
    Allocator allocator;
    WeComm<asio::io_context, WeDispatch, Allocator> comm(context, dispatch, allocator);
    comm.Setup(TYPE_SERVER, 1, "12");
    comm.Setup(TYPE_CLIENT, 0, "12");
    context.run();
    return 0;
}
