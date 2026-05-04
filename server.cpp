// Caio Eloi
// server.cpp — Servidor UDP para o jogo de senha (Mastermind)
// Uso: ./server <porta> <senha> <NT>

#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "protocol.h"

// Estado de uma sessão de cliente
struct ClientSession {
    struct sockaddr_in addr;
    socklen_t addrlen;
    bool active;
    int tries_used;       // tentativas usadas (SEQNUM esperado do próximo TRY)
    uint16_t last_seqnum; // SEQNUM da última mensagem TRY recebida
    // Guarda a última resposta enviada para retransmissão
    uint8_t last_resp_buf[MSG_SIZE_FULL];
    int     last_resp_size;
    bool    has_last_resp;
};

// Compara dois endereços sockaddr_in
bool same_addr(const struct sockaddr_in& a, const struct sockaddr_in& b) {
    return a.sin_addr.s_addr == b.sin_addr.s_addr &&
           a.sin_port == b.sin_port;
}

// Gera uma senha aleatória de comprimento 'len' com dígitos 0-9 sem repetição
std::string random_password(int len) {
    int digits[10] = {0,1,2,3,4,5,6,7,8,9};
    // Fisher-Yates shuffle
    for (int i = 9; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = digits[i]; digits[i] = digits[j]; digits[j] = tmp;
    }
    std::string s;
    for (int i = 0; i < len; i++) s += ('0' + digits[i]);
    return s;
}

// Valida a senha: 4-8 dígitos, sem repetição, apenas 0-9
bool valid_password(const std::string& pw) {
    int n = pw.size();
    if (n < 4 || n > 8) return false;
    bool seen[10] = {false};
    for (char c : pw) {
        if (c < '0' || c > '9') return false;
        int d = c - '0';
        if (seen[d]) return false;
        seen[d] = true;
    }
    return true;
}

// Avalia uma tentativa contra a senha secreta
// Preenche resp[i] com '*', '+', ou '-'
void evaluate(const std::string& secret, const uint8_t* guess, int na,
              uint8_t* resp) {
    bool used_secret[8] = {false};
    bool used_guess[8]  = {false};

    // Primeira passagem: posição certa
    for (int i = 0; i < na; i++) {
        if (guess[i] == (uint8_t)(secret[i] - '0')) {
            resp[i] = '*';
            used_secret[i] = true;
            used_guess[i]  = true;
        }
    }
    // Segunda passagem: dígito presente mas em posição errada
    for (int i = 0; i < na; i++) {
        if (used_guess[i]) continue;
        for (int j = 0; j < na; j++) {
            if (used_secret[j]) continue;
            if (guess[i] == (uint8_t)(secret[j] - '0')) {
                resp[i] = '+';
                used_secret[j] = true;
                used_guess[i]  = true;
                break;
            }
        }
        if (!used_guess[i]) resp[i] = '-';
    }
}

