/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#include <ui/logiFlow/code/codeSyntax.hpp>

#include <array>

namespace SILICON::project {
namespace {

  // Adapted from Vim's runtime/syntax/verilog.vim.
  constexpr auto StatementWords = std::to_array<std::string_view>({
      std::string_view{"always"},
      "and",
      "assign",
      "automatic",
      "buf",
      "bufif0",
      "bufif1",
      "cell",
      "cmos",
      "config",
      "deassign",
      "defparam",
      "design",
      "disable",
      "edge",
      "endconfig",
      "endfunction",
      "endgenerate",
      "endmodule",
      "endprimitive",
      "endspecify",
      "endtable",
      "endtask",
      "event",
      "force",
      "function",
      "generate",
      "genvar",
      "highz0",
      "highz1",
      "ifnone",
      "incdir",
      "include",
      "initial",
      "inout",
      "input",
      "instance",
      "integer",
      "large",
      "liblist",
      "library",
      "localparam",
      "macromodule",
      "medium",
      "module",
      "nand",
      "negedge",
      "nmos",
      "nor",
      "noshowcancelled",
      "not",
      "notif0",
      "notif1",
      "or",
      "output",
      "parameter",
      "pmos",
      "posedge",
      "primitive",
      "pull0",
      "pull1",
      "pulldown",
      "pullup",
      "pulsestyle_onevent",
      "pulsestyle_ondetect",
      "rcmos",
      "real",
      "realtime",
      "reg",
      "release",
      "rnmos",
      "rpmos",
      "rtran",
      "rtranif0",
      "rtranif1",
      "scalared",
      "showcancelled",
      "signed",
      "small",
      "specify",
      "specparam",
      "strong0",
      "strong1",
      "supply0",
      "supply1",
      "table",
      "task",
      "time",
      "tran",
      "tranif0",
      "tranif1",
      "tri",
      "tri0",
      "tri1",
      "triand",
      "trior",
      "trireg",
      "unsigned",
      "use",
      "vectored",
      "wait",
      "wand",
      "weak0",
      "weak1",
      "wire",
      "wor",
      "xnor",
      "xor",
  });

  constexpr auto LabelWords =
      std::to_array<std::string_view>({"begin", "end", "fork", "join"});
  constexpr auto ConditionalWords = std::to_array<std::string_view>(
      {"if", "else", "case", "casex", "casez", "default", "endcase"});
  constexpr auto RepeatWords =
      std::to_array<std::string_view>({"forever", "repeat", "while", "for"});

  constexpr std::array KeywordGroups{
      CodeSyntaxKeywordGroup{CodeSyntaxStyle::Statement, StatementWords},
      CodeSyntaxKeywordGroup{CodeSyntaxStyle::Label, LabelWords},
      CodeSyntaxKeywordGroup{CodeSyntaxStyle::Conditional, ConditionalWords},
      CodeSyntaxKeywordGroup{CodeSyntaxStyle::Repeat, RepeatWords},
  };

  constexpr std::array TodoRules{CodeSyntaxContainedRule{
      CodeSyntaxStyle::Todo, R"((?<![A-Za-z0-9_?])(?:TODO|FIXME)(?![A-Za-z0-9_?]))"}};
  constexpr std::array EscapeRules{
      CodeSyntaxContainedRule{CodeSyntaxStyle::Escape, R"(\\(?:[nt"\\]|[0-7]{1,3}))"}};

