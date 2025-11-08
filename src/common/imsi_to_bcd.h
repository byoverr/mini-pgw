#pragma once
#include <string>
#include <vector>
#include <cstdint>

std::vector<uint8_t> encode_imsi_bcd(const std::string &imsi);

std::string decode_imsi_bcd(const std::vector<uint8_t> &bcd);
