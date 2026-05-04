// Caio Eloi
// protocol.h — Definição do protocolo UDP para o jogo de senha (Mastermind)

#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <cstring>
#include <arpa/inet.h>

// Tipos de mensagem
#define MSG_HEL 1
#define MSG_TRY 2
#define MSG_RES 3
#define MSG_BYE 4
#define MSG_ERR 5

// Tamanho máximo da senha
#define MAX_DIGITS 8

// Tamanho fixo das mensagens
#define MSG_SIZE_SHORT 4   // HEL, BYE, ERR
#define MSG_SIZE_FULL  12  // TRY, RES

// Estrutura da mensagem (layout fixo em rede)
// TIPO(1) | CHECKSUM(1) | SEQNUM(2) | A1..A8(8)
struct Message {
    uint8_t  tipo;
    uint8_t  checksum;
    uint16_t seqnum;   // armazenado em ordem de rede (big-endian)
    uint8_t  a[8];
};

// Calcula checksum como XOR exclusivo de todos os bytes da mensagem
// O campo checksum é zerado antes do cálculo
inline uint8_t calc_checksum(const Message& msg, int size) {
    uint8_t result = 0;
    const uint8_t* raw = reinterpret_cast<const uint8_t*>(&msg);
    for (int i = 0; i < size; i++) {
        result ^= raw[i];
    }
    return result;
}

// Serializa a mensagem para um buffer de bytes (big-endian)
// Retorna o número de bytes escritos
inline int serialize(const Message& msg, uint8_t* buf, int size) {
    buf[0] = msg.tipo;
    buf[1] = msg.checksum;
    uint16_t seq_net = htons(msg.seqnum);
    memcpy(buf + 2, &seq_net, 2);
    if (size == MSG_SIZE_FULL) {
        memcpy(buf + 4, msg.a, 8);
    }
    return size;
}

// Desserializa um buffer de bytes para uma mensagem
inline void deserialize(const uint8_t* buf, int size, Message& msg) {
    memset(&msg, 0, sizeof(msg));
    msg.tipo     = buf[0];
    msg.checksum = buf[1];
    uint16_t seq_net;
    memcpy(&seq_net, buf + 2, 2);
    msg.seqnum = ntohs(seq_net);
    if (size == MSG_SIZE_FULL) {
        memcpy(msg.a, buf + 4, 8);
    }
}

// Verifica se o checksum da mensagem recebida é válido
inline bool verify_checksum(const uint8_t* buf, int size) {
    uint8_t received = buf[1];
    // Copia buffer e zera campo checksum para recalcular
    uint8_t tmp[MSG_SIZE_FULL];
    memcpy(tmp, buf, size);
    tmp[1] = 0;
    uint8_t expected = 0;
    for (int i = 0; i < size; i++) expected ^= tmp[i];
    return received == expected;
}

// Preenche e finaliza uma mensagem, calculando o checksum
inline void make_message(Message& msg, uint8_t tipo, uint16_t seqnum,
                          const uint8_t* digits, int na, int size) {
    memset(&msg, 0, sizeof(msg));
    msg.tipo    = tipo;
    msg.seqnum  = seqnum;
    msg.checksum = 0;
    if (digits && size == MSG_SIZE_FULL) {
        for (int i = 0; i < na && i < 8; i++) msg.a[i] = digits[i];
    }
    // Serializa para calcular checksum
    uint8_t buf[MSG_SIZE_FULL] = {0};
    buf[0] = msg.tipo;
    buf[1] = 0; // checksum = 0 durante cálculo
    uint16_t seq_net = htons(msg.seqnum);
    memcpy(buf + 2, &seq_net, 2);
    if (size == MSG_SIZE_FULL) memcpy(buf + 4, msg.a, 8);
    uint8_t cs = 0;
    for (int i = 0; i < size; i++) cs ^= buf[i];
    msg.checksum = cs;
}

#endif // PROTOCOL_H
