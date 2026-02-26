#pragma once
#include <stdint.h>

#pragma pack(push, 1)

typedef struct {
    uint8_t version;
    uint8_t msg_type;
    uint16_t flags;
    uint32_t length;
} MessageHeader;

typedef struct {
    char sender_name[64];
    char body[1024];
} MessageBody;

typedef struct {
    MessageHeader header;
    MessageBody body;
} MessageHeaderAndBody;

typedef struct {
    MessageHeaderAndBody *data; // heap-allocated array
    int length;                 // current number of elements
    int capacity;
} MessageHistory;

#pragma pack(pop)

// Shared enum for message types
enum MessageType {
    MSG_HELLO = 0,
    MSG_SET_NAME = 1,
    MSG_CHAT = 2,
    MSG_PING = 3,
    MSG_USER_JOINED = 4,
    MSG_USER_DISCONNECTED = 5,
    MSG_ASK_FOR_NAME = 6,
    MSG_DISCONNECT = 99
};
