//OpenSSL
#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/err.h>
///////////

#define FRAME_SIZE		42

typedef struct {
    uint16_t sync;
    uint16_t framesize;
    uint16_t id_code;
    uint32_t soc;
    uint32_t fracsec;
    uint16_t stat;
    float voltage_amplitude;
    float voltage_angle;
    float current_amplitude;
    float current_angle;
    float voltage_frequency;
    float delta_frequency; // not reported/used by their producers/consumers
    uint16_t crc;
} c37_packet;

c37_packet *get_c37_packet(char *data);
void write_c37_packet(SSL *output, c37_packet *pkt);
void write_c37_packet_readable(FILE *output, c37_packet *pkt);
