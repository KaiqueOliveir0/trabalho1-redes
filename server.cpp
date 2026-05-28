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

struct ClientSession {
    struct sockaddr_in addr;
    socklen_t addrlen;
    bool active;
    bool finished;
    int tries_used;
    uint16_t last_seqnum;

    uint8_t last_resp_buf[MSG_SIZE_FULL];
    int last_resp_size;
    bool has_last_resp;
};

bool same_addr(const struct sockaddr_in& a, const struct sockaddr_in& b) {
    return a.sin_addr.s_addr == b.sin_addr.s_addr &&
           a.sin_port == b.sin_port;
}

std::string random_password(int len) {
    int digits[10] = {0,1,2,3,4,5,6,7,8,9};

    for (int i = 9; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = digits[i];
        digits[i] = digits[j];
        digits[j] = tmp;
    }

    std::string s;
    for (int i = 0; i < len; i++) {
        s += ('0' + digits[i]);
    }

    return s;
}

bool valid_password(const std::string& pw) {
    int n = pw.size();

    if (n < 4 || n > 8) {
        return false;
    }

    bool seen[10] = {false};

    for (char c : pw) {
        if (c < '0' || c > '9') {
            return false;
        }

        int d = c - '0';

        if (seen[d]) {
            return false;
        }

        seen[d] = true;
    }

    return true;
}

