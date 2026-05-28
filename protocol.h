#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <cstring>
#include <arpa/inet.h>

#define MSG_HEL 1
#define MSG_TRY 2
#define MSG_RES 3
#define MSG_BYE 4
#define MSG_ERR 5

#define MAX_DIGITS 8
#define MSG_SIZE_SHORT 4
#define MSG_SIZE_FULL 12

struct Message {
    uint8_t tipo;
    uint8_t checksum;
    uint16_t seqnum;
    uint8_t a[8];
};

inline bool verify_checksum(const uint8_t *buf, int size) {
    uint8_t recebido = buf[1];
    uint8_t soma = 0;

    for (int i = 0; i < size; i++) {
        if (i == 1) soma ^= 0;
        else soma ^= buf[i];
    }

    return soma == recebido;
}

inline void serialize(const Message& msg, uint8_t *buf, int size) {
    buf[0] = msg.tipo;
    buf[1] = msg.checksum;

    uint16_t s = htons(msg.seqnum);
    memcpy(buf + 2, &s, 2);

    if (size == MSG_SIZE_FULL) {
        memcpy(buf + 4, msg.a, 8);
    }
}

inline void deserialize(const uint8_t *buf, int size, Message& msg) {
    memset(&msg, 0, sizeof(Message));

    msg.tipo = buf[0];
    msg.checksum = buf[1];

    uint16_t s;
    memcpy(&s, buf + 2, 2);
    msg.seqnum = ntohs(s);

    if (size == MSG_SIZE_FULL) {
        memcpy(msg.a, buf + 4, 8);
    }
}

inline void make_message(Message& msg, uint8_t tipo, uint16_t seq,
                         const uint8_t *dados, int qtd, int size) {
    memset(&msg, 0, sizeof(Message));

    msg.tipo = tipo;
    msg.seqnum = seq;

    if (dados != NULL && size == MSG_SIZE_FULL) {
        for (int i = 0; i < qtd && i < 8; i++) {
            msg.a[i] = dados[i];
        }
    }

    uint8_t buf[MSG_SIZE_FULL];
    memset(buf, 0, sizeof(buf));

    buf[0] = msg.tipo;
    buf[1] = 0;

    uint16_t s = htons(msg.seqnum);
    memcpy(buf + 2, &s, 2);

    if (size == MSG_SIZE_FULL) {
        memcpy(buf + 4, msg.a, 8);
    }

    uint8_t cs = 0;
    for (int i = 0; i < size; i++) {
        cs ^= buf[i];
    }

    msg.checksum = cs;
}

#endif