// Envia mensagem para um endereço específico
void send_msg(int sockfd, const uint8_t* buf, int size,
              const struct sockaddr_in& addr, socklen_t addrlen) {
    sendto(sockfd, buf, size, 0,
           (struct sockaddr*)&addr, addrlen);
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <porta> <senha> <NT>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    std::string password_arg = argv[2];
    int NT = atoi(argv[3]);

    if (NT < 1) {
        fprintf(stderr, "NT deve ser >= 1\n");
        return 1;
    }

    // Determina a senha real
    std::string secret;
    int na; // tamanho da senha (TS)

    // Verifica se é sequência de zeros (senha aleatória)
    bool all_zeros = true;
    int zeros_len = password_arg.size();
    if (zeros_len >= 4 && zeros_len <= 8) {
        for (char c : password_arg) {
            if (c != '0') { all_zeros = false; break; }
        }
    } else {
        all_zeros = false;
    }

    if (all_zeros) {
        srand((unsigned)time(NULL));
        secret = random_password(zeros_len);
        na = zeros_len;
    } else {
        if (!valid_password(password_arg)) {
            fprintf(stderr, "Senha inválida: %s\n", password_arg.c_str());
            return 1;
        }
        secret = password_arg;
        na = secret.size();
    }

    // Cria socket UDP
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in srv_addr;
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family      = AF_INET;
    srv_addr.sin_addr.s_addr = INADDR_ANY;
    srv_addr.sin_port        = htons(port);

    if (bind(sockfd, (struct sockaddr*)&srv_addr, sizeof(srv_addr)) < 0) {
        perror("bind");
        close(sockfd);
        return 1;
    }

    // Serve até dois clientes sequencialmente ou de forma intercalada
    int clients_served = 0;
    const int MAX_CLIENTS = 2;

    // Sessões dos dois possíveis clientes simultâneos
    ClientSession sessions[2];
    memset(sessions, 0, sizeof(sessions));
    int num_sessions = 0;

    while (clients_served < MAX_CLIENTS) {
        uint8_t buf[MSG_SIZE_FULL];
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);

        int n = recvfrom(sockfd, buf, sizeof(buf), 0,
                         (struct sockaddr*)&cli_addr, &cli_len);
        if (n < MSG_SIZE_SHORT) continue; // pacote muito pequeno

        // Determina o tamanho esperado com base no tipo
        uint8_t tipo = buf[0];
        // RES recebido de cliente não faz sentido — ignora
        if (tipo == MSG_RES) continue;

        // Verifica checksum
        if (!verify_checksum(buf, (n >= MSG_SIZE_FULL ? MSG_SIZE_FULL : MSG_SIZE_SHORT))) {
            // Pacote corrompido — ignora silenciosamente
            continue;
        }

        // Verifica se é um cliente já em sessão
        int sess_idx = -1;
        for (int i = 0; i < num_sessions; i++) {
            if (sessions[i].active &&
                same_addr(sessions[i].addr, cli_addr)) {
                sess_idx = i;
                break;
            }
        }

        // Deserializa
        Message msg;
        deserialize(buf, (n >= MSG_SIZE_FULL ? MSG_SIZE_FULL : MSG_SIZE_SHORT), msg);

        if (tipo == MSG_HEL) {
            // Novo cliente ou retransmissão de HEL
            if (sess_idx >= 0) {
                // Retransmissão de HEL: reenviar última resposta
                if (sessions[sess_idx].has_last_resp) {
                    send_msg(sockfd, sessions[sess_idx].last_resp_buf,
                             sessions[sess_idx].last_resp_size,
                             cli_addr, cli_len);
                }
                continue;
            }

            // Novo cliente — inicia sessão
            if (num_sessions >= 2) {
                // Ignora terceiro cliente
                continue;
            }

            int si = num_sessions++;
            memset(&sessions[si], 0, sizeof(ClientSession));
            sessions[si].active   = true;
            sessions[si].addr     = cli_addr;
            sessions[si].addrlen  = cli_len;
            sessions[si].tries_used = 0;
            sessions[si].last_seqnum = 0;
            sessions[si].has_last_resp = false;

            // Monta RES de resposta ao HEL:
            // SEQNUM = NT, padrão com NA sinais '?' (valor 10 para indicar '?')
            // Conforme especificação: NA sinais de interrogação nas posições da senha
            uint8_t resp_digits[8];
            memset(resp_digits, '?', 8); // '?' = 0x3F
            // Usamos '?' = 63 para codificar o '?' (interrogação)
            for (int i = 0; i < 8; i++) resp_digits[i] = '?';

            Message res_msg;
            make_message(res_msg, MSG_RES, (uint16_t)NT, resp_digits, na, MSG_SIZE_FULL);

            uint8_t res_buf[MSG_SIZE_FULL];
            serialize(res_msg, res_buf, MSG_SIZE_FULL);
            send_msg(sockfd, res_buf, MSG_SIZE_FULL, cli_addr, cli_len);

            // Guarda última resposta
            memcpy(sessions[si].last_resp_buf, res_buf, MSG_SIZE_FULL);
            sessions[si].last_resp_size = MSG_SIZE_FULL;
            sessions[si].has_last_resp = true;

        } else if (tipo == MSG_TRY) {
            if (sess_idx < 0) {
                // TRY sem HEL — ERR com SEQNUM=0
                Message err_msg;
                make_message(err_msg, MSG_ERR, 0, nullptr, 0, MSG_SIZE_SHORT);
                uint8_t err_buf[MSG_SIZE_SHORT];
                serialize(err_msg, err_buf, MSG_SIZE_SHORT);
                send_msg(sockfd, err_buf, MSG_SIZE_SHORT, cli_addr, cli_len);
                continue;
            }

            ClientSession& sess = sessions[sess_idx];
            uint16_t seqnum = msg.seqnum;
            int expected_seq = sess.tries_used + 1;

            // Verifica se é retransmissão (mesmo SEQNUM da última TRY)
            if (sess.has_last_resp && seqnum == sess.last_seqnum) {
                send_msg(sockfd, sess.last_resp_buf, sess.last_resp_size,
                         cli_addr, cli_len);
                continue;
            }

            // Verifica SEQNUM esperado
            if ((int)seqnum != expected_seq) {
                // SEQNUM fora da ordem esperada
                Message err_msg;
                make_message(err_msg, MSG_ERR, 0, nullptr, 0, MSG_SIZE_SHORT);
                uint8_t err_buf[MSG_SIZE_SHORT];
                serialize(err_msg, err_buf, MSG_SIZE_SHORT);
                send_msg(sockfd, err_buf, MSG_SIZE_SHORT, cli_addr, cli_len);
                // Armazena como última resposta
                memcpy(sess.last_resp_buf, err_buf, MSG_SIZE_SHORT);
                sess.last_resp_size = MSG_SIZE_SHORT;
                sess.has_last_resp = true;
                sess.last_seqnum = seqnum;
                continue;
            }

            // Verifica número de tentativas
            if (sess.tries_used >= NT) {
                Message err_msg;
                make_message(err_msg, MSG_ERR, 0, nullptr, 0, MSG_SIZE_SHORT);
                uint8_t err_buf[MSG_SIZE_SHORT];
                serialize(err_msg, err_buf, MSG_SIZE_SHORT);
                send_msg(sockfd, err_buf, MSG_SIZE_SHORT, cli_addr, cli_len);
                continue;
            }

            // Valida a senha proposta (tamanho correto e dígitos sem repetição)
            bool proposal_valid = true;
            bool seen[10] = {false};
            for (int i = 0; i < na; i++) {
                uint8_t d = msg.a[i];
                if (d > 9) { proposal_valid = false; break; }
                if (seen[d]) { proposal_valid = false; break; }
                seen[d] = true;
            }
            // Verifica se bytes além de na são zero (ignorados mas devem ser OK)
            // Não há restrição explícita, ignora

            if (!proposal_valid) {
                // Senha inválida: ERR com SEQNUM > 0 (usa seqnum recebido)
                Message err_msg;
                make_message(err_msg, MSG_ERR, seqnum, nullptr, 0, MSG_SIZE_SHORT);
                uint8_t err_buf[MSG_SIZE_SHORT];
                serialize(err_msg, err_buf, MSG_SIZE_SHORT);
                send_msg(sockfd, err_buf, MSG_SIZE_SHORT, cli_addr, cli_len);
                // Guarda para possível retransmissão
                memcpy(sess.last_resp_buf, err_buf, MSG_SIZE_SHORT);
                sess.last_resp_size = MSG_SIZE_SHORT;
                sess.has_last_resp = true;
                sess.last_seqnum = seqnum;
                continue;
            }

            // Avalia tentativa
            uint8_t resp_chars[8];
            memset(resp_chars, '-', 8);
            evaluate(secret, msg.a, na, resp_chars);

            sess.tries_used++;
            int remaining = NT - sess.tries_used;

            // Monta RES: SEQNUM = NT - tries_used (tentativas restantes)
            // Conforme spec: SEQNUM da RES = NT - SEQNUM_TRY recebido
            // Reinterpretando: "SEQNUM da mensagem TRY que está sendo respondida
            // (isto é, representa o número de tentativas restantes)"
            // => SEQNUM_RES = NT - seqnum_TRY = remaining
            Message res_msg;
            make_message(res_msg, MSG_RES, (uint16_t)remaining, resp_chars, na, MSG_SIZE_FULL);

            uint8_t res_buf[MSG_SIZE_FULL];
            serialize(res_msg, res_buf, MSG_SIZE_FULL);
            send_msg(sockfd, res_buf, MSG_SIZE_FULL, cli_addr, cli_len);

            memcpy(sess.last_resp_buf, res_buf, MSG_SIZE_FULL);
            sess.last_resp_size = MSG_SIZE_FULL;
            sess.has_last_resp = true;
            sess.last_seqnum = seqnum;

        } else if (tipo == MSG_BYE) {
            if (sess_idx < 0) {
                // BYE sem sessão ativa — ERR
                Message err_msg;
                make_message(err_msg, MSG_ERR, 0, nullptr, 0, MSG_SIZE_SHORT);
                uint8_t err_buf[MSG_SIZE_SHORT];
                serialize(err_msg, err_buf, MSG_SIZE_SHORT);
                send_msg(sockfd, err_buf, MSG_SIZE_SHORT, cli_addr, cli_len);
                continue;
            }

            ClientSession& sess = sessions[sess_idx];

            // Verifica SEQNUM do BYE: deve ser igual ao SEQNUM da última TRY
            // "deve ter NUMSEQ igual ao valor de NUMSEQ da última mensagem TRY enviada"
            // Retransmissão de BYE: reenviar última resposta
            // Se já respondemos ao BYE, reenviar
            // Monta RES final com a senha real: SEQNUM = -1 (65535 em uint16)
            uint8_t secret_digits[8];
            memset(secret_digits, 0, 8);
            for (int i = 0; i < na; i++) secret_digits[i] = secret[i] - '0';

            Message res_msg;
            make_message(res_msg, MSG_RES, (uint16_t)0xFFFF, secret_digits, na, MSG_SIZE_FULL);

            uint8_t res_buf[MSG_SIZE_FULL];
            serialize(res_msg, res_buf, MSG_SIZE_FULL);
            send_msg(sockfd, res_buf, MSG_SIZE_FULL, cli_addr, cli_len);

            // Encerra sessão
            sess.active = false;
            clients_served++;

        } else if (tipo == MSG_ERR) {
            // Ignora ERR recebido de cliente
            continue;
        }
    }

    close(sockfd);
    return 0;
}
