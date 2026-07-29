/*
 Copyright (c) 2026. Giulio Cocconi

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "tests.hpp"

#include <utils/num_formatting.hpp>

#include <limits>

using namespace SILICON::core;

TEST(NumFormattingTest, FixedWidthHexMatchesBusWidth)
{
  EXPECT_EQ(SILICON::core::formatFixedWidthHex(0, 1), "0X0");
  EXPECT_EQ(SILICON::core::formatFixedWidthHex(10, 4), "0XA");
  EXPECT_EQ(SILICON::core::formatFixedWidthHex(10, 8), "0X0A");
  EXPECT_EQ(SILICON::core::formatFixedWidthHex(0x123, 12), "0X123");
}

TEST(NumFormattingTest, MaxValueForBusWidthClampsAtUnsignedIntWidth)
{
  EXPECT_EQ(SILICON::core::maxValueForBusWidth(4), 15U);
  EXPECT_EQ(SILICON::core::maxValueForBusWidth(std::numeric_limits<unsigned int>::digits),
            std::numeric_limits<unsigned int>::max());
}

TEST(NumFormattingTest, ParseBusValueAcceptsDecimalHexAndBinary)
{
  unsigned int value = 0;

  ASSERT_TRUE(SILICON::core::parseBusValue("42", value));
  EXPECT_EQ(value, 42U);

  ASSERT_TRUE(SILICON::core::parseBusValue(" 0x2a ", value));
  EXPECT_EQ(value, 42U);

  ASSERT_TRUE(SILICON::core::parseBusValue("0b101010", value));
  EXPECT_EQ(value, 42U);

  EXPECT_FALSE(SILICON::core::parseBusValue("", value));
  EXPECT_FALSE(SILICON::core::parseBusValue("0b102", value));
}

TEST(NumFormattingTest, FormatsKnownRawBits)
{
  EXPECT_EQ(SILICON::core::formatRawBits("1010", SILICON::core::NumberFormat::Unsigned), "10");
  EXPECT_EQ(SILICON::core::formatRawBits("1010", SILICON::core::NumberFormat::Signed), "-6");
  EXPECT_EQ(SILICON::core::formatRawBits("1010", SILICON::core::NumberFormat::Hex), "0XA");
  EXPECT_EQ(SILICON::core::formatRawBits("1010", SILICON::core::NumberFormat::Oct), "0O12");
  EXPECT_EQ(SILICON::core::formatRawBits("1010", SILICON::core::NumberFormat::Bin), "0B1010");
}

TEST(NumFormattingTest, SignedUsesTwosComplementWidth)
{
  EXPECT_EQ(SILICON::core::formatRawBits("1111", SILICON::core::NumberFormat::Signed), "-1");
  EXPECT_EQ(SILICON::core::formatRawBits("1000", SILICON::core::NumberFormat::Signed), "-8");
  EXPECT_EQ(SILICON::core::formatRawBits("0111", SILICON::core::NumberFormat::Signed), "7");
}

TEST(NumFormattingTest, SupportsWideValuesWithoutBigIntDependency)
{
  EXPECT_EQ(SILICON::core::formatRawBits(std::string(65, '1'), SILICON::core::NumberFormat::Unsigned),
            "36893488147419103231");
  EXPECT_EQ(
      SILICON::core::formatRawBits("1" + std::string(64, '0'), SILICON::core::NumberFormat::Signed),
      "-18446744073709551616");
}

TEST(NumFormattingTest, UnknownAndMixedValuesRemainReadable)
{
  EXPECT_EQ(SILICON::core::formatRawBits("x", SILICON::core::NumberFormat::Hex), "X");
  EXPECT_EQ(SILICON::core::formatRawBits("z", SILICON::core::NumberFormat::Unsigned), "Z");
  EXPECT_EQ(SILICON::core::formatRawBits("10x1", SILICON::core::NumberFormat::Signed), "10X1");
}
