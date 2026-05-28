//Caio Eloi Campos e Kaique de Oliveira e Silva

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstring>
#include <arpa/inet.h>

#define MSG_HEL 1
#define MSG_TRY 2
#define MSG_RES 3
#define MSG_BYE 4
#define MSG_ERR 5

#define MAX_DIGITS 8

// mensagens pequenas: HEL, BYE e ERR
#define MSG_SIZE_SHORT 4

// mensagens com campo A: TRY e RES
#define MSG_SIZE_FULL 12

struct Message {
    unsigned char tipo;
    unsigned char checksum;
    unsigned short seqnum;
    unsigned char a[8];
};

// confere se o checksum bate
inline bool verify_checksum(const unsigned char *buf, int size) {
    unsigned char recebido = buf[1];
    unsigned char calc = 0;

    for (int i = 0; i < size; i++) {
        if (i == 1) {
            calc ^= 0; // checksum entra como zero na conta
        } else {
            calc ^= buf[i];
        }
    }

    return calc == recebido;
}

// coloca a struct no buffer, usando big endian no seq
inline void serialize(const Message& msg, unsigned char *buf, int size) {
    buf[0] = msg.tipo;
    buf[1] = msg.checksum;

    unsigned short seq_rede = htons(msg.seqnum);
    memcpy(buf + 2, &seq_rede, 2);

    if (size == MSG_SIZE_FULL) {
        memcpy(buf + 4, msg.a, 8);
    }
}

// pega os bytes recebidos e joga pra struct
inline void deserialize(const unsigned char *buf, int size, Message& msg) {
    memset(&msg, 0, sizeof(Message));

    msg.tipo = buf[0];
    msg.checksum = buf[1];

    unsigned short seq_rede;
    memcpy(&seq_rede, buf + 2, 2);
    msg.seqnum = ntohs(seq_rede);

    if (size == MSG_SIZE_FULL) {
        memcpy(msg.a, buf + 4, 8);
    }
}

// monta msg e calcula checksum no final
inline void make_message(Message& msg, unsigned char tipo, unsigned short seq,
                         const unsigned char *dados, int qtd, int size) {
    memset(&msg, 0, sizeof(Message));

    msg.tipo = tipo;
    msg.seqnum = seq;

    if (dados != NULL && size == MSG_SIZE_FULL) {
        for (int i = 0; i < qtd && i < 8; i++) {
            msg.a[i] = dados[i];
        }
    }

    unsigned char buf[MSG_SIZE_FULL];
    memset(buf, 0, sizeof(buf));

    buf[0] = msg.tipo;
    buf[1] = 0;

    unsigned short seq_rede = htons(msg.seqnum);
    memcpy(buf + 2, &seq_rede, 2);

    if (size == MSG_SIZE_FULL) {
        memcpy(buf + 4, msg.a, 8);
    }

    unsigned char cs = 0;
    for (int i = 0; i < size; i++) {
        cs ^= buf[i];
    }

    msg.checksum = cs;
}

#endif
