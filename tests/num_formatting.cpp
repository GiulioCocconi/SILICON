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

TEST(NumFormattingTest, FixedWidthHexMatchesBusWidth)
{
  EXPECT_EQ(silicon::formatFixedWidthHex(0, 1), "0X0");
  EXPECT_EQ(silicon::formatFixedWidthHex(10, 4), "0XA");
  EXPECT_EQ(silicon::formatFixedWidthHex(10, 8), "0X0A");
  EXPECT_EQ(silicon::formatFixedWidthHex(0x123, 12), "0X123");
}

TEST(NumFormattingTest, MaxValueForBusWidthClampsAtUnsignedIntWidth)
{
  EXPECT_EQ(silicon::maxValueForBusWidth(4), 15U);
  EXPECT_EQ(silicon::maxValueForBusWidth(std::numeric_limits<unsigned int>::digits),
            std::numeric_limits<unsigned int>::max());
}

TEST(NumFormattingTest, ParseBusValueAcceptsDecimalHexAndBinary)
{
  unsigned int value = 0;

  ASSERT_TRUE(silicon::parseBusValue("42", value));
  EXPECT_EQ(value, 42U);

  ASSERT_TRUE(silicon::parseBusValue(" 0x2a ", value));
  EXPECT_EQ(value, 42U);

  ASSERT_TRUE(silicon::parseBusValue("0b101010", value));
  EXPECT_EQ(value, 42U);

  EXPECT_FALSE(silicon::parseBusValue("", value));
  EXPECT_FALSE(silicon::parseBusValue("0b102", value));
}

TEST(NumFormattingTest, FormatsKnownRawBits)
{
  EXPECT_EQ(silicon::formatRawBits("1010", silicon::NumberFormat::Unsigned), "10");
  EXPECT_EQ(silicon::formatRawBits("1010", silicon::NumberFormat::Signed), "-6");
  EXPECT_EQ(silicon::formatRawBits("1010", silicon::NumberFormat::Hex), "0XA");
  EXPECT_EQ(silicon::formatRawBits("1010", silicon::NumberFormat::Oct), "0O12");
  EXPECT_EQ(silicon::formatRawBits("1010", silicon::NumberFormat::Bin), "0B1010");
}

TEST(NumFormattingTest, SignedUsesTwosComplementWidth)
{
  EXPECT_EQ(silicon::formatRawBits("1111", silicon::NumberFormat::Signed), "-1");
  EXPECT_EQ(silicon::formatRawBits("1000", silicon::NumberFormat::Signed), "-8");
  EXPECT_EQ(silicon::formatRawBits("0111", silicon::NumberFormat::Signed), "7");
}

TEST(NumFormattingTest, SupportsWideValuesWithoutBigIntDependency)
{
  EXPECT_EQ(silicon::formatRawBits(std::string(65, '1'), silicon::NumberFormat::Unsigned),
            "36893488147419103231");
  EXPECT_EQ(silicon::formatRawBits("1" + std::string(64, '0'),
                                   silicon::NumberFormat::Signed),
            "-18446744073709551616");
}

TEST(NumFormattingTest, UnknownAndMixedValuesRemainReadable)
{
  EXPECT_EQ(silicon::formatRawBits("x", silicon::NumberFormat::Hex), "X");
  EXPECT_EQ(silicon::formatRawBits("z", silicon::NumberFormat::Unsigned), "Z");
  EXPECT_EQ(silicon::formatRawBits("10x1", silicon::NumberFormat::Signed), "10X1");
}
