#include <gtest/gtest.h>

#include "argparse.h"

#if defined(__clang__)
// NOTE: It's much easier to test passing a char** when creating the string
// literals.
#pragma GCC diagnostic ignored "-Wwritable-strings"
#else
#pragma GCC diagnostic ignored "-Wwrite-strings"
#endif

namespace {

TEST(ArgParse, SimplePosArgs) {
  constexpr char *kArgv[] = {
      "exe",
      "arg1",
      "arg2",
      nullptr,
  };
  constexpr int kArgc = 3;

  argparse::ArgParser parser;
  parser.AddPosArg("pos1");
  parser.AddPosArg("pos2");

  auto res = parser.ParseArgs(kArgc, kArgv);

  EXPECT_EQ(res.get("pos1"), "arg1");
  EXPECT_EQ(res.get("pos2"), "arg2");
}

TEST(ArgParse, OptArg) {
  argparse::ArgParser parser;
  parser.AddPosArg("pos1");
  parser.AddOptArg("opt");

  auto check = [&](int argc, char * const*argv){
    auto res = parser.ParseArgs(argc, argv);
    EXPECT_EQ(res.get("pos1"), "arg1");
    EXPECT_EQ(res.get("opt"), "optarg");
  };

  constexpr int kArgc = 4;

  {
    constexpr char *kArgv[] = {
        "exe",
        "arg1",
        "--opt",
        "optarg",
        nullptr,
    };
    check(kArgc, kArgv);
  }

  {
    constexpr char *kArgv[] = {
        "exe",
        "--opt",
        "optarg",
        "arg1",
        nullptr,
    };
    check(kArgc, kArgv);
  }
}

TEST(ArgParse, OptArgStoreTrue) {
  constexpr char *kArgv[] = {
      "exe",
      "arg1",
      "--opt",
      nullptr,
  };
  constexpr int kArgc = 3;

  argparse::ArgParser parser;
  parser.AddPosArg("pos1");
  parser.AddOptArg("opt").setStoreTrue();

  auto res = parser.ParseArgs(kArgc, kArgv);
  EXPECT_TRUE(res.get<bool>("opt"));
}

TEST(ArgParse, OptArgStoreTrueDefaultFalse) {
  constexpr char *kArgv[] = {
      "exe",
      "arg1",
      nullptr,
  };
  constexpr int kArgc = 2;

  argparse::ArgParser parser;
  parser.AddPosArg("pos1");
  parser.AddOptArg("opt").setStoreTrue();

  auto res = parser.ParseArgs(kArgc, kArgv);
  EXPECT_FALSE(res.get<bool>("opt"));
}

TEST(ArgParse, OptArgShortname) {
  constexpr char *kArgv[] = {
      "exe",
      "arg1",
      "-o",
      "optarg",
      nullptr,
  };
  constexpr int kArgc = 4;

  argparse::ArgParser parser;
  parser.AddPosArg("pos1");
  parser.AddOptArg("opt", 'o');

  auto res = parser.ParseArgs(kArgc, kArgv);
  EXPECT_EQ(res.get("opt"), "optarg");
}

TEST(ArgParse, OptArgAppend) {
  constexpr char *kArgv[] = {
      "exe",
      "--opt",
      "arg1",
      "arg2",
      "--opt",
      "arg3",
      nullptr,
  };
  constexpr int kArgc = 6;

  argparse::ArgParser parser;
  parser.AddPosArg("pos1");
  parser.AddOptArg("opt").setAppend();

  auto res = parser.ParseArgs(kArgc, kArgv);
  std::vector<std::string> expected{"arg1", "arg3"};
  EXPECT_EQ(res.getList("opt"), expected);
  EXPECT_EQ(res.get("pos1"), "arg2");
}

TEST(ArgParse, OptArgAppendDefaultNone) {
  constexpr char *kArgv[] = {
      "exe",
      "arg1",
      nullptr,
  };
  constexpr int kArgc = 2;

  argparse::ArgParser parser;
  parser.AddPosArg("pos1");
  parser.AddOptArg("opt").setAppend();

  auto res = parser.ParseArgs(kArgc, kArgv);
  EXPECT_FALSE(res.has("opt"));
}

TEST(ArgParse, OptArgAppendExplicitDefault) {
  constexpr char *kArgv[] = {
      "exe",
      "arg1",
      nullptr,
  };
  constexpr int kArgc = 2;

  argparse::ArgParser parser;
  parser.AddPosArg("pos1");
  parser.AddOptArg("opt").setAppend().setDefaultList();

  auto res = parser.ParseArgs(kArgc, kArgv);
  EXPECT_TRUE(res.has("opt"));
  EXPECT_TRUE(res.getList("opt").empty());
}

TEST(ArgParse, OptArgAppendShortName) {
  constexpr char *kArgv[] = {
      "exe",
      "-o",
      "arg1",
      "arg2",
      "-o",
      "arg3",
      nullptr,
  };
  constexpr int kArgc = 6;

  argparse::ArgParser parser;
  parser.AddPosArg("pos1");
  parser.AddOptArg("opt", 'o').setAppend();

  auto res = parser.ParseArgs(kArgc, kArgv);
  std::vector<std::string> expected{"arg1", "arg3"};
  EXPECT_EQ(res.getList("opt"), expected);
  EXPECT_EQ(res.get("pos1"), "arg2");
}

}  // namespace
