/*
 Copyright (c) 2026. Giulio Cocconi

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
*/

#include "tests.hpp"

#include <core/wireUtils.hpp>
#include <utils/num_formatting.hpp>

#include <string>
#include <tuple>
#include <vector>

using namespace SILICON::core;

TEST(NumFormattingTest, FormatsKnownValues)
{
  const auto value = busValueFromBits("1010");

  EXPECT_EQ(formatValue(value, BusValueFormat::Raw), "1010");
  EXPECT_EQ(formatValue(value, BusValueFormat::Unsigned), "10");
  EXPECT_EQ(formatValue(value, BusValueFormat::Signed), "-6");
  EXPECT_EQ(formatValue(value, BusValueFormat::Hex), "0xA");
  EXPECT_EQ(formatValue(value, BusValueFormat::Oct), "0o12");
  EXPECT_EQ(formatValue(value, BusValueFormat::Bin), "0b1010");
  EXPECT_EQ(formatValue(busValueFromInteger(10, 8), BusValueFormat::Hex, 2),
            "0x0A");
}

TEST(NumFormattingTest, MaxValueHasNoMachineIntegerWidthLimit)
{
  EXPECT_EQ(maxValueForBusWidth(4), busValueFromBits("1111"));
  EXPECT_EQ(maxValueForBusWidth(128), BusValue(128, State::HIGH));
}

TEST(NumFormattingTest, ParsesSupportedNumericFormats)
{
  for (const auto& [text, expectedFormat, expectedWidth] :
       std::vector<std::tuple<std::string, BusValueFormat, std::size_t>>{
           {"42", BusValueFormat::Unsigned, 6},
           {" 0x2a ", BusValueFormat::Hex, 8},
           {"0o52", BusValueFormat::Oct, 6},
           {"0b101010", BusValueFormat::Bin, 6}}) {
    const auto [value, format] = valueFromStr(text);
    EXPECT_EQ(format, expectedFormat);
    EXPECT_EQ(value.size(), expectedWidth);
    EXPECT_EQ(formatValue(value, BusValueFormat::Unsigned), "42");
  }

  const auto [hexWithE, format] = valueFromStr("0xFE");
  EXPECT_EQ(format, BusValueFormat::Hex);
  EXPECT_EQ(hexWithE, busValueFromBits("11111110"));
}

TEST(NumFormattingTest, ParsesFourStateRawValuesAsMsbFirst)
{
  const auto [binaryValue, binaryFormat] = valueFromStr("1010");
  EXPECT_EQ(binaryFormat, BusValueFormat::Raw);
  EXPECT_EQ(binaryValue, busValueFromBits("1010"));

  const auto [value, format] = valueFromStr("10xE");
  EXPECT_EQ(format, BusValueFormat::Raw);
  EXPECT_EQ(value, BusValue({State::ERROR, State::UNKNOWN, State::LOW,
                             State::HIGH}));
  EXPECT_EQ(formatValue(value, BusValueFormat::Raw), "10XE");
  EXPECT_EQ(formatValue(value, BusValueFormat::Hex), "10XE");
}

TEST(NumFormattingTest, RejectsInvalidInput)
{
  EXPECT_EQ(valueFromStr("").format, BusValueFormat::Unknown);
  EXPECT_EQ(valueFromStr("0b102").format, BusValueFormat::Unknown);
  EXPECT_EQ(valueFromStr("10Z1").format, BusValueFormat::Unknown);
  EXPECT_EQ(valueFromStr("-0x2a").format, BusValueFormat::Unknown);
  EXPECT_THROW((void)busValueFromBits("10Z1"), std::invalid_argument);
}

TEST(NumFormattingTest, SignedUsesTwosComplementWidth)
{
  EXPECT_EQ(formatValue(busValueFromBits("1111"), BusValueFormat::Signed),
            "-1");
  EXPECT_EQ(formatValue(busValueFromBits("1000"), BusValueFormat::Signed),
            "-8");
  EXPECT_EQ(formatValue(busValueFromBits("0111"), BusValueFormat::Signed),
            "7");

  const auto [negative, format] = valueFromStr("-42");
  EXPECT_EQ(format, BusValueFormat::Signed);
  EXPECT_EQ(formatValue(negative, BusValueFormat::Signed), "-42");
}

TEST(NumFormattingTest, ResizesParsedSignedValuesWithTwosComplementSemantics)
{
  const auto expectResize = [](const std::string_view text, const std::size_t width,
                               const std::string_view expected) {
    const auto resized = resizeParsedValue(valueFromStr(text), width);
    ASSERT_TRUE(resized) << text << " at " << width << " bits";
    EXPECT_EQ(formatValue(*resized, BusValueFormat::Raw), expected);
  };

  expectResize("-1", 1, "1");
  expectResize("-1", 4, "1111");
  expectResize("-1", 8, "11111111");
  expectResize("-8", 4, "1000");
  expectResize("-8", 8, "11111000");
  expectResize("127", 8, "01111111");

  EXPECT_FALSE(resizeParsedValue(valueFromStr("-9"), 4));
  EXPECT_FALSE(resizeParsedValue(valueFromStr("+128"), 8));
}

TEST(NumFormattingTest, ResizesUnsignedAndRawValuesWithZeroExtension)
{
  EXPECT_EQ(resizeParsedValue(valueFromStr("255"), 8),
            busValueFromBits("11111111"));
  EXPECT_FALSE(resizeParsedValue(valueFromStr("256"), 8));
  EXPECT_EQ(resizeParsedValue(valueFromStr("0b101"), 8),
            busValueFromBits("00000101"));
  EXPECT_EQ(resizeParsedValue(valueFromStr("X"), 8),
            BusValue(8, State::UNKNOWN));

  const auto wide = valueFromStr("36893488147419103231");
  EXPECT_TRUE(resizeParsedValue(wide, 65));
  EXPECT_FALSE(resizeParsedValue(wide, 64));
}

TEST(NumFormattingTest, NumericFitChecksAreSeparateFromBitVectorNormalization)
{
  EXPECT_TRUE(SILICON::wireUtils::fitsUnsigned(busValueFromBits("000001"), 4));
  EXPECT_FALSE(SILICON::wireUtils::fitsUnsigned(busValueFromBits("100001"), 4));
  EXPECT_TRUE(SILICON::wireUtils::fitsSigned(busValueFromBits("11111"), 1));
  EXPECT_TRUE(SILICON::wireUtils::fitsSigned(busValueFromBits("11000"), 4));
  EXPECT_FALSE(SILICON::wireUtils::fitsSigned(busValueFromBits("10111"), 4));

  EXPECT_EQ(SILICON::wireUtils::normalizeBusValue(
                busValueFromBits("100001"), 4),
            busValueFromBits("0001"));
  EXPECT_EQ(SILICON::wireUtils::normalizeBusValue(
                busValueFromBits("1"), 4, State::HIGH),
            busValueFromBits("1111"));
}

TEST(NumFormattingTest, SupportsValuesWiderThanMachineIntegers)
{
  EXPECT_EQ(formatValue(BusValue(65, State::HIGH), BusValueFormat::Unsigned),
            "36893488147419103231");
  EXPECT_EQ(formatValue(busValueFromBits("1" + std::string(64, '0')),
                        BusValueFormat::Signed),
            "-18446744073709551616");

  const auto [wide, format] = valueFromStr("36893488147419103231");
  EXPECT_EQ(format, BusValueFormat::Unsigned);
  EXPECT_EQ(wide, BusValue(65, State::HIGH));
}
