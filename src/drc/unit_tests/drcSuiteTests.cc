
/*

  KLayout Layout Viewer
  Copyright (C) 2006-2017 Matthias Koefferlein

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

*/

#include "utHead.h"
#include "dbReader.h"
#include "lymMacro.h"

void runtest (ut::TestBase *_this, int mode)
{
  std::string rs = ut::testsrc ();
  rs += "/testdata/drc/drcSuiteTests.drc";

  std::string input = ut::testsrc ();
  input += "/testdata/drc/drctest.gds";

  std::string au = ut::testsrc ();
  au += "/testdata/drc/drcSuiteTests_au";
  au += tl::to_string (mode);
  au += ".gds";

  std::string output = _this->tmp_file ("tmp.gds");

  {
    //  Set some variables
    lym::Macro config;
    config.set_text (tl::sprintf (
        "$drc_test_source = \"%s\"\n"
        "$drc_test_target = \"%s\"\n"
        "$drc_test_mode = %d\n"
      , input, output, mode)
    );
    config.set_interpreter (lym::Macro::Ruby);
    EXPECT_EQ (config.run (), 0);
  }

  lym::Macro drc;
  drc.load_from (rs);
  EXPECT_EQ (drc.run (), 0);

  db::Layout layout;

  {
    tl::InputStream stream (output);
    db::Reader reader (stream);
    reader.read (layout);
  }

  _this->compare_layouts (layout, au, ut::NoNormalization);
}

TEST(1)
{
  runtest (_this, 1);
}

TEST(2)
{
  test_is_long_runner ();
  runtest (_this, 2);
}

TEST(3)
{
  test_is_long_runner ();
  runtest (_this, 3);
}

TEST(4)
{
  test_is_long_runner ();
  runtest (_this, 4);
}
