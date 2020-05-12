#include "SMQTransport.h"


struct WeHello {
    char str[10];
};

class WeProtocol
{
    // Dispatch interface
public:
    virtual void HandleEvent(void* stream, uint16_t event, uintptr_t param1, uintptr_t param2)
    {
    }
    virtual void HandleMessage(void* stream, MESSAGE* msg)
    {
        WeHello* hello = PayloadOf<WeHello*>(msg);

        printf("WeDispatch::Handle: '%s'\n", hello->str);
    }
};

int main(int argc, char* argv[])
{
    // asio::io_context context;
    WeProtocol dispatch;
    MessageAllocatorDefault allocator;


    //  启动服务端
    SMQTransport<WeProtocol, MessageAllocatorDefault> comm1;
    comm1.Init(11, &dispatch, &allocator, 4096);
    comm1.SetupAcceptor("12");
    std::thread t1([&comm1]() {
        printf("comm1 run\n");
        comm1.Loop();
        printf("comm1 run end\n");
    });


    //  启动客户端
    SMQTransport<WeProtocol, MessageAllocatorDefault> comm2;
    comm2.Init(22, &dispatch, &allocator, 4096);
    comm2.SetupConnect("12");
    std::thread t2([&comm2]() {
        printf("comm2 run\n");
        comm2.Loop();
        printf("comm2 run end\n");
    });


    printf("Enter to send test message...\n");
    getchar();


    MESSAGE* msg = allocator.Alloc(sizeof(WeHello));
    msg->Type(MESSAGE::TYPE_USER);
    WeHello* hello = PayloadOf<WeHello*>(msg);
    memcpy(hello->str, "hello", 6);
    msg->PayloadLength(sizeof(WeHello));
    msg->Target(22);

    comm1.Post(msg);

    printf("Enter to exit...\n");
    getchar();
    return 0;
}

//#include "Archive.h"
//#include "boost/archive/binary_iarchive.hpp"
//#include "boost/archive/binary_oarchive.hpp"
//
// int main(int argc, char* argv[])
//{
//    Test t{11, 22};
//    message_oarchive oar;
//
//    oar& t;
//
//    return 0;
//}
