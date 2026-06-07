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

#include "fstWrapper.hpp"

#include <format>
#include <stdexcept>

// --- FstReader -------------------------------------------------------------------------

FstReader::FstReader(std::string_view fileName) : fn(fileName)
{
  // Acquire raw libfst reader context.
  const std::string fileNameStorage(fileName);
  auto*             raw_ctx = fstReaderOpen(fileNameStorage.c_str());

  if (!raw_ctx)
    throw std::runtime_error(std::format("Failed to open FST file for reading: {}", fn));

  // Transfer ownership
  context.reset(raw_ctx);
}

std::string FstReader::getVersion() const
{
  assert(context);

  const char* str = fstReaderGetVersionString(context.get());
  return str ? std::string(str) : "";
}

std::string FstReader::getDate() const
{
  assert(context);

  const char* str = fstReaderGetDateString(context.get());

  return str ? std::string(str) : "";
}

FstReader::FstScopeNode FstReader::buildHierarchyTree()
{
  assert(context);

  std::vector<FstScopeNode> stack;

  iterateHierarchy([&stack](const fstHier* hier) {
    if (hier->htyp == FST_HT_SCOPE) {
      // Enter a new scope
      stack.push_back(
          {std::string(hier->u.scope.name, hier->u.scope.name_length),
           std::string(hier->u.scope.component, hier->u.scope.component_length),
           static_cast<fstScopeType>(hier->u.scope.typ),
           {},
           {}});
    } else if (hier->htyp == FST_HT_VAR) {
      // Variable encountered inside current scope.
      if (!stack.empty()) {
        stack.back().vars.push_back(
            {std::string(hier->u.var.name, hier->u.var.name_length), hier->u.var.handle,
             hier->u.var.length, static_cast<fstVarDir>(hier->u.var.direction),
             static_cast<fstVarType>(hier->u.var.typ)});
      }
    } else if (hier->htyp == FST_HT_UPSCOPE) {
      // Exit current scope. The completed scope node is moved into its parent.
      if (!stack.empty() && stack.size() > 1) {
        FstScopeNode completed = std::move(stack.back());
        stack.pop_back();
        stack.back().children.push_back(std::move(completed));
      }
    }
  });

  if (stack.size() != 1)
    throw std::runtime_error(std::format(
        "SILICON only supports FSTs with exactly one top-level scope, {} has {}", fn,
        stack.size()));

  return std::move(stack.front());
}

FstReader::EnumTable FstReader::parseEnumTable(std::string_view enumString)
{
  const std::string enumStringStorage(enumString);
  fstETab*          etab =
      fstUtilityExtractEnumTableFromString(enumStringStorage.c_str());
  if (!etab)
    return {};

  EnumTable ret;
  if (etab->name)
    ret.name = etab->name;

  ret.mapping.reserve(etab->elem_count);
  for (uint32_t i = 0; i < etab->elem_count; ++i) {
    const char* literal = etab->literal_arr[i] ? etab->literal_arr[i] : "";
    const char* value   = etab->val_arr[i] ? etab->val_arr[i] : "";
    ret.mapping.emplace_back(literal, value);
  }

  fstUtilityFreeEnumTable(etab);
  return ret;
}

// --- FstHierarchyBuilder ---------------------------------------------------------------

FstHierarchyBuilder::FstHierarchyBuilder(std::string_view fileName,
                                         int              use_compressed_hier)
  : fn(fileName)
{
  const std::string fileNameStorage(fileName);
  auto*             raw_ctx =
      fstWriterCreate(fileNameStorage.c_str(), use_compressed_hier);

  if (!raw_ctx)
    throw std::runtime_error(
        std::format("Failed to create FST file for writing: {}", fn));

  context.reset(raw_ctx);
}

void FstHierarchyBuilder::setScope(fstScopeType scope_type, std::string_view scope_name,
                                   std::string_view scope_comp)
{
  assert(context);
  const std::string scopeName(scope_name);
  const std::string scopeComp(scope_comp);
  fstWriterSetScope(context.get(), scope_type, scopeName.c_str(), scopeComp.c_str());
}

fstHandle FstHierarchyBuilder::createVar(fstVarType var_type, fstVarDir var_dir,
                                         uint32_t len, std::string_view name,
                                         fstHandle aliasHandle)
{
  assert(context);

  const std::string nameStorage(name);
  return fstWriterCreateVar(context.get(), var_type, var_dir, len,
                            nameStorage.c_str(), aliasHandle);
}

fstEnumHandle FstHierarchyBuilder::createEnumTable(
    std::string_view name, unsigned int min_valbits,
    const std::vector<std::pair<const std::string, const std::string>>& values)
{
  assert(context);

  // libfst expects separate arrays of pointers rather than a vector of pairs. These
  // temporary arrays are constructed as lightweight pointer views into the
  // caller-provided strings.
  std::vector<const char*> literals;
  std::vector<const char*> vals;

  literals.reserve(values.size());
  vals.reserve(values.size());

  for (const auto& [lit, val] : values) {
    literals.push_back(lit.c_str());
    vals.push_back(val.c_str());
  }

  const std::string nameStorage(name);
  return fstWriterCreateEnumTable(context.get(), nameStorage.c_str(), values.size(),
                                  min_valbits, literals.data(), vals.data());
}

FstDataWriter FstHierarchyBuilder::finish() &&
{
  assert(context && "Builder already consumed!");

  // Transfer ownership of the configured writer context into the runtime waveform writer
  // phase.
  FstDataWriter writer(std::move(context));

  return writer;
}
