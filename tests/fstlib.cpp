/*
  Copyright (C) 2026 Giulio Cocconi

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
#include <fstapi.h>

using namespace SILICON::core;

TEST(FstLibTest, IO)
{
  fstWriterContext* writer = fstWriterCreate("out.fst", 1);
  ASSERT_TRUE(writer);

  const fstHandle var =
      fstWriterCreateVar(writer, FST_VT_VCD_WIRE, FST_VD_IMPLICIT, 1, "var", 0);

  fstWriterEmitTimeChange(writer, 0);
  fstWriterEmitValueChange(writer, var, "0");
  fstWriterEmitTimeChange(writer, 1);
  fstWriterEmitValueChange(writer, var, "1");
  fstWriterEmitTimeChange(writer, 2);

  fstWriterClose(writer);

  // Read generated FST file

  fstReaderContext* reader = fstReaderOpen("out.fst");
  ASSERT_TRUE(reader);

  EXPECT_EQ(fstReaderGetStartTime(reader), 0);
  EXPECT_EQ(fstReaderGetEndTime(reader), 2);
  EXPECT_EQ(fstReaderGetVarCount(reader), 1);
  EXPECT_EQ(fstReaderGetScopeCount(reader), 0);
  EXPECT_EQ(fstReaderGetAliasCount(reader), 0);

  fstHier* iter = fstReaderIterateHier(reader);
  ASSERT_TRUE(iter);

  EXPECT_EQ(iter->htyp, FST_HT_VAR);
  EXPECT_STREQ(iter->u.var.name, "var");
  EXPECT_EQ(iter->u.var.length, 1);

  iter = fstReaderIterateHier(reader);
  EXPECT_FALSE(iter);
  fstReaderClose(reader);
}