void evaluate(const std::string& secret, const uint8_t* guess, int na,
              uint8_t* resp) {
    bool used_secret[8] = {false};
    bool used_guess[8]  = {false};

    for (int i = 0; i < na; i++) {
        if (guess[i] == (uint8_t)(secret[i] - '0')) {
            resp[i] = '*';
            used_secret[i] = true;
            used_guess[i]  = true;
        }
    }

    for (int i = 0; i < na; i++) {
        if (used_guess[i]) {
            continue;
        }

        for (int j = 0; j < na; j++) {
            if (used_secret[j]) {
                continue;
            }

            if (guess[i] == (uint8_t)(secret[j] - '0')) {
                resp[i] = '+';
                used_secret[j] = true;
                used_guess[i]  = true;
                break;
            }
        }

        if (!used_guess[i]) {
            resp[i] = '-';
        }
    }
}

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

    std::string secret;
    int na;

    bool all_zeros = true;
    int zeros_len = password_arg.size();

    if (zeros_len >= 4 && zeros_len <= 8) {
        for (char c : password_arg) {
            if (c != '0') {
                all_zeros = false;
                break;
            }
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

    int clients_served = 0;
    const int MAX_CLIENTS = 2;

    ClientSession sessions[2];
    memset(sessions, 0, sizeof(sessions));

    int num_sessions = 0;

    while (clients_served < MAX_CLIENTS) {
        uint8_t buf[MSG_SIZE_FULL];

        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);

        int n = recvfrom(sockfd, buf, sizeof(buf), 0,
                         (struct sockaddr*)&cli_addr, &cli_len);

        if (n < MSG_SIZE_SHORT) {
            continue;
        }

        uint8_t tipo = buf[0];

        if (tipo == MSG_RES) {
            continue;
        }

        int msg_size = (n >= MSG_SIZE_FULL) ? MSG_SIZE_FULL : MSG_SIZE_SHORT;

        if (!verify_checksum(buf, msg_size)) {
            continue;
        }

        int sess_idx = -1;

        for (int i = 0; i < num_sessions; i++) {
            if (sessions[i].active &&
                same_addr(sessions[i].addr, cli_addr)) {
                sess_idx = i;
                break;
            }
        }

        Message msg;
        deserialize(buf, msg_size, msg);

        if (tipo == MSG_HEL) {
            if (sess_idx >= 0) {
                if (sessions[sess_idx].has_last_resp) {
                    send_msg(sockfd,
                             sessions[sess_idx].last_resp_buf,
                             sessions[sess_idx].last_resp_size,
                             cli_addr,
                             cli_len);
                }

                continue;
            }

            if (num_sessions >= 2) {
                continue;
            }

            int si = num_sessions++;

            memset(&sessions[si], 0, sizeof(ClientSession));

            sessions[si].active = true;
            sessions[si].finished = false;
            sessions[si].addr = cli_addr;
            sessions[si].addrlen = cli_len;
            sessions[si].tries_used = 0;
            sessions[si].last_seqnum = 0;
            sessions[si].has_last_resp = false;

            uint8_t resp_digits[8];
            memset(resp_digits, '?', 8);

            Message res_msg;
            make_message(res_msg, MSG_RES, (uint16_t)NT,
                         resp_digits, na, MSG_SIZE_FULL);

            uint8_t res_buf[MSG_SIZE_FULL];
            serialize(res_msg, res_buf, MSG_SIZE_FULL);

            send_msg(sockfd, res_buf, MSG_SIZE_FULL, cli_addr, cli_len);

            memcpy(sessions[si].last_resp_buf, res_buf, MSG_SIZE_FULL);
            sessions[si].last_resp_size = MSG_SIZE_FULL;
            sessions[si].has_last_resp = true;

        } else if (tipo == MSG_TRY) {
            if (sess_idx < 0) {
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

            if (sess.has_last_resp && seqnum == sess.last_seqnum) {
                send_msg(sockfd,
                         sess.last_resp_buf,
                         sess.last_resp_size,
                         cli_addr,
                         cli_len);
                continue;
            }

            if ((int)seqnum != expected_seq) {
                Message err_msg;
                make_message(err_msg, MSG_ERR, 0, nullptr, 0, MSG_SIZE_SHORT);

                uint8_t err_buf[MSG_SIZE_SHORT];
                serialize(err_msg, err_buf, MSG_SIZE_SHORT);

                send_msg(sockfd, err_buf, MSG_SIZE_SHORT, cli_addr, cli_len);

                memcpy(sess.last_resp_buf, err_buf, MSG_SIZE_SHORT);
                sess.last_resp_size = MSG_SIZE_SHORT;
                sess.has_last_resp = true;
                sess.last_seqnum = seqnum;

                continue;
            }

            if (sess.tries_used >= NT) {
                Message err_msg;
                make_message(err_msg, MSG_ERR, 0, nullptr, 0, MSG_SIZE_SHORT);

                uint8_t err_buf[MSG_SIZE_SHORT];
                serialize(err_msg, err_buf, MSG_SIZE_SHORT);

                send_msg(sockfd, err_buf, MSG_SIZE_SHORT, cli_addr, cli_len);
                continue;
            }

            bool proposal_valid = true;
            bool seen[10] = {false};

            for (int i = 0; i < na; i++) {
                uint8_t d = msg.a[i];

                if (d > 9) {
                    proposal_valid = false;
                    break;
                }

                if (seen[d]) {
                    proposal_valid = false;
                    break;
                }

                seen[d] = true;
            }

            if (!proposal_valid) {
                sess.tries_used++;

                Message err_msg;
                make_message(err_msg, MSG_ERR, seqnum, nullptr, 0, MSG_SIZE_SHORT);

                uint8_t err_buf[MSG_SIZE_SHORT];
                serialize(err_msg, err_buf, MSG_SIZE_SHORT);

                send_msg(sockfd, err_buf, MSG_SIZE_SHORT, cli_addr, cli_len);

                memcpy(sess.last_resp_buf, err_buf, MSG_SIZE_SHORT);
                sess.last_resp_size = MSG_SIZE_SHORT;
                sess.has_last_resp = true;
                sess.last_seqnum = seqnum;

                continue;
            }

            uint8_t resp_chars[8];
            memset(resp_chars, '-', 8);

            evaluate(secret, msg.a, na, resp_chars);

            sess.tries_used++;

            int remaining = NT - sess.tries_used;

            Message res_msg;
            make_message(res_msg, MSG_RES, (uint16_t)remaining,
                         resp_chars, na, MSG_SIZE_FULL);

            uint8_t res_buf[MSG_SIZE_FULL];
            serialize(res_msg, res_buf, MSG_SIZE_FULL);

            send_msg(sockfd, res_buf, MSG_SIZE_FULL, cli_addr, cli_len);

            memcpy(sess.last_resp_buf, res_buf, MSG_SIZE_FULL);
            sess.last_resp_size = MSG_SIZE_FULL;
            sess.has_last_resp = true;
            sess.last_seqnum = seqnum;

        } else if (tipo == MSG_BYE) {
            if (sess_idx < 0) {
                continue;
            }

            ClientSession& sess = sessions[sess_idx];

            if (sess.finished) {
                send_msg(sockfd,
                         sess.last_resp_buf,
                         sess.last_resp_size,
                         cli_addr,
                         cli_len);
                continue;
            }

            uint8_t secret_digits[8];
            memset(secret_digits, 0, 8);

            for (int i = 0; i < na; i++) {
                secret_digits[i] = secret[i] - '0';
            }

            Message res_msg;
            make_message(res_msg, MSG_RES, (uint16_t)0xFFFF,
                         secret_digits, na, MSG_SIZE_FULL);

            uint8_t res_buf[MSG_SIZE_FULL];
            serialize(res_msg, res_buf, MSG_SIZE_FULL);

            send_msg(sockfd, res_buf, MSG_SIZE_FULL, cli_addr, cli_len);

            memcpy(sess.last_resp_buf, res_buf, MSG_SIZE_FULL);
            sess.last_resp_size = MSG_SIZE_FULL;
            sess.has_last_resp = true;

            sess.finished = true;
            clients_served++;

        } else if (tipo == MSG_ERR) {
            continue;
        }
    }

    close(sockfd);
    return 0;
}