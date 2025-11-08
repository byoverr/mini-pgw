#include <gtest/gtest.h>
#include "imsi_to_bcd.h"
#include <vector>
#include <sstream>
#include <iomanip>


TEST(IMSI_BCD, EncodeDecodeEven) {
    std::string s = "123456";
    auto b = encode_imsi_bcd(s);
    ASSERT_EQ(b.size(), 3u);
    EXPECT_EQ(decode_imsi_bcd(b), s);
}

TEST(IMSI_BCD, EncodeDecodeOdd) {
    std::string s = "12345";
    auto b = encode_imsi_bcd(s);
    EXPECT_EQ(decode_imsi_bcd(b), s);
    EXPECT_EQ((b.back() >> 4) & 0x0F, 0x0F);
}

TEST(IMSI_BCD, SingleDigit) {
    std::string s = "1";
    auto b = encode_imsi_bcd(s);
    EXPECT_EQ(b.size(), 1u);
    EXPECT_EQ(decode_imsi_bcd(b), s);
    EXPECT_EQ((b[0] >> 4) & 0x0F, 0x0F);
}

TEST(IMSI_BCD, TwoDigits) {
    std::string s = "12";
    auto b = encode_imsi_bcd(s);
    EXPECT_EQ(b.size(), 1u);
    EXPECT_EQ(decode_imsi_bcd(b), s);
}

TEST(IMSI_BCD, LongIMSI) {
    std::string s = "001010123456789";
    auto b = encode_imsi_bcd(s);
    EXPECT_EQ(b.size(), 8u);
    EXPECT_EQ(decode_imsi_bcd(b), s);
}

TEST(IMSI_BCD, AllZeros) {
    std::string s = "0000";
    auto b = encode_imsi_bcd(s);
    EXPECT_EQ(decode_imsi_bcd(b), s);
    for (auto byte : b) {
        EXPECT_EQ(byte, 0x00);
    }
}

TEST(IMSI_BCD, AllNines) {
    std::string s = "9999";
    auto b = encode_imsi_bcd(s);
    EXPECT_EQ(decode_imsi_bcd(b), s);
    for (auto byte : b) {
        EXPECT_EQ(byte, 0x99);
    }
}

TEST(IMSI_BCD, Empty) {
    EXPECT_THROW(encode_imsi_bcd(""), std::invalid_argument);
}

TEST(IMSI_BCD, InvalidChars) {
    EXPECT_THROW(encode_imsi_bcd("12a45"), std::invalid_argument);
    EXPECT_THROW(encode_imsi_bcd("abc"), std::invalid_argument);
    EXPECT_THROW(encode_imsi_bcd("12-34"), std::invalid_argument);
    EXPECT_THROW(encode_imsi_bcd("12 34"), std::invalid_argument);
}

TEST(IMSI_BCD, SpecialChars) {
    EXPECT_THROW(encode_imsi_bcd("12@34"), std::invalid_argument);
    EXPECT_THROW(encode_imsi_bcd("12.34"), std::invalid_argument);
}


TEST(IMSI_BCD, RoundTripVariousLengths) {
    std::vector<std::string> test_cases = {
        "1", "12", "123", "1234", "12345", "123456", "1234567",
        "001010123456789", "999999999999999"
    };
    
    for (const auto& imsi : test_cases) {
        auto encoded = encode_imsi_bcd(imsi);
        auto decoded = decode_imsi_bcd(encoded);
        EXPECT_EQ(decoded, imsi) << "Failed for IMSI: " << imsi;
    }
}

TEST(IMSI_BCD, ByteFormatEven) {
    auto b = encode_imsi_bcd("12");
    ASSERT_EQ(b.size(), 1u);
    EXPECT_EQ(b[0], 0x21);
    
    b = encode_imsi_bcd("34");
    ASSERT_EQ(b.size(), 1u);
    EXPECT_EQ(b[0], 0x43);
}

TEST(IMSI_BCD, ByteFormatOdd) {
    auto b = encode_imsi_bcd("1");
    ASSERT_EQ(b.size(), 1u);
    EXPECT_EQ(b[0] & 0x0F, 0x01); 
    EXPECT_EQ((b[0] >> 4) & 0x0F, 0x0F); 
}

TEST(IMSI_BCD, ByteFormatLong) {
    auto b = encode_imsi_bcd("1234");
    ASSERT_EQ(b.size(), 2u);
    EXPECT_EQ(b[0], 0x21);
    EXPECT_EQ(b[1], 0x43);
}
