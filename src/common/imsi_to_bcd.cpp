#include "imsi_to_bcd.h"
#include <cctype>
#include <stdexcept>

std::vector<uint8_t> encode_imsi_bcd(const std::string &imsi) {
    if (imsi.empty()) {
        throw std::invalid_argument("IMSI cannot be empty");
    }

    for (char c : imsi) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            throw std::invalid_argument("IMSI must contain only digits 0-9");
        }
    }

    std::vector<uint8_t> out;
    out.reserve((imsi.size() + 1) / 2);

    size_t i = 0;
    while (i < imsi.size()) {
        uint8_t low = static_cast<uint8_t>(imsi[i] - '0');
        uint8_t high;
        if (i + 1 < imsi.size()) high = static_cast<uint8_t>(imsi[i + 1] - '0');
        else high = 0x0F;

        uint8_t byte = static_cast<uint8_t>((high << 4) | (low & 0x0F));
        out.push_back(byte);
        i += 2;
    }
    return out;
}


std::string decode_imsi_bcd(const std::vector<uint8_t> &bcd) {
    std::string imsi;
    imsi.reserve(bcd.size() * 2);
    for (size_t i = 0; i < bcd.size(); ++i) {
        uint8_t byte = bcd[i];
        uint8_t low = byte & 0x0F;
        uint8_t high = (byte >> 4) & 0x0F;

        imsi.push_back(static_cast<char>('0' + low));
        if (high == 0x0F) {
            break;
        } else {
            imsi.push_back(static_cast<char>('0' + high));
        }
    }
    return imsi;
}