#pragma once
#include <stdint.h>

#pragma pack(push, 1)
typedef struct {
    uint8_t version;
    uint8_t msg_type;
    uint16_t flags;
    uint32_t length;
} MessageHeader;
#pragma pack(pop)

// Shared enum for message types
enum MessageType {
    MSG_HELLO = 0,
    MSG_SET_NAME = 1,
    MSG_CHAT = 2,
    MSG_PING = 3
};
