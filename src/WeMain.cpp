#include "SMQTransport.h"

class WeDispatch
{
    // Dispatch interface
public:
    void HandleMessage(MESSAGE* msg)
    {
        printf("WeDispatch::Handle\n");
    }
};

struct WeHello {
    char str[10];
};

int main(int argc, char* argv[])
{
    asio::io_context context;
    WeDispatch dispatch;
    MessageAllocatorDefault allocator;
    SMQTransport<asio::io_context, WeDispatch, MessageAllocatorDefault> comm(context, 4096);
    comm.Init(&dispatch, &allocator);

    //
    comm.SetupAcceptor("12");
    comm.SetupConnect("12");

    std::thread t([&context]() { context.run(); });

    MESSAGE* msg = allocator.Alloc(sizeof(WeHello));
    WeHello* hello = PayloadOf<WeHello>(msg);
    memcpy(hello->str, "hello", 6);
    msg->Length(sizeof(WeHello));

    comm.Post(msg);
    getchar();
    return 0;
}