  constexpr std::array MatchRules{
      CodeSyntaxMatchRule{
          CodeSyntaxStyle::Operator, R"([&|~><!)(*#%@+/=?:;}{,.^\-\[\]])", {}},
      CodeSyntaxMatchRule{CodeSyntaxStyle::Constant,
                          R"((?<![A-Za-z0-9_?])[A-Z][A-Z0-9_]+(?![A-Za-z0-9_?]))",
                          {}},
      CodeSyntaxMatchRule{
          CodeSyntaxStyle::Number,
          R"((?<![A-Za-z0-9_?])(?:[0-9]+)?'[sS]?[bB]\s*[0-1_xXzZ?]+(?![A-Za-z0-9_?]))",
          {}},
      CodeSyntaxMatchRule{
          CodeSyntaxStyle::Number,
          R"((?<![A-Za-z0-9_?])(?:[0-9]+)?'[sS]?[oO]\s*[0-7_xXzZ?]+(?![A-Za-z0-9_?]))",
          {}},
      CodeSyntaxMatchRule{
          CodeSyntaxStyle::Number,
          R"((?<![A-Za-z0-9_?])(?:[0-9]+)?'[sS]?[dD]\s*[0-9_xXzZ?]+(?![A-Za-z0-9_?]))",
          {}},
      CodeSyntaxMatchRule{
          CodeSyntaxStyle::Number,
          R"((?<![A-Za-z0-9_?])(?:[0-9]+)?'[sS]?[hH]\s*[0-9a-fA-F_xXzZ?]+(?![A-Za-z0-9_?]))",
          {}},
      CodeSyntaxMatchRule{
          CodeSyntaxStyle::Number,
          R"((?<![A-Za-z0-9_?])[+-]?[0-9_]+(?:\.[0-9_]*)?(?:e[0-9_]*)?(?![A-Za-z0-9_?]))",
          {}},
      CodeSyntaxMatchRule{
          CodeSyntaxStyle::Global,
          R"(`(?:celldefine|default_nettype|define|else|elsif|endcelldefine|endif|ifdef|ifndef|include|line|nounconnected_drive|resetall|timescale|unconnected_drive|undef)(?![A-Za-z0-9_?]))",
          {}},
      CodeSyntaxMatchRule{CodeSyntaxStyle::Global, R"(\$[A-Za-z0-9_]+\b)", {}},
  };

  // More specific regions precede their generic comment counterparts so ties at
  // the same start position resolve like Vim's contained syntax groups.
  constexpr std::array RegionRules{
      CodeSyntaxRegionRule{CodeSyntaxStyle::Directive,
                           R"(//\s*synopsys dc_script_begin\b)",
                           R"(//\s*synopsys dc_script_end\b)",
                           {},
                           {},
                           true},
      CodeSyntaxRegionRule{CodeSyntaxStyle::Directive,
                           R"(//\s*\$s dc_script_begin\b)",
                           R"(//\s*\$s dc_script_end\b)",
                           {},
                           {},
                           true},
      CodeSyntaxRegionRule{
          CodeSyntaxStyle::Directive, R"(/\*\s*synopsys\b)", R"(\*/)", {}, {}, true},
      CodeSyntaxRegionRule{
          CodeSyntaxStyle::Directive, R"(/\*\s*\$s\b)", R"(\*/)", {}, {}, true},
      CodeSyntaxRegionRule{
          CodeSyntaxStyle::Directive, R"(//\s*synopsys\b)", R"($)", {}, {}, false},
      CodeSyntaxRegionRule{
          CodeSyntaxStyle::Directive, R"(//\s*\$s\b)", R"($)", {}, {}, false},
      CodeSyntaxRegionRule{
          CodeSyntaxStyle::Comment, R"(/\*)", R"(\*/)", {}, TodoRules, true},
      CodeSyntaxRegionRule{
          CodeSyntaxStyle::Comment, R"(//)", R"($)", {}, TodoRules, false},
      CodeSyntaxRegionRule{CodeSyntaxStyle::String, R"(")", R"(")", R"(\\.)", EscapeRules,
                           false},
  };

}  // namespace

const CodeSyntax VERILOG_CODE_SYNTAX{
    .keywordGroups       = KeywordGroups,
    .matchRules          = MatchRules,
    .regionRules         = RegionRules,
    .extraWordCharacters = "?",
};

}  // namespace SILICON::project
