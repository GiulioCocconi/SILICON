/* Standalone smoke test for Verilog -> Yosys JSON -> Silicon -> Verilog. */

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#include <core/circuit.hpp>

namespace {

class TemporaryDirectory {
public:
  TemporaryDirectory()
  {
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    path              = std::filesystem::temp_directory_path()
           / std::format("silicon_yosys_roundtrip_{}", suffix);
    std::filesystem::create_directories(path);
  }

  ~TemporaryDirectory()
  {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }

  const std::filesystem::path& get() const { return path; }

private:
  std::filesystem::path path;
};

void writeFile(const std::filesystem::path& path, const std::string_view contents)
{
  std::ofstream output(path);
  if (!output)
    throw std::runtime_error(std::format("Could not open '{}'", path.string()));
  output << contents;
  if (!output)
    throw std::runtime_error(std::format("Could not write '{}'", path.string()));
}

std::string readFile(const std::filesystem::path& path)
{
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error(std::format("Could not open '{}'", path.string()));
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string shellQuote(const std::string_view value)
{
  std::string quoted("'");
  for (const char character : value)
    quoted += character == '\'' ? "'\"'\"'" : std::string(1, character);
  return quoted + "'";
}

std::string yosysQuote(const std::filesystem::path& path)
{
  std::string quoted("\"");
  for (const char character : path.string()) {
    if (character == '\\' || character == '"')
      quoted += '\\';
    quoted += character;
  }
  return quoted + "\"";
}

void runYosys(const std::filesystem::path& scriptPath, const std::string_view script,
              const std::string_view failure)
{
  writeFile(scriptPath, script);
  const auto command = std::format("{} -q -s {}", shellQuote(SILICON_YOSYS_EXECUTABLE),
                                   shellQuote(scriptPath.string()));
  if (std::system(command.c_str()) != 0)
    throw std::runtime_error(std::string(failure));
}

}  // namespace

int main(const int argc, char** argv)
{
  try {
    if (argc > 3) {
      std::cerr << "Usage: yosys_roundtrip [input.v] [output.v]\n";
      return 2;
    }

    constexpr std::string_view moduleName = "three_flip_flops";
    const auto                 inputPath  = std::filesystem::absolute(
        argc >= 2 ? std::filesystem::path(argv[1]) : "silicon_three_flip_flops.v");
    const auto outputPath =
        std::filesystem::absolute(argc == 3 ? std::filesystem::path(argv[2])
                                            : "silicon_three_flip_flops_roundtrip.v");

    TemporaryDirectory temporary;
    const auto         sourceJsonPath   = temporary.get() / "source.json";
    const auto         siliconJsonPath  = temporary.get() / "silicon.json";
    const auto         importScriptPath = temporary.get() / "import.ys";
    const auto         exportScriptPath = temporary.get() / "export.ys";
    const auto         temporaryVerilog = temporary.get() / "roundtrip.v";

    runYosys(importScriptPath,
             std::format("read_verilog {}\n"
                         "hierarchy -check -top {}\n"
                         "proc\n"
                         "opt\n"
                         "write_json {}\n",
                         yosysQuote(inputPath), moduleName, yosysQuote(sourceJsonPath)),
             "Yosys failed to lower the input Verilog to JSON");

    const auto circuit = Circuit::deserializeYosys(readFile(sourceJsonPath), moduleName);
    writeFile(siliconJsonPath, circuit.getYosysJson());

    runYosys(exportScriptPath,
             std::format("read_json {}\n"
                         "hierarchy -check -top {}\n"
                         "opt\n"
                         "write_verilog -noattr {}\n",
                         yosysQuote(siliconJsonPath), moduleName,
                         yosysQuote(temporaryVerilog)),
             "Yosys failed to emit Verilog from Silicon's JSON");

    if (outputPath.has_parent_path())
      std::filesystem::create_directories(outputPath.parent_path());
    std::filesystem::copy_file(temporaryVerilog, outputPath,
                               std::filesystem::copy_options::overwrite_existing);
    std::cout << outputPath.string() << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "yosys_roundtrip: " << error.what() << '\n';
    return 1;
  }
}
