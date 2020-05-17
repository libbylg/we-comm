#include "SMQTransport.h"


struct WeHello {
    char str[10];
};

class WeProtocol
{
    // Dispatch interface
public:
    //    virtual void HandleEvent(void* stream, uint16_t event, uintptr_t param1, uintptr_t param2)
    //    {
    //    }
    virtual int32_t HandleMessage(void* stream, MESSAGE* msg)
    {
        WeHello* hello = PayloadOf<WeHello*>(msg);

        printf("WeDispatch::Handle: '%s'\n", hello->str);

        return ACTION_NONE;
    }
};

int main(int argc, char* argv[])
{
    if (argc < 2) {
        printf("we-comm client\n");
        printf("we-comm server\n");
        return 0;
    }

    WeProtocol dispatch;
    MessageAllocatorDefault allocator;
    auto comm = new SMQTransport<WeProtocol, MessageAllocatorDefault>();
    std::thread thread;
    uint16_t target = 0;

    //  启动服务端
    if (0 == strcmp(argv[1], "server")) {
        comm->Init(11, &dispatch, &allocator, 4096);
        comm->SetupAcceptor("localhost:9090");
        thread = std::thread([comm]() {
            printf("comm1 run\n");
            comm->Loop();
            printf("comm1 run end\n");
        });
        target = 22;
    }


    //  启动客户端
    if (0 == strcmp(argv[1], "client")) {
        comm->Init(22, &dispatch, &allocator, 4096);
        comm->SetupConnect("localhost:9090");
        thread = std::thread([comm]() {
            printf("comm2 run\n");
            comm->Loop();
            printf("comm2 run end\n");
        });
        target = 11;
    }


    int counter = 1;
    while (1) {
        printf("Enter to send test message...\n");
        getchar();

        MESSAGE* msg = allocator.Alloc(sizeof(WeHello));
        msg->Type(MESSAGE::TYPE_USER);
        WeHello* hello = PayloadOf<WeHello*>(msg);

        snprintf(hello->str, 6, "he%d", counter++);
        msg->PayloadLength(sizeof(WeHello));
        msg->Target(target);
        comm->Post(msg);
    }


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
