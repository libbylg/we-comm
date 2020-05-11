#ifndef ARCHIVE_H
#define ARCHIVE_H

#include <cstdio>

#include "MESSAGE.h"

template <typename AR, typename T>
class serializer
{
public:
    void load(AR& ar, T& val)
    {
        std::printf("serializer::load\n");
    }
    void save(AR& ar, const T& val)
    {
        std::printf("serializer::save\n");
    }
};

class message_oarchive
{
public:
    static constexpr bool is_load = false;
    static constexpr bool is_save = true;
    message_oarchive(MESSAGE* message)
    {
    }
};

class message_iarchive
{
    static constexpr bool is_load = true;
    static constexpr bool is_save = false;
};

template <typename AR, typename T>
void serialize(AR&, T& val)
{
    std::printf("serialize(T& val)\n");
};


////////////////////////////////////////////////////////////////////////////////////


struct Test {
    int a;
    int b;
};

template <typename AR>
class serializer<AR, Test>
{
public:
    void invoke(AR& ar, Test& val)
    {
        std::printf("serializer<AR, Test&>\n");
    }
};

#endif  // ARCHIVE_H
